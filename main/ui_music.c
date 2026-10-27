// main/ui_music.c —— musicdl 播放器视觉外壳实现。
//
// 线程纪律:本模块所有函数都必须在持有 LVGL 锁(或 LVGL 任务内)调用;
// 状态栏电量/网络的对外入口只有 ui_music_status_set_net() 与内部定时器,
// 前者由应用主任务在锁内调用。
#include "ui_music.h"

#include "bsp_battery.h"

// 当前生效的缺省字体(接入中文字体前为 Montserrat 14)。
static const lv_font_t *s_font = &lv_font_montserrat_14;

// 状态栏对象挂在当前屏幕上;换屏时由 screen_create 重建。
static lv_obj_t *s_status_scr;
static lv_obj_t *s_status_net;
static lv_obj_t *s_status_bat;
static lv_timer_t *s_status_timer;
static int s_net_mode = -1;

void ui_music_set_default_font(const lv_font_t *font) {
    s_font = font ? font : &lv_font_montserrat_14;
}

// 电量指示:电量计不可读(-1)时隐藏;低于 20% 显示红色(基线 demo_battery 惯例)。
static void status_battery_apply(int soc) {
    if (!s_status_bat) return;
    if (soc < 0) {
        lv_obj_add_flag(s_status_bat, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(s_status_bat, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(s_status_bat, "%d%%", soc);
    lv_obj_set_style_text_color(s_status_bat,
        lv_color_hex(soc < 20 ? UI_MUSIC_DANGER : UI_MUSIC_MUTED), 0);
}

static void status_net_apply(int mode) {
    if (!s_status_net) return;
    switch (mode) {
    case 1:
        lv_obj_remove_flag(s_status_net, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_status_net, LV_SYMBOL_WIFI);
        break;
    case 0:
        lv_obj_remove_flag(s_status_net, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_status_net, LV_SYMBOL_SETTINGS);
        break;
    default:
        lv_obj_add_flag(s_status_net, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

void ui_music_status_set_net(int mode) {
    s_net_mode = mode;
    status_net_apply(mode);
}

void ui_music_status_refresh(void) {
    status_net_apply(s_net_mode);
    status_battery_apply(bsp_battery_soc());
}

// 1s 刷新电量(在 LVGL 任务里,无需另加锁)。
static void status_timer_cb(lv_timer_t *timer) {
    (void)timer;
    status_battery_apply(bsp_battery_soc());
}

lv_obj_t *ui_music_screen_create(const char *title) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_MUSIC_BG), 0);

    // 状态栏:左 = 网络图标,右 = 电量百分比。
    s_status_scr = scr;
    s_status_net = lv_label_create(scr);
    lv_obj_set_style_text_font(s_status_net, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status_net, lv_color_hex(UI_MUSIC_MUTED), 0);
    lv_obj_align(s_status_net, LV_ALIGN_TOP_LEFT, 8, 4);
    s_status_bat = lv_label_create(scr);
    lv_obj_set_style_text_font(s_status_bat, &lv_font_montserrat_14, 0);
    lv_obj_align(s_status_bat, LV_ALIGN_TOP_RIGHT, -8, 4);

    // 标题行:小号强调色,顶部状态栏下方。
    lv_obj_t *title_label = lv_label_create(scr);
    lv_obj_set_style_text_font(title_label, s_font, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(UI_MUSIC_ACCENT), 0);
    lv_label_set_text(title_label, title ? title : "");
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 8, 24);

    // 每屏一个状态栏定时器;换屏删除旧屏时一并删除,避免悬空回调。
    if (s_status_timer) lv_timer_delete(s_status_timer);
    s_status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
    ui_music_status_refresh();
    return scr;
}

lv_obj_t *ui_music_row_create(lv_obj_t *parent, int y, int height) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 224, height);
    lv_obj_set_pos(row, 8, y);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_MUSIC_PANEL), 0);
    // 左侧选中指示条:border_side 选左边,border_width 由 set_selected 控制。
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(UI_MUSIC_ACCENT), 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_all(row, 6, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    return row;
}

