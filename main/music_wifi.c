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
#include "music_http.h"
#include "music_json.h"
#include "music_kuwo.h"

static const char *TAG = "music_wifi";

// 队列条目:状态机事件 / 浏览请求(榜单/搜索)两类,net_task 统一串行处理。
typedef enum {
    NEV_SM = 0,       // music_net_ev_t 事件
    NEV_CHART,        // 抓取榜单
    NEV_SEARCH,       // 抓取搜索结果
} net_evt_kind_t;

typedef struct {
    net_evt_kind_t kind;
    music_net_ev_t ev;
    int32_t chart_id;
    char keyword[65];
    // NEV_SM 的 PROVISIONED 事件载荷(net_task 里才写进状态机)。
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
    const net_evt_t item = { .kind = NEV_SM, .ev = ev };
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
    // 上线才开遥控页:搜索要出外网,过早开放只会有失败请求。
    if (s_sm.state == MUS_NS_ONLINE && before != MUS_NS_ONLINE) {
        music_httpd_start_sta();
    }
    if (s_notify_fn && (act.notify || s_sm.state != before)) {
        s_notify_fn(act.notify, s_sm.state, s_notify_user);
    }
}

// ---- 榜单/搜索抓取(net_task 内串行执行) -----------------------------------

static music_browse_result_t s_result;
static music_browse_fn s_browse_fn;
static void *s_browse_user;

typedef struct {
    music_json_parser_t *p;
    music_browse_result_t *r;
} list_ctx_t;

static void on_list_song(void *user, const music_song_t *song, uint8_t index) {
    (void)index;
    list_ctx_t *ctx = user;
    if (ctx->r->count < MUSIC_QUEUE_CAP) ctx->r->songs[ctx->r->count++] = *song;
}

static bool on_list_chunk(const char *data, size_t len, void *user) {
    music_json_parser_t *p = user;
    (void)music_json_feed(p, data, len);
    return !p->error && !p->done;  // 畸形或收满都提前停
}

// 抓一页列表(rn=30,SAX 收满 16 首即断开,不整包缓冲)。
static void fetch_list(const char *url, music_json_list_t kind, int32_t chart_id,
                       const char *keyword) {
    memset(&s_result, 0, sizeof(s_result));
    s_result.chart_id = chart_id;
    strlcpy(s_result.keyword, keyword ? keyword : "", sizeof(s_result.keyword));
    if (s_sm.state != MUS_NS_ONLINE) {
        ESP_LOGW(TAG, "未联网,忽略浏览请求(%s)", url);
    } else {
        music_json_parser_t p;
        list_ctx_t ctx = { .p = &p, .r = &s_result };
        music_json_songs_init(&p, kind, MUSIC_QUEUE_CAP, on_list_song, &ctx);
        const int status = music_http_get(url, on_list_chunk, &p);
        s_result.ok = status == 200 && !p.error;
        ESP_LOGI(TAG, "列表抓取完成:%s HTTP=%d 收到 %u 首",
                 chart_id > 0 ? "榜单" : "搜索", status, s_result.count);
    }
    if (s_browse_fn) s_browse_fn(&s_result, s_browse_user);
}

// 抓取请求本身参数不合法(URL 构造失败):直接以失败结果回调。
static void fetch_fail(int32_t chart_id, const char *keyword) {
    memset(&s_result, 0, sizeof(s_result));
    s_result.chart_id = chart_id;
    strlcpy(s_result.keyword, keyword ? keyword : "", sizeof(s_result.keyword));
    if (s_browse_fn) s_browse_fn(&s_result, s_browse_user);
}

static void handle_fetch(const net_evt_t *item) {
    char url[MUSIC_KUWO_URL_MAX];
    if (item->kind == NEV_CHART) {
        if (music_kuwo_chart_url((music_kuwo_bang_t)item->chart_id, 0, 30,
                                 url, sizeof(url)) == (size_t)-1) {
            fetch_fail(item->chart_id, NULL);
            return;
        }
        fetch_list(url, MUSIC_JSON_LIST_CHART, item->chart_id, NULL);
    } else {
        if (music_kuwo_search_url(item->keyword, 0, 30, url, sizeof(url)) == (size_t)-1) {
            fetch_fail(-1, item->keyword);
            return;
        }
        fetch_list(url, MUSIC_JSON_LIST_SEARCH, -1, item->keyword);
    }
}

static void net_task(void *arg) {
    (void)arg;
    // BOOT 决定初始模式:有已存凭据直连 STA,否则开热点等配网。
    step_sm(MUS_NET_EV_BOOT, NULL);
    for (;;) {
        net_evt_t item;
        if (xQueueReceive(s_evtq, &item, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (item.kind == NEV_SM) step_sm(item.ev, &item);
            else handle_fetch(&item);
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

void music_wifi_set_browse(music_browse_fn fn, void *user) {
    s_browse_fn = fn;
    s_browse_user = user;
}

static bool enqueue_fetch(net_evt_kind_t kind, int32_t chart_id, const char *keyword) {
    if (!s_evtq) return false;
    net_evt_t e = { .kind = kind, .chart_id = chart_id };
    if (keyword) strlcpy(e.keyword, keyword, sizeof(e.keyword));
    return xQueueSend(s_evtq, &e, 0) == pdTRUE;
}

bool music_wifi_request_chart(int32_t bang_id) {
    return enqueue_fetch(NEV_CHART, bang_id, NULL);
}

bool music_wifi_request_search(const char *keyword_utf8) {
    if (!keyword_utf8 || !keyword_utf8[0] || strlen(keyword_utf8) > 64) return false;
    return enqueue_fetch(NEV_SEARCH, -1, keyword_utf8);
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
