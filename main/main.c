// main/main.c —— musicdl 音乐播放器应用:初始化 + 输入分发 + 导航接线。
//
// 按键语义(全局统一,与三键参考文档一致:单击/长按才是意图,双击显式处理):
//   上/下 短按   列表中=移动选中项(clamp,不循环);播放页=上一首/下一首
//   确定  短按   列表中=播放选中项;播放页=暂停/续播
//   确定  长按   返回上级(配网页=回首页)
//   上/下 长按   播放页=音量调节
//
// 本提交阶段的边界:UI 导航全量可用;榜单数据为本地静态榜单名(联网拉取
// 在后续提交接入),歌曲队列因此为空,播放页显示占位状态。
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
#include "music_fonts.h"
#include "music_kuwo.h"
#include "music_queue.h"
#include "music_ui_nav.h"
#include "music_wifi.h"
#include "ui_music.h"

#include <stdio.h>

static const char *TAG = "main";

#define INPUT_QUEUE_DEPTH 8

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;
static TaskHandle_t s_input_task;
static volatile bool s_input_ready;

// ---- 应用状态 -------------------------------------------------------------

// 榜单首页数据:名字静态,联网后拉取歌曲填进队列。
typedef struct {
    const char *name;
    int bang;  // music_kuwo.h 的 MUSIC_KUWO_BANG_*
} chart_entry_t;
static const chart_entry_t CHARTS[] = {
    { "热歌榜", MUSIC_KUWO_BANG_HOT },
    { "新歌榜", MUSIC_KUWO_BANG_NEW },
    { "华语榜", MUSIC_KUWO_BANG_CN },
    { "飙升榜", MUSIC_KUWO_BANG_RISING },
    { "短视频热歌", MUSIC_KUWO_BANG_SHORT_VIDEO },
};
#define CHART_COUNT (sizeof(CHARTS) / sizeof(CHARTS[0]))

#define SETTINGS_COUNT 3   // 音量 / 重新配网 / 关于
#define VOLUME_DEFAULT 70

// 歌曲列表的当前来源与抓取进度(榜单来自按键,搜索来自网页遥控)。
typedef enum { LST_NONE = 0, LST_LOADING, LST_READY, LST_EMPTY, LST_FAIL } list_state_t;
typedef enum { SRC_NONE = 0, SRC_CHART, SRC_SEARCH } list_src_t;
static list_state_t s_list_state;
static list_src_t s_list_src;
static int32_t s_list_chart_id;

static music_ui_nav_t s_nav;
static music_queue_t s_queue;
static uint8_t s_volume = VOLUME_DEFAULT;
static bool s_paused;
static music_screen_t s_cur_screen = MUS_SCR_HOME;  // switch_screen 的镜像

// 屏幕(懒构建,构建后常驻复用;换屏只切 active)。
static lv_obj_t *s_boot_scr;
static lv_obj_t *s_prov_scr;
static ui_music_list_t s_home_ui;      // 榜单首页
static ui_music_list_t s_songs_ui;     // 歌曲列表
static ui_music_list_t s_settings_ui;  // 设置
static ui_music_play_t s_play_ui;      // 正在播放

// ---- 列表绘制 -------------------------------------------------------------

// 通用行填充:绝对下标 abs = page_top + row。
static void draw_list_page(ui_music_list_t *ui, const music_ui_nav_t *nav) {
    for (uint8_t row = 0; row < UI_MUSIC_LIST_ROWS; row++) {
        const uint8_t abs = nav->page_top + row;
        if (abs < nav->list_len) {
            char num[4];
            snprintf(num, sizeof(num), "%u", (unsigned)abs + 1);
            ui_music_list_row_set(ui, row, num, "", "", abs == nav->cursor);
        } else {
            ui_music_list_row_set(ui, row, "", "", "", false);
        }
    }
    ui_music_list_page_set(ui, music_ui_nav_page(nav), music_ui_nav_pages(nav));
}

static void draw_home(void) {
    draw_list_page(&s_home_ui, &s_nav);
    for (uint8_t i = 0; i < CHART_COUNT && i < UI_MUSIC_LIST_ROWS; i++) {
        char num[4];
        snprintf(num, sizeof(num), "%u", (unsigned)i + 1);
        ui_music_list_row_set(&s_home_ui, i, num, CHARTS[i].name, "",
                              i == s_nav.cursor);
    }
    ui_music_list_hint_set(&s_home_ui, NULL);
}

