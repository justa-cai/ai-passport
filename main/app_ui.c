#include "app_ui.h"

#include <stdio.h>

#include "app_fonts.h"
#include "bsp_battery.h"

/* 电池徽标：右上角纯图形（本体 + 电量填充 + 电极帽），不放数字——
 * 填充比例已表达电量。徽标 timer 挂在当前屏幕上（LVGL 任务内运行），
 * 切屏时随屏销毁前删除。*/
typedef struct {
    lv_obj_t *fill;  /* 电量比例填充 */
    lv_timer_t *timer;
} battery_badge_t;

static battery_badge_t s_battery;

static void battery_refresh(void)
{
    int soc = bsp_battery_soc();
    if (soc < 0) {
        lv_obj_set_size(s_battery.fill, 0, 0);
        return;
    }
    /* 填充宽度 2..20（本体 22 宽，内缩 2px）*/
    lv_obj_set_size(s_battery.fill, 2 + (soc * 18) / 100, 6);
}

static void battery_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    battery_refresh();
}

static void battery_badge_create(lv_obj_t *parent)
{
    /* 徽标区：x198..224, y12..23 —— 位于 30px 圆角安全区内 */
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_set_pos(body, 198, 12);
    lv_obj_set_size(body, 22, 11);
    lv_obj_set_style_bg_color(body, lv_color_hex(PY_CARD), 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_border_color(body, lv_color_hex(PY_SUB), 0);
    lv_obj_set_style_radius(body, 3, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cap = lv_obj_create(parent);
    lv_obj_set_size(cap, 2, 5);
    lv_obj_set_pos(cap, 221, 15);
    lv_obj_set_style_bg_color(cap, lv_color_hex(PY_SUB), 0);
    lv_obj_set_style_border_width(cap, 0, 0);
    lv_obj_set_style_radius(cap, 1, 0);
    lv_obj_set_style_pad_all(cap, 0, 0);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_SCROLLABLE);

    s_battery.fill = lv_obj_create(parent);
    lv_obj_set_size(s_battery.fill, 12, 6);
    lv_obj_set_pos(s_battery.fill, 200, 14);
    lv_obj_set_style_bg_color(s_battery.fill, lv_color_hex(PY_GREEN), 0);
    lv_obj_set_style_border_width(s_battery.fill, 0, 0);
    lv_obj_set_style_radius(s_battery.fill, 1, 0);
    lv_obj_set_style_pad_all(s_battery.fill, 0, 0);
    lv_obj_clear_flag(s_battery.fill, LV_OBJ_FLAG_SCROLLABLE);

    s_battery.timer = lv_timer_create(battery_timer_cb, 5000, NULL);
    battery_refresh();
}

lv_obj_t *app_screen_create(const char *title)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(PY_BG), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    lv_obj_t *title_label = lv_label_create(scr);
    lv_obj_set_style_text_font(title_label, &g_font_ui, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(PY_INK), 0);
    lv_label_set_text(title_label, title);
    lv_obj_set_pos(title_label, 12, 8);

    battery_badge_create(scr);
    return scr;
}

void app_screen_destroy(lv_obj_t *screen)
{
    if (s_battery.timer != NULL) {
        lv_timer_del(s_battery.timer);
        s_battery.timer = NULL;
    }
    if (screen != NULL) {
        lv_obj_delete(screen);
    }
}

lv_obj_t *app_panel_create(lv_obj_t *parent, int x, int y, int w, int h,
                           uint32_t bg, int radius)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, lv_color_hex(bg), 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, radius, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(panel, 0, 0);
    return panel;
}

lv_obj_t *app_label_create(lv_obj_t *parent, const lv_font_t *font,
                           uint32_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t *app_badge_create(lv_obj_t *parent, int x, int y, int d,
                           uint32_t bg, const lv_font_t *font,
                           uint32_t fg, const char *text)
{
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_set_pos(badge, x, y);
    lv_obj_set_size(badge, d, d);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(bg), 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = app_label_create(badge, font, fg, text);
    lv_obj_center(label);
    return badge;
}

lv_obj_t *app_badge_label(lv_obj_t *badge)
{
    return lv_obj_get_child(badge, 0);
}

/* ---- 轻动效 ---- */

static void slide_x_cb(void *obj, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)obj, v);
}

void app_anim_slide_x(lv_obj_t *obj, int32_t to_x, uint16_t ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, slide_x_cb);
    lv_anim_set_values(&a, lv_obj_get_x(obj), to_x);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void bounce_cb(void *obj, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)obj, v);
}

/* 弹跳反馈用 y 轴位移实现——绝不用 transform_scale：宽对象（240px 标签）
 * 的缩放渲染需要 ~83KB 绘制层缓冲，反复播放碎片化堆后分配失败，
 * SW 绘制分发会无限重试并饿死看门狗（taskLVGL 自旋事故）。*/
void app_anim_pop(lv_obj_t *obj)
{
    int32_t y0 = lv_obj_get_y(obj);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, bounce_cb);
    lv_anim_set_values(&a, y0, y0 - 6);
    lv_anim_set_duration(&a, 110);
    lv_anim_set_playback_duration(&a, 110);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}