void ui_music_row_set_selected(lv_obj_t *row, bool selected) {
    lv_obj_set_style_border_width(row, selected ? 4 : 0, 0);
    lv_obj_set_style_bg_color(row,
        lv_color_hex(selected ? UI_MUSIC_ELEVATED : UI_MUSIC_PANEL), 0);
}

lv_obj_t *ui_music_label_create(lv_obj_t *parent, int x, int y, int w,
                                const char *text, bool wrap) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, s_font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_MUSIC_TEXT), 0);
    lv_label_set_text(label, text ? text : "");
    if (w > 0) lv_obj_set_width(label, w);
    lv_label_set_long_mode(label,
        wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
    lv_obj_set_pos(label, x, y);
    return label;
}

// ---- S0 启动页 -----------------------------------------------------------

lv_obj_t *ui_music_boot_screen_create(const char *app, const char *sub) {
    lv_obj_t *scr = ui_music_screen_create(NULL);
    lv_obj_t *name = lv_label_create(scr);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(UI_MUSIC_ACCENT), 0);
    lv_label_set_text(name, app ? app : "musicdl");
    lv_obj_align(name, LV_ALIGN_CENTER, 0, -24);
    lv_obj_t *s = ui_music_label_create(scr, 0, 0, 0, sub ? sub : "", false);
    lv_obj_set_style_text_color(s, lv_color_hex(UI_MUSIC_MUTED), 0);
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 6);
    return scr;
}

// ---- S1 配网页 -----------------------------------------------------------

lv_obj_t *ui_music_provision_screen_create(const char *ap_name) {
    lv_obj_t *scr = ui_music_screen_create("配网");
    int y = UI_MUSIC_CONTENT_TOP + 4;
    lv_obj_t *l1 = ui_music_label_create(scr, 8, y, 224, "1. 手机连接设备热点:", true);
    (void)l1;
    lv_obj_t *ssid = ui_music_label_create(scr, 24, y + 26, 200, ap_name, false);
    lv_obj_set_style_text_color(ssid, lv_color_hex(UI_MUSIC_ACCENT), 0);
    ui_music_label_create(scr, 8, y + 56, 224, "2. 浏览器打开:", true);
    lv_obj_t *url = ui_music_label_create(scr, 24, y + 82, 200, "http://192.168.4.1", false);
    lv_obj_set_style_text_color(url, lv_color_hex(UI_MUSIC_ACCENT), 0);
    ui_music_label_create(scr, 8, y + 112, 224, "3. 选择家里 Wi-Fi 并输入密码", true);
    lv_obj_t *wait = ui_music_label_create(scr, 8, y + 156, 224,
                                           "提交后设备自动连接,成功即开始播放。", true);
    lv_obj_set_style_text_color(wait, lv_color_hex(UI_MUSIC_MUTED), 0);
    return scr;
}

// ---- 通用 8 行列表页 -----------------------------------------------------

void ui_music_list_screen_build(ui_music_list_t *ui, const char *title) {
    ui->screen = ui_music_screen_create(title);
    ui->page = lv_label_create(ui->screen);
    lv_obj_set_style_text_font(ui->page, s_font, 0);
    lv_obj_set_style_text_color(ui->page, lv_color_hex(UI_MUSIC_MUTED), 0);
    lv_obj_align(ui->page, LV_ALIGN_TOP_RIGHT, -8, 28);

    // 8 行 × 30px,内容区 y=50 起。
    for (uint8_t i = 0; i < UI_MUSIC_LIST_ROWS; i++) {
        const int y = UI_MUSIC_CONTENT_TOP + i * 30;
        ui->rows[i] = ui_music_row_create(ui->screen, y, 28);
        ui->num[i] = ui_music_label_create(ui->rows[i], 2, 2, 20, "", false);
        lv_obj_set_style_text_color(ui->num[i], lv_color_hex(UI_MUSIC_MUTED), 0);
        // 主文本给足宽度,次文本固定右侧 60px。
        ui->main[i] = ui_music_label_create(ui->rows[i], 24, 2, 118, "", false);
        ui->sub[i] = ui_music_label_create(ui->rows[i], 0, 2, 58, "", false);
        lv_obj_set_style_text_color(ui->sub[i], lv_color_hex(UI_MUSIC_MUTED), 0);
        lv_obj_align(ui->sub[i], LV_ALIGN_RIGHT_MID, -4, 0);
        lv_label_set_long_mode(ui->sub[i], LV_LABEL_LONG_DOT);
    }
    ui->hint = ui_music_label_create(ui->screen, 8, 120, 224, "", true);
    lv_obj_set_style_text_color(ui->hint, lv_color_hex(UI_MUSIC_MUTED), 0);
    lv_obj_add_flag(ui->hint, LV_OBJ_FLAG_HIDDEN);
}

