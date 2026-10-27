// main/music_wifi.c —— music_wifi.h 的实现。
//
// 线程模型:IDF 事件回调与 music_wifi_provision() 只入队;net_task 串行
// 消费队列(1s 无事件视为 TICK),驱动 music_net_sm 并执行其 action:
//   ap_enable   → Wi-Fi AP 模式 + 启动配网页(music_httpd)
//   sta_enable  → Wi-Fi STA 模式 + 停配网页
//   sta_connect → 写入 STA 凭据并连接
//   commit_candidate → NVS 覆盖已存凭据(仅连接测试成功时出现)
#include "music_wifi.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "music_httpd.h"

static const char *TAG = "music_wifi";

typedef struct {
    music_net_ev_t ev;
    char ssid[MUSIC_NET_SSID_MAX];
    char pass[MUSIC_NET_PASS_MAX];
} net_evt_t;

static music_net_sm_t s_sm;
static QueueHandle_t s_evtq;
static TaskHandle_t s_net_task;
static music_wifi_notify_fn s_notify_fn;
static void *s_notify_user;
static bool s_wifi_started;
static bool s_ap_active;  // 当前是否处于 AP 模式(httpd 是否在跑)

// ---- 事件入队(IDF 回调/表单线程只做这一件事) ---------------------------