static void draw_songs(void) {
    draw_list_page(&s_songs_ui, &s_nav);
    for (uint8_t i = 0; i < UI_MUSIC_LIST_ROWS && s_nav.page_top + i < s_nav.list_len; i++) {
        const music_song_t *song = music_queue_at(&s_queue, s_nav.page_top + i);
        char num[4];
        snprintf(num, sizeof(num), "%u", (unsigned)s_nav.page_top + i + 1);
        ui_music_list_row_set(&s_songs_ui, i, num,
                              song ? song->title : "", song ? song->artist : "",
                              s_nav.page_top + i == s_nav.cursor);
    }
    const char *hint = NULL;
    switch (s_list_state) {
    case LST_LOADING: hint = "加载中..."; break;
    case LST_EMPTY: hint = "没有找到歌曲"; break;
    case LST_FAIL: hint = "加载失败,稍后重试"; break;
    case LST_NONE: hint = "联网后进入榜单自动加载"; break;
    default: break;  // LST_READY:不显示
    }
    ui_music_list_hint_set(&s_songs_ui, hint);
}

static void draw_settings(void) {
    char vol[8];
    snprintf(vol, sizeof(vol), "%u%%", (unsigned)s_volume);
    const char *subs[SETTINGS_COUNT] = { vol, "", "v0.1" };
    const char *mains[SETTINGS_COUNT] = { "音量", "重新配网", "关于" };
    draw_list_page(&s_settings_ui, &s_nav);
    for (uint8_t i = 0; i < SETTINGS_COUNT && i < UI_MUSIC_LIST_ROWS; i++) {
        char num[4];
        snprintf(num, sizeof(num), "%u", (unsigned)i + 1);
        ui_music_list_row_set(&s_settings_ui, i, num, mains[i], subs[i],
                              i == s_nav.cursor);
    }
    ui_music_list_hint_set(&s_settings_ui, NULL);
}

// ---- 播放页刷新 -----------------------------------------------------------

static void play_page_refresh(void) {
    const music_song_t *song = music_queue_current(&s_queue);
    ui_music_play_song_set(&s_play_ui, song);
    ui_music_play_state_set(&s_play_ui, song ? (s_paused ? "已暂停" : "待联网") : "未在播放");
    ui_music_play_volume_set(&s_play_ui, s_volume);
    ui_music_play_eq_set(&s_play_ui, false);
}

// ---- 换屏 -----------------------------------------------------------------

// 调用前 nav->screen 已是目标屏;本函数按屏懒构建 + 重置列表长度 + 重绘。
static void switch_screen(music_screen_t scr) {
    switch (scr) {
    case MUS_SCR_HOME:
        music_ui_nav_set_list(&s_nav, (uint8_t)CHART_COUNT);
        if (!s_home_ui.screen) ui_music_list_screen_build(&s_home_ui, "榜单");
        draw_home();
        lv_screen_load(s_home_ui.screen);
        break;
    case MUS_SCR_LIST:
        music_ui_nav_set_list(&s_nav, music_queue_count(&s_queue));
        if (!s_songs_ui.screen) ui_music_list_screen_build(&s_songs_ui, "歌曲");
        draw_songs();
        lv_screen_load(s_songs_ui.screen);
        break;
    case MUS_SCR_PLAY:
        if (!s_play_ui.screen) ui_music_play_screen_build(&s_play_ui);
        play_page_refresh();
        lv_screen_load(s_play_ui.screen);
        break;
    case MUS_SCR_SETTINGS:
        music_ui_nav_set_list(&s_nav, SETTINGS_COUNT);
        if (!s_settings_ui.screen) ui_music_list_screen_build(&s_settings_ui, "设置");
        draw_settings();
        lv_screen_load(s_settings_ui.screen);
        break;
    case MUS_SCR_PROVISION:
        // 热点名与 music_wifi 实际广播的 SSID 同源,指引与实物一致。
        if (!s_prov_scr) s_prov_scr = ui_music_provision_screen_create(MUSIC_WIFI_AP_SSID);
        lv_screen_load(s_prov_scr);
        break;
    }
    s_cur_screen = scr;
}