void ui_music_list_row_set(ui_music_list_t *ui, uint8_t row, const char *num_text,
                           const char *main_text, const char *sub_text, bool selected) {
    if (row >= UI_MUSIC_LIST_ROWS) return;
    lv_label_set_text(ui->num[row], num_text ? num_text : "");
    lv_label_set_text(ui->main[row], main_text ? main_text : "");
    lv_label_set_text(ui->sub[row], sub_text ? sub_text : "");
    ui_music_row_set_selected(ui->rows[row], selected);
}

void ui_music_list_page_set(ui_music_list_t *ui, uint8_t page, uint8_t pages) {
    lv_label_set_text_fmt(ui->page, "%u/%u", (unsigned)page + 1, (unsigned)pages);
}

void ui_music_list_hint_set(ui_music_list_t *ui, const char *text) {
    if (text && text[0]) {
        lv_label_set_text(ui->hint, text);
        lv_obj_remove_flag(ui->hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui->hint, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---- S4 播放页 -----------------------------------------------------------

// EQ 动画:播放中每 120ms 走一步伪随机高度;暂停时归零。
static ui_music_play_t *s_eq_ui;
static bool s_eq_playing;
static uint8_t s_eq_seed = 7;

static void eq_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (!s_eq_ui || !s_eq_playing) return;
    s_eq_seed = (uint8_t)(s_eq_seed * 31 + 17);
    for (int i = 0; i < 5; i++) {
        s_eq_seed = (uint8_t)(s_eq_seed * 31 + 17);
        const int32_t h = 6 + (s_eq_seed % 22);  // 6..27
        lv_bar_set_value(s_eq_ui->eq[i], h, LV_ANIM_OFF);
    }
}

void ui_music_play_screen_build(ui_music_play_t *ui) {
    ui->screen = ui_music_screen_create("正在播放");
    ui->title = ui_music_label_create(ui->screen, 8, UI_MUSIC_CONTENT_TOP + 6, 224, "", false);
    ui->artist = ui_music_label_create(ui->screen, 8, UI_MUSIC_CONTENT_TOP + 34, 224, "", false);
    lv_obj_set_style_text_color(ui->artist, lv_color_hex(UI_MUSIC_MUTED), 0);
    ui->state = ui_music_label_create(ui->screen, 8, 148, 224, "", false);
    lv_obj_set_style_text_color(ui->state, lv_color_hex(UI_MUSIC_ACCENT), 0);

    // 进度条 + 两侧时长。
    ui->bar = lv_bar_create(ui->screen);
    lv_obj_set_size(ui->bar, 168, 6);
    lv_obj_align(ui->bar, LV_ALIGN_LEFT_MID, 8, 42);
    lv_bar_set_range(ui->bar, 0, 100);
    lv_bar_set_value(ui->bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui->bar, lv_color_hex(UI_MUSIC_ELEVATED), 0);
    lv_obj_set_style_bg_color(ui->bar, lv_color_hex(UI_MUSIC_ACCENT), LV_PART_INDICATOR);
    ui->time_cur = ui_music_label_create(ui->screen, 0, 0, 40, "0:00", false);
    lv_obj_set_style_text_color(ui->time_cur, lv_color_hex(UI_MUSIC_MUTED), 0);
    lv_obj_align(ui->time_cur, LV_ALIGN_BOTTOM_LEFT, 8, -84);
    ui->time_total = ui_music_label_create(ui->screen, 0, 0, 40, "0:00", false);
    lv_obj_set_style_text_color(ui->time_total, lv_color_hex(UI_MUSIC_MUTED), 0);
    lv_obj_align(ui->time_total, LV_ALIGN_BOTTOM_RIGHT, -8, -84);

    // 五柱 EQ:8px 宽,纵向,底部一组。
    for (int i = 0; i < 5; i++) {
        ui->eq[i] = lv_bar_create(ui->screen);
        lv_obj_set_size(ui->eq[i], 10, 36);
        lv_obj_set_pos(ui->eq[i], 68 + i * 22, 246);
        lv_bar_set_range(ui->eq[i], 0, 32);
        lv_bar_set_value(ui->eq[i], 2, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(ui->eq[i], lv_color_hex(UI_MUSIC_PANEL), 0);
        lv_obj_set_style_bg_color(ui->eq[i], lv_color_hex(UI_MUSIC_ACCENT_DIM),
                                  LV_PART_INDICATOR);
        lv_obj_set_style_radius(ui->eq[i], 2, 0);
    }
    // 音量条:右下角瞬时显示。
    ui->vol = lv_bar_create(ui->screen);
    lv_obj_set_size(ui->vol, 80, 6);
    lv_obj_align(ui->vol, LV_ALIGN_BOTTOM_RIGHT, -8, -44);
    lv_bar_set_range(ui->vol, 0, 100);
    lv_obj_set_style_bg_color(ui->vol, lv_color_hex(UI_MUSIC_ELEVATED), 0);
    lv_obj_set_style_bg_color(ui->vol, lv_color_hex(UI_MUSIC_WARN), LV_PART_INDICATOR);

    s_eq_ui = ui;
    static lv_timer_t *eq_timer;
    if (!eq_timer) eq_timer = lv_timer_create(eq_timer_cb, 120, NULL);
}

void ui_music_play_song_set(ui_music_play_t *ui, const music_song_t *song) {
    lv_label_set_text(ui->title, song ? song->title : "未在播放");
    lv_label_set_text(ui->artist, song ? song->artist : "");
    if (song && song->duration_s) {
        lv_label_set_text_fmt(ui->time_total, "%u:%02u",
                              (unsigned)(song->duration_s / 60), (unsigned)(song->duration_s % 60));
    } else {
        lv_label_set_text(ui->time_total, "-:--");
    }
    lv_bar_set_value(ui->bar, 0, LV_ANIM_OFF);
    lv_label_set_text(ui->time_cur, "0:00");
}

void ui_music_play_state_set(ui_music_play_t *ui, const char *text) {
    lv_label_set_text(ui->state, text ? text : "");
}

void ui_music_play_progress_set(ui_music_play_t *ui, uint8_t pct,
                                uint32_t cur_s, uint32_t total_s) {
    lv_bar_set_value(ui->bar, pct, LV_ANIM_OFF);
    lv_label_set_text_fmt(ui->time_cur, "%lu:%02lu",
                          (unsigned long)(cur_s / 60), (unsigned long)(cur_s % 60));
    if (total_s) {
        lv_label_set_text_fmt(ui->time_total, "%lu:%02lu",
                              (unsigned long)(total_s / 60), (unsigned long)(total_s % 60));
    }
}

void ui_music_play_volume_set(ui_music_play_t *ui, uint8_t pct) {
    lv_bar_set_value(ui->vol, pct, LV_ANIM_OFF);
}

void ui_music_play_eq_set(ui_music_play_t *ui, bool playing) {
    s_eq_playing = playing;
    if (!playing) {
        for (int i = 0; i < 5; i++) lv_bar_set_value(ui->eq[i], 2, LV_ANIM_OFF);
    }
}