static void enqueue_ev(music_net_ev_t ev) {
    if (!s_evtq) return;
    const net_evt_t item = { .ev = ev };
    (void)xQueueSend(s_evtq, &item, 0);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id,
                          void *event_data) {
    (void)arg;
    (void)base;
    switch (id) {
    case WIFI_EVENT_AP_STACONNECTED:
        enqueue_ev(MUS_NET_EV_CLIENT_JOINED);
        break;
    case WIFI_EVENT_AP_STADISCONNECTED:
        enqueue_ev(MUS_NET_EV_CLIENT_LEFT);
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        // 断开原因很多(主动断开/超时/密码错),状态机统一按失败处理,
        // 由重试计数与回落逻辑决定去向。
        enqueue_ev(MUS_NET_EV_STA_FAIL);
        break;
    default:
        break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id,
                        void *event_data) {
    (void)arg;
    (void)base;
    (void)event_data;
    if (id == IP_EVENT_STA_GOT_IP) enqueue_ev(MUS_NET_EV_STA_GOT_IP);
}

// ---- NVS ------------------------------------------------------------------

// 启动时读已存凭据;读不到(首次配网)返回 false。
static bool nvs_load_committed(char *ssid, size_t ssid_cap,
                               char *pass, size_t pass_cap) {
    nvs_handle_t h;
    if (nvs_open(MUSIC_WIFI_NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t ssid_len = ssid_cap;
    size_t pass_len = pass_cap;
    const esp_err_t e1 = nvs_get_str(h, MUSIC_WIFI_NVS_KEY_SSID, ssid, &ssid_len);
    const esp_err_t e2 = nvs_get_str(h, MUSIC_WIFI_NVS_KEY_PASS, pass, &pass_len);
    nvs_close(h);
    return e1 == ESP_OK && e2 == ESP_OK;
}

static void nvs_commit_committed(const char *ssid, const char *pass) {
    nvs_handle_t h;
    if (nvs_open(MUSIC_WIFI_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "NVS 打开失败,凭据只保留在内存");
        return;
    }
    const esp_err_t e1 = nvs_set_str(h, MUSIC_WIFI_NVS_KEY_SSID, ssid);
    const esp_err_t e2 = nvs_set_str(h, MUSIC_WIFI_NVS_KEY_PASS, pass);
    const esp_err_t e3 = nvs_commit(h);
    nvs_close(h);
    if (e1 != ESP_OK || e2 != ESP_OK || e3 != ESP_OK) {
        ESP_LOGE(TAG, "NVS 写入失败:%d/%d/%d", (int)e1, (int)e2, (int)e3);
    } else {
        ESP_LOGI(TAG, "新凭据已写入 NVS");
    }
}

// ---- Wi-Fi 模式切换 -------------------------------------------------------

// set_mode 在 stop/start 之间做才稳妥;AP 起 httpd,STA 停 httpd。
static void apply_mode(bool ap) {
    if (s_ap_active == ap && s_wifi_started) return;
    esp_wifi_stop();
    ESP_ERROR_CHECK(esp_wifi_set_mode(ap ? WIFI_MODE_AP : WIFI_MODE_STA));
    if (ap) {
        wifi_config_t cfg = { 0 };
        strlcpy((char *)cfg.ap.ssid, MUSIC_WIFI_AP_SSID, sizeof(cfg.ap.ssid));
        cfg.ap.ssid_len = strlen(MUSIC_WIFI_AP_SSID);
        cfg.ap.channel = 6;
        cfg.ap.max_connection = 2;
        cfg.ap.authmode = WIFI_AUTH_OPEN;  // 开放热点,配网页本身不带敏感数据
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_started = true;
    if (ap) {
        music_httpd_start_ap();
        ESP_LOGI(TAG, "热点就绪:SSID=%s(开放),配网页 http://192.168.4.1",
                 MUSIC_WIFI_AP_SSID);
    } else {
        music_httpd_stop();
    }
    s_ap_active = ap;
}

static void apply_action(const music_net_action_t *act) {
    if (act->ap_enable) apply_mode(true);
    if (act->sta_enable && !act->ap_enable) apply_mode(false);
    if (act->sta_connect) {
        wifi_config_t cfg = { 0 };
        strlcpy((char *)cfg.sta.ssid, act->sta_ssid, sizeof(cfg.sta.ssid));
        strlcpy((char *)cfg.sta.password, act->sta_pass, sizeof(cfg.sta.password));
        // 家用路由最通用的组合;WPA3-only 网络目前不支持(文档注明)。
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
        esp_wifi_connect();
        ESP_LOGI(TAG, "STA 连接中:%s", act->sta_ssid);
    }
    if (act->commit_candidate) {
        nvs_commit_committed(s_sm.committed_ssid, s_sm.committed_pass);
    }
}

static void step_sm(music_net_ev_t ev, const net_evt_t *item) {
    music_net_action_t act;
    const music_ns_t before = s_sm.state;
    // 表单凭据随 PROVISIONED 事件一起进队,在这里才写状态机(单线程)。
    if (ev == MUS_NET_EV_PROVISIONED && item) {
        music_net_sm_set_candidate(&s_sm, item->ssid, item->pass);
    }
    music_net_sm_step(&s_sm, ev, (uint32_t)(esp_timer_get_time() / 1000), &act);
    apply_action(&act);
    if (s_notify_fn && (act.notify || s_sm.state != before)) {
        s_notify_fn(act.notify, s_sm.state, s_notify_user);
    }
}

static void net_task(void *arg) {
    (void)arg;
    // BOOT 决定初始模式:有已存凭据直连 STA,否则开热点等配网。
    step_sm(MUS_NET_EV_BOOT, NULL);
    for (;;) {
        net_evt_t item;
        if (xQueueReceive(s_evtq, &item, pdMS_TO_TICKS(1000)) == pdTRUE) {
            step_sm(item.ev, &item);
        } else {
            step_sm(MUS_NET_EV_TICK, NULL);
        }
    }
}

// ---- 公开接口 -------------------------------------------------------------

void music_wifi_set_notify(music_wifi_notify_fn fn, void *user) {
    s_notify_fn = fn;
    s_notify_user = user;
}

bool music_wifi_provision(const char *ssid, const char *pass) {
    if (!s_evtq || !ssid || !pass) return false;
    // 基本校验(WPA2):SSID 1..32 字节,密码 8..64 字节且可见 ASCII。
    const size_t ssid_len = strlen(ssid);
    const size_t pass_len = strlen(pass);
    if (ssid_len == 0 || ssid_len > 32 || pass_len < 8 || pass_len > 64) return false;
    for (size_t i = 0; i < pass_len; i++) {
        if ((unsigned char)pass[i] < 0x20 || (unsigned char)pass[i] > 0x7E) return false;
    }
    net_evt_t item = { .ev = MUS_NET_EV_PROVISIONED };
    strlcpy(item.ssid, ssid, sizeof(item.ssid));
    strlcpy(item.pass, pass, sizeof(item.pass));
    return xQueueSend(s_evtq, &item, 0) == pdTRUE;
}

music_ns_t music_wifi_state(void) {
    return s_sm.state;
}

void music_wifi_start(void) {
    if (s_net_task) return;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // 两个接口的 netif 都先建好;后续只是 AP/STA 模式来回切。
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                               on_ip_event, NULL));

    music_net_sm_init(&s_sm);
    char ssid[MUSIC_NET_SSID_MAX];
    char pass[MUSIC_NET_PASS_MAX];
    if (nvs_load_committed(ssid, sizeof(ssid), pass, sizeof(pass))) {
        music_net_sm_set_committed(&s_sm, ssid, pass);
        ESP_LOGI(TAG, "已存凭据:SSID=%s(直接尝试连接)", ssid);
    } else {
        ESP_LOGI(TAG, "无已存凭据,等待网页配网");
    }

    s_evtq = xQueueCreate(8, sizeof(net_evt_t));
    if (!s_evtq || xTaskCreate(net_task, "music_net", 6144, NULL, 4,
                               &s_net_task) != pdPASS) {
        ESP_LOGE(TAG, "net 任务创建失败,配网不可用");
        s_net_task = NULL;
        if (s_evtq) {
            vQueueDelete(s_evtq);
            s_evtq = NULL;
        }
    }
}
