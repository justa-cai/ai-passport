// main/main.c —— musicdl 音乐播放器应用:初始化 + 输入分发。
//
// 按键语义(全局统一,与三键参考文档一致:单击/长按才是意图,双击显式处理):
//   上/下 短按   列表中=移动选中项(clamp,不循环);播放页=上一首/下一首
//   确定  短按   列表中=播放选中项;播放页=暂停/续播
//   确定  长按   返回上级
//   上/下 长按   播放页=音量调节
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "key_guard.h"
#include "lvgl.h"
#include "ui_music.h"

static const char *TAG = "main";

#define INPUT_QUEUE_DEPTH 8

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;
static TaskHandle_t s_input_task;
static volatile bool s_input_ready;

// 骨架阶段唯一的页面:榜单首页占位(网络与列表在后续提交接入)。
static lv_obj_t *s_home_scr;
static lv_obj_t *s_home_hint;

static void home_build(void) {
    s_home_scr = ui_music_screen_create("musicdl");
    s_home_hint = ui_music_label_create(s_home_scr, 8, 64, 224,
                                        "Charts loading...", false);
    lv_screen_load(s_home_scr);
}

static void process_input(const input_event_t *input) {
    // 骨架阶段:按键只打日志,验证输入链路;导航状态机随后续提交接入。
    ESP_LOGI(TAG, "key: btn=%d ev=%d", (int)input->btn, (int)input->event);
}

static void input_task(void *arg) {
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) == pdTRUE) {
            process_input(&input);
        }
    }
}

static esp_err_t input_dispatch_init(void) {
    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(input_task, "music_input", 4096, NULL, 5, &s_input_task) != pdPASS) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// 按键唤醒时那颗键在整个启动期间还按着:key_guard 挡住它的动作直到松手
// (状态机在 key_guard.h,可独立于应用做主机测试)。
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

// button callbacks run on the shared esp_timer task; enqueue only and return immediately.
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手,按键恢复响应");
        }
        return;
    }
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

void app_main(void) {
    ESP_LOGI(TAG, "musicdl 播放器启动");
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒:先屏蔽按键动作,直到那颗键松手");
    }

    // 显示是本应用的 UI 载体,失败即无可事事:打清日志后退出。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,应用无法继续。"
                      "检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    // 电量计驱动状态栏指示;读不到时 UI 自动降级隐藏。
    const bool battery_ok = bsp_battery_init() == ESP_OK;
    // 音频懒初始化:首次播放时才 bsp_audio_init(),配网/浏览阶段省内存。

    const esp_err_t input_err = input_dispatch_init();
    if (input_err != ESP_OK) {
        ESP_LOGE(TAG, "按键事件任务创建失败: %s", esp_err_to_name(input_err));
    } else {
        esp_err_t button_err = bsp_button_init(on_key, NULL);
        if (button_err != ESP_OK) {
            ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(button_err));
        } else if (s_key_guard.armed) {
            // 唤醒键在按键驱动启动前已松手时,组件不会发 release 事件,
            // 按真实电压同步一次避免守卫误拦。
            key_guard_sync_released(&s_key_guard,
                bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN);
        }
    }

    if (bsp_lvgl_lock(1000)) {
        home_build();
        bsp_lvgl_unlock();
        s_input_ready = true;
    }

    ESP_LOGI(TAG, "就绪:Battery=%d Input=%d", battery_ok, input_err == ESP_OK);
}