// 请求一个榜单(同一榜单已缓存则跳过;清空队列,列表页显示加载中)。
static void browse_chart(int bang) {
    if (s_list_src == SRC_CHART && s_list_chart_id == bang &&
        s_list_state == LST_READY) {
        return;
    }
    s_list_src = SRC_CHART;
    s_list_chart_id = bang;
    s_list_state = LST_LOADING;
    music_queue_clear(&s_queue);
    music_queue_set_current(&s_queue, -1);
    music_wifi_request_chart(bang);
}

// ---- 意图分发(已持 LVGL 锁) ----------------------------------------------

static void apply_action(const music_ui_action_t *act) {
    // 首页 OK 进列表 = 选中榜单,先发抓取请求再切屏(cursor 此刻还是榜单下标)。
    if (act->change_screen && act->new_screen == MUS_SCR_LIST &&
        s_cur_screen == MUS_SCR_HOME && s_nav.cursor < CHART_COUNT) {
        browse_chart(CHARTS[s_nav.cursor].bang);
    }
    if (act->change_screen) switch_screen(act->new_screen);

    if (act->play_cursor) {
        music_queue_set_current(&s_queue, (int8_t)s_nav.cursor);
        s_paused = false;
        switch_screen(MUS_SCR_PLAY);
    }

    if (act->redraw_list) {
        switch (s_nav.screen) {
        case MUS_SCR_HOME: draw_home(); break;
        case MUS_SCR_LIST: draw_songs(); break;
        case MUS_SCR_SETTINGS: draw_settings(); break;
        default: break;
        }
    }

    if (act->toggle_pause && music_queue_current(&s_queue)) {
        s_paused = !s_paused;
        ui_music_play_state_set(&s_play_ui, s_paused ? "已暂停" : "待联网");
        ui_music_play_eq_set(&s_play_ui, false);
    }

    if (act->next_track || act->prev_track) {
        const int8_t idx = act->next_track ? music_queue_next_index(&s_queue)
                                           : music_queue_prev_index(&s_queue);
        if (idx >= 0) {
            music_queue_set_current(&s_queue, idx);
            s_paused = false;
            play_page_refresh();
        }
    }

    if (act->vol_delta) {
        int v = (int)s_volume + act->vol_delta;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        s_volume = (uint8_t)v;
        ui_music_play_volume_set(&s_play_ui, s_volume);
    }

    if (act->settings_select && act->settings_index == 1) {
        // "重新配网" → 配网指引页(OK 长按返回首页)。
        switch_screen(MUS_SCR_PROVISION);
    }
}

// ---- 输入链路 -------------------------------------------------------------

static bool map_key(bsp_btn_t btn, music_key_t *out) {
    switch (btn) {
    case BSP_BTN_UP: *out = MUS_KEY_UP; return true;
    case BSP_BTN_DOWN: *out = MUS_KEY_DOWN; return true;
    case BSP_BTN_OK: *out = MUS_KEY_OK; return true;
    default: return false;
    }
}

static bool map_kev(bsp_btn_ev_t ev, music_kev_t *out) {
    switch (ev) {
    case BSP_BTN_CLICK: *out = MUS_KEV_CLICK; return true;
    case BSP_BTN_DOUBLE: *out = MUS_KEV_DOUBLE; return true;
    case BSP_BTN_LONG: *out = MUS_KEV_LONG; return true;
    default: return false;  // PRESS/RELEASE 不是意图事件
    }
}

