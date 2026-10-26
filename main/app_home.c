/* 首页：双模式入口大卡（琥珀=卡片浏览、天蓝=闯关测验）+ 掌握星汇总。
 * 选中态：卡片右移 + 主题色淡底 + 徽章反色，附滑动动画。*/
#include <stdio.h>

#include "app.h"
#include "app_fonts.h"
#include "app_progress.h"
#include "app_screens.h"
#include "app_ui.h"
#include "audio_player.h"
#include "lvgl.h"

#define MODE_COUNT 2

static lv_obj_t *s_scr;
static int8_t s_sel;          /* 0 卡片浏览 1 闯关测验 */
static lv_obj_t *s_cards[MODE_COUNT];
static lv_obj_t *s_badges[MODE_COUNT];
static lv_obj_t *s_subtitles[MODE_COUNT];
static lv_obj_t *s_star_summary;

static const char *k_titles[MODE_COUNT] = {"卡片浏览", "闯关测验"};
static const char *k_subs[MODE_COUNT] = {
    "13 课 · 课文同步",
    "听一听 · 选一选",
};
/* 模式主题色：卡片浏览=琥珀 测验=天蓝 */
static const uint32_t k_mode_colors[MODE_COUNT] = {PY_AMBER, PY_BLUE};
static const uint32_t k_mode_washes[MODE_COUNT] = {PY_WASH_AMBER, PY_WASH_BLUE};

static uint16_t star_total(void)
{
    uint16_t n = 0;
    const pinyin_progress_t *p = pinyin_progress();
    for (int i = 0; i < PY_N_ITEMS; i++) {
        n += p->stars[i] >= 3 ? 1 : 0;
    }
    return n;
}

static void refresh_selection(void)
{
    char sub[40];
    for (int i = 0; i < MODE_COUNT; i++) {
        bool sel = i == s_sel;
        /* 边框宽度恒定 3px：border 画在内侧，变宽会挤压内容区裁字 */
        lv_obj_set_style_border_color(s_cards[i],
            lv_color_hex(k_mode_colors[i]), 0);
        lv_obj_set_style_bg_color(s_cards[i],
            lv_color_hex(sel ? k_mode_washes[i] : PY_CARD), 0);
        lv_obj_t *lab = app_badge_label(s_badges[i]);
        lv_obj_set_style_bg_color(s_badges[i],
            lv_color_hex(sel ? PY_CARD : k_mode_colors[i]), 0);
        lv_obj_set_style_text_color(lab,
            lv_color_hex(sel ? k_mode_colors[i] : PY_CARD), 0);
        app_anim_slide_x(s_cards[i], sel ? 22 : 12, 150);
    }
    if (s_sel == 0) {
        snprintf(sub, sizeof(sub), "13 课 · 上次第%d课",
                 pinyin_progress()->last_lesson);
        lv_label_set_text(s_subtitles[0], sub);
    }
    char stars[24];
    snprintf(stars, sizeof(stars), "\xe2\x98\x85 %u/63", star_total());
    lv_label_set_text(s_star_summary, stars);
}

void app_home_enter(void)
{
    s_scr = app_screen_create("拼音小课堂");

    static const char *k_nums[MODE_COUNT] = {"1", "2"};
    /* 双卡垂直居中：卡 64 高 + 间距 12，整体落在 y88..164 */
    static const int k_card_y[MODE_COUNT] = {88, 164};
    for (int i = 0; i < MODE_COUNT; i++) {
        s_cards[i] = app_panel_create(s_scr, 12, k_card_y[i], 208, 64,
                                      PY_CARD, 16);
        lv_obj_set_style_border_width(s_cards[i], 3, 0);
        lv_obj_set_style_border_color(s_cards[i],
            lv_color_hex(k_mode_colors[i]), 0);

        s_badges[i] = app_badge_create(s_cards[i], 10, 18, 28,
                                       k_mode_colors[i], &g_font_ui,
                                       PY_CARD, k_nums[i]);

        lv_obj_t *title = app_label_create(s_cards[i], &g_font_ui, PY_INK,
                                           k_titles[i]);
        lv_obj_set_pos(title, 48, 8);
        s_subtitles[i] = app_label_create(s_cards[i], &g_font_small, PY_SUB,
                                          k_subs[i]);
        lv_obj_set_pos(s_subtitles[i], 48, 38);
    }

    s_star_summary = app_label_create(s_scr, &g_font_ui, PY_AMBER, "");
    lv_obj_set_style_text_align(s_star_summary, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_star_summary, 240);
    lv_obj_set_pos(s_star_summary, 0, 292);

    s_sel = 0;
    refresh_selection();
    lv_screen_load(s_scr);
}

void app_home_exit(void)
{
    app_screen_destroy(s_scr);
    s_scr = NULL;
}

bool app_home_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK && ev != BSP_BTN_DOUBLE) {
        return false;
    }
    if (btn == BSP_BTN_UP && ev == BSP_BTN_CLICK) {
        s_sel = (int8_t)((s_sel + 1) % MODE_COUNT);
        audio_player_play_fx(FX_PAGE);
    } else if (btn == BSP_BTN_DOWN && ev == BSP_BTN_CLICK) {
        s_sel = (int8_t)((s_sel + 1) % MODE_COUNT);
        audio_player_play_fx(FX_PAGE);
    } else if (btn == BSP_BTN_OK) {
        audio_player_play_fx(FX_PAGE);
        switch (s_sel) {
        case 0: app_nav_goto(APP_SCR_LESSONS); break;
        case 1: app_nav_goto(APP_SCR_QUIZ); break;
        default: break;
        }
        return true;
    }
    refresh_selection();
    return false;
}
