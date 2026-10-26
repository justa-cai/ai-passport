/* 拼音卡片全套 · 同步一年级上册拼读训练 —— 应用入口。
 * 初始化顺序沿用基线（I2C → 显示 → LVGL → 背光 → 输入/音频/电池），
 * 应用自持屏幕路由与输入分发（按钮回调仅入队，慢操作在 worker 任务）。*/
#include <stdatomic.h>

#include "app.h"
#include "app_fonts.h"
#include "app_progress.h"
#include "app_screens.h"
#include "app_ui.h"
#include "audio_player.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "screen_dump.h"

static const char *TAG = "main";

#define INPUT_QUEUE_LEN 8
#define INPUT_TASK_STACK 4096
#define INPUT_TASK_PRIO 5
#define IDLE_DIM_MS 45000
#define IDLE_DIM_PERCENT 20

typedef struct {
    void (*enter)(void);
    void (*exit)(void);
    bool (*key)(bsp_btn_t btn, bsp_btn_ev_t ev);
    app_screen_id_t back; /* OK 长按的返回目标（HOME 无返回）*/
} screen_hdl_t;

static const screen_hdl_t k_screens[] = {
    [APP_SCR_HOME] = {app_home_enter, app_home_exit, app_home_key, APP_SCR_HOME},
    [APP_SCR_LESSONS] = {app_lessons_enter, app_lessons_exit, app_lessons_key, APP_SCR_HOME},
    [APP_SCR_CARD] = {app_card_enter, app_card_exit, app_card_key, APP_SCR_LESSONS},
    [APP_SCR_QUIZ] = {app_quiz_enter, app_quiz_exit, app_quiz_key, APP_SCR_HOME},
};

static app_screen_id_t s_current = APP_SCR_HOME;
static QueueHandle_t s_input_queue;
static TaskHandle_t s_input_task;
static atomic_int s_input_ready = 0;
static int64_t s_last_activity_us;
static lv_timer_t *s_idle_timer;

pinyin_progress_t *app_progress(void)
{
    return pinyin_progress();
}

void app_progress_commit(void)
{
    pinyin_progress_commit();
}

void app_nav_goto(app_screen_id_t id)
{
    /* 调用方（input_task/process_input）已持 LVGL 锁 */
    if (id == s_current) {
        return;
    }
    const screen_hdl_t *cur = &k_screens[s_current];
    const screen_hdl_t *next = &k_screens[id];
    if (cur->exit != NULL) {
        cur->exit();
    }
    s_current = id;
    if (next->enter != NULL) {
        next->enter();
    }
}

void app_debug_nav(int screen_id)
{
    /* 调试控制台切屏用：与 process_input 相同的锁约定 */
    if (screen_id < 0 || screen_id >= (int)(sizeof(k_screens) / sizeof(k_screens[0]))) {
        return;
    }
    if (bsp_lvgl_lock(2000)) {
        app_nav_goto((app_screen_id_t)screen_id);
        bsp_lvgl_unlock();
    }
}

void app_debug_key(int btn, int ev)
{
    /* 调试控制台注入按键：与 on_key 相同的入队通道（queue-send 跨任务安全）*/
    if (!atomic_load(&s_input_ready)) {
        return;
    }
    struct {
        bsp_btn_t btn;
        bsp_btn_ev_t ev;
    } msg = {(bsp_btn_t)btn, (bsp_btn_ev_t)ev};
    xQueueSend(s_input_queue, &msg, 0);
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    /* 按钮组件共享 esp_timer 任务上下文：只入队（AGENTS.md 运行时约定）*/
    (void)user;
    if (!atomic_load(&s_input_ready)) {
        return;
    }
    struct {
        bsp_btn_t btn;
        bsp_btn_ev_t ev;
    } msg = {btn, ev};
    xQueueSend(s_input_queue, &msg, 0);
}

static void process_input(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!bsp_lvgl_lock(500)) {
        ESP_LOGW(TAG, "LVGL 锁超时，丢弃按键");
        return;
    }
    const screen_hdl_t *scr = &k_screens[s_current];
    bool handled = scr->key != NULL && scr->key(btn, ev);
    if (!handled && btn == BSP_BTN_OK && ev == BSP_BTN_LONG &&
        k_screens[s_current].back != s_current) {
        audio_player_play_fx(FX_PAGE);
        app_nav_goto(k_screens[s_current].back);
    }
    bsp_lvgl_unlock();
}

static void input_task(void *arg)
{
    (void)arg;
    struct {
        bsp_btn_t btn;
        bsp_btn_ev_t ev;
    } msg;
    for (;;) {
        if (xQueueReceive(s_input_queue, &msg, portMAX_DELAY) == pdTRUE) {
            s_last_activity_us = esp_timer_get_time();
            process_input(msg.btn, msg.ev);
        }
    }
}

static void idle_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    int64_t idle = esp_timer_get_time() - s_last_activity_us;
    if (idle > IDLE_DIM_MS * 1000) {
        bsp_display_backlight(IDLE_DIM_PERCENT);
    } else if (idle < 1000 * 1000) {
        /* 刚有输入：恢复亮度 */
        bsp_display_backlight(100);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "拼音拼读训练 启动");

    ESP_ERROR_CHECK(bsp_i2c_init());
    bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "显示初始化失败 MOSI=%d SCLK=%d CS=%d DC=%d BL=%d",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    pinyin_progress_open();

    if (audio_player_start() != ESP_OK) {
        ESP_LOGW(TAG, "音频不可用，界面将以纯视觉模式运行");
    }

    esp_err_t battery_err = bsp_battery_init();
    if (battery_err != ESP_OK) {
        ESP_LOGW(TAG, "电量计不可用(%d)，徽标显示 --", battery_err);
    }

    s_input_queue = xQueueCreate(INPUT_QUEUE_LEN, sizeof(bsp_btn_t) + sizeof(bsp_btn_ev_t));
    assert(s_input_queue != NULL);
    assert(xTaskCreate(input_task, "py_input", INPUT_TASK_STACK, NULL,
                       INPUT_TASK_PRIO, &s_input_task) == pdPASS);
    ESP_ERROR_CHECK(bsp_button_init(on_key, NULL));
    screen_dump_start(); /* 调试控制台（shot/heap，详见 screen_dump.h）*/

    if (bsp_lvgl_lock(1000)) {
        app_fonts_init();
        k_screens[APP_SCR_HOME].enter();
        s_idle_timer = lv_timer_create(idle_timer_cb, 2000, NULL);
        s_last_activity_us = esp_timer_get_time();
        atomic_store(&s_input_ready, 1);
        bsp_lvgl_unlock();
    } else {
        ESP_LOGE(TAG, "LVGL 锁获取失败，无法进入界面");
        return;
    }

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "LVGL 内存: total=%u used=%u%% frag=%u%% free=%u biggest=%u",
             (unsigned)mon.total_size, (unsigned)mon.used_pct,
             (unsigned)mon.frag_pct, (unsigned)mon.free_size,
             (unsigned)mon.free_biggest_size);
}
