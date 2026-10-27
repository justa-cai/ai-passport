// main/ui_music.c —— musicdl 播放器视觉外壳实现。
//
// 线程纪律:本模块所有函数都必须在持有 LVGL 锁(或 LVGL 任务内)调用;
// 状态栏电量/网络的对外入口只有 ui_music_status_set_net() 与内部定时器,
// 前者由应用主任务在锁内调用。
#include "ui_music.h"

#include "bsp_battery.h"
#include "esp_log.h"

static const char *TAG = "ui_music";

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