static void process_input(const input_event_t *input) {
    music_key_t key;
    music_kev_t kev;
    if (!map_key(input->btn, &key) || !map_kev(input->event, &kev)) return;
    music_ui_action_t act;
    if (!music_ui_nav_key(&s_nav, key, kev, &act)) return;
    if (bsp_lvgl_lock(500)) {
        apply_action(&act);
        bsp_lvgl_unlock();
    } else {
        ESP_LOGW(TAG, "LVGL 锁超时,忽略一次按键意图");
    }
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

// 启动页停留 ~1s 后进首页(一次性;回调在 LVGL 任务里,无需另加锁)。
static void boot_timer_cb(lv_timer_t *timer) {
    lv_timer_delete(timer);
    music_ui_nav_goto(&s_nav, MUS_SCR_HOME);
    switch_screen(MUS_SCR_HOME);
}

// ---- 网络状态 → UI ---------------------------------------------------------
// net_task 上下文回调:状态栏图标 + 配网页自动进出(首次配网免手动进)。
static void on_net_notify(uint32_t bits, music_ns_t state, void *user) {
    (void)user;
    if (!bsp_lvgl_lock(500)) {
        ESP_LOGW(TAG, "LVGL 锁超时,丢弃一次网络状态更新");
        return;
    }
    // -1 未联网(隐藏)/0 热点配网中(扳手)/1 已联网(WiFi)。
    ui_music_status_set_net(state == MUS_NS_ONLINE ? 1
                            : (state == MUS_NS_AP_WAIT ? 0 : -1));
    // 热点起来且用户还停在浏览页 → 引导去配网页;连上了 → 从配网页回首页。
    if ((bits & (MUS_NET_NOTIFY_AP_UP | MUS_NET_NOTIFY_BACK_TO_AP)) &&
        (s_nav.screen == MUS_SCR_HOME || s_nav.screen == MUS_SCR_LIST ||
         s_nav.screen == MUS_SCR_PLAY)) {
        music_ui_nav_goto(&s_nav, MUS_SCR_PROVISION);
        switch_screen(MUS_SCR_PROVISION);
    }
    if ((bits & MUS_NET_NOTIFY_ONLINE) && s_nav.screen == MUS_SCR_PROVISION) {
        music_ui_nav_goto(&s_nav, MUS_SCR_HOME);
        switch_screen(MUS_SCR_HOME);
    }
    bsp_lvgl_unlock();
}

// net_task 上下文:榜单/搜索抓取完成 → 换掉歌曲队列并刷新列表页。
// 只接受当前来源的结果(用户已切去别的榜单时,迟到的旧结果直接丢)。
static void on_browse(const music_browse_result_t *r, void *user) {
    (void)user;
    const bool matches =
        (r->chart_id > 0 && s_list_src == SRC_CHART && s_list_chart_id == r->chart_id) ||
        (r->chart_id < 0 && s_list_src == SRC_SEARCH);
    if (!matches) return;
    if (!bsp_lvgl_lock(500)) {
        // 锁超时:放弃本批数据,列表停留在加载中;用户重进榜单即可重试。
        ESP_LOGW(TAG, "LVGL 锁超时,丢弃一批列表结果");
        return;
    }
    music_queue_clear(&s_queue);
    if (r->ok) {
        for (uint8_t i = 0; i < r->count; i++) music_queue_push(&s_queue, &r->songs[i]);
        s_list_state = r->count ? LST_READY : LST_EMPTY;
    } else {
        s_list_state = LST_FAIL;
    }
    if (s_nav.screen == MUS_SCR_LIST) {
        music_ui_nav_set_list(&s_nav, music_queue_count(&s_queue));
        draw_songs();
    }
    bsp_lvgl_unlock();
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
        // 中文字体:全部界面文字走 16px 点阵,LVGL 符号走 montserrat 回退。
        music_fonts_init();
        ui_music_set_default_font(&music_font_16);
        music_ui_nav_init(&s_nav, UI_MUSIC_LIST_ROWS);
        music_queue_clear(&s_queue);

        // 启动页 → (一次性定时器) → 榜单首页。
        s_boot_scr = ui_music_boot_screen_create("musicdl", "在线音乐播放器");
        lv_screen_load(s_boot_scr);
        lv_timer_create(boot_timer_cb, 1000, NULL);
        bsp_lvgl_unlock();
        s_input_ready = true;
    }

    // 网络生命周期:有已存凭据直连,否则开热点等网页配网。
    music_wifi_set_notify(on_net_notify, NULL);
    music_wifi_set_browse(on_browse, NULL);
    music_wifi_start();

    ESP_LOGI(TAG, "就绪:Battery=%d Input=%d", battery_ok, input_err == ESP_OK);
}
