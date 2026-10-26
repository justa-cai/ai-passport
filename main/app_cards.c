/* 卡片浏览：选课列表 → 课内卡片（大拼音 + 声调条 + 例词 + 发音）。*/
#include <stdio.h>

#include "app.h"
#include "app_fonts.h"
#include "app_progress.h"
#include "app_screens.h"
#include "app_ui.h"
#include "audio_player.h"
#include "lvgl.h"
#include "pinyin_blend.h"

/* ---- 选课列表 ---- */

static lv_obj_t *s_lesson_scr;
static int8_t s_lesson_sel;
static lv_obj_t *s_lesson_rows[13];
static lv_obj_t *s_lesson_stars[13];

/* ---- 卡片视图 ---- */

static lv_obj_t *s_card_scr;
static uint8_t s_lesson;       /* 当前课 1..13 */
static uint16_t s_card_pos;    /* 课内卡序（声母 1 张，韵母/整体 4 张）*/
static lv_obj_t *s_card_title;
static lv_obj_t *s_kind_chip;
static lv_obj_t *s_big;
static lv_obj_t *s_tones[4];   /* 声调药丸（容器）*/
static lv_obj_t *s_example;
static lv_obj_t *s_hint;
static lv_obj_t *s_example_bar;

/* 课内卡片数 */
static uint16_t deck_size(uint8_t lesson)
{
    uint16_t n = 0;
    int16_t first = py_item_first_in_lesson(lesson);
    if (first < 0) {
        return 0;
    }
    for (uint16_t i = (uint16_t)first; i < py_item_count(); i++) {
        const py_item_t *it = py_item(i);
        if (it->lesson != lesson) {
            break;
        }
        n += (it->kind == PY_K_INITIAL) ? 1 : 4;
    }
    return n;
}

/* 卡序 → (item_index, tone)；tone 0 = 声母（无调）*/
static bool deck_at(uint8_t lesson, uint16_t pos, uint16_t *item_out, uint8_t *tone_out)
{
    int16_t first = py_item_first_in_lesson(lesson);
    if (first < 0) {
        return false;
    }
    uint16_t acc = 0;
    for (uint16_t i = (uint16_t)first; i < py_item_count(); i++) {
        const py_item_t *it = py_item(i);
        if (it->lesson != lesson) {
            break;
        }
        uint16_t span = (it->kind == PY_K_INITIAL) ? 1 : 4;
        if (pos >= acc && pos < acc + span) {
            *item_out = i;
            *tone_out = (it->kind == PY_K_INITIAL) ? 0 : (uint8_t)(pos - acc + 1);
            return true;
        }
        acc += span;
    }
    return false;
}

static const char *kind_text(const py_item_t *it)
{
    switch (it->kind) {
    case PY_K_INITIAL: return "声母";
    case PY_K_FINAL: return "韵母";
    case PY_K_WHOLE: return "整体认读";
    default: return "";
    }
}

/* 分类主题色：声母=琥珀 韵母=天蓝 整体=青绿 */
static uint32_t kind_color(const py_item_t *it)
{
    switch (it->kind) {
    case PY_K_INITIAL: return PY_AMBER;
    case PY_K_FINAL: return PY_BLUE;
    case PY_K_WHOLE: return PY_GREEN;
    default: return PY_SUB;
    }
}

static uint32_t kind_wash(const py_item_t *it)
{
    switch (it->kind) {
    case PY_K_INITIAL: return PY_WASH_AMBER;
    case PY_K_FINAL: return PY_WASH_BLUE;
    case PY_K_WHOLE: return PY_WASH_GREEN;
    default: return PY_SLOT;
    }
}

/* 某课已得星数（Σ stars，用于选课行右侧）*/
static uint16_t lesson_star_sum(uint8_t lesson)
{
    int16_t first = py_item_first_in_lesson(lesson);
    if (first < 0) {
        return 0;
    }
    uint16_t sum = 0;
    for (uint16_t i = (uint16_t)first; i < py_item_count(); i++) {
        const py_item_t *it = py_item(i);
        if (it->lesson != lesson) {
            break;
        }
        sum += pinyin_progress()->stars[i];
    }
    return sum;
}

/* 例词：优先取同声调的例词 */
static const py_word_t *example_for(uint16_t item_index, uint8_t tone)
{
    uint16_t idx[8];
    uint8_t n = py_words_for_item(item_index, idx, 8);
    if (n == 0) {
        return NULL;
    }
    const py_word_t *fallback = py_word(idx[0]);
    for (uint8_t i = 0; i < n; i++) {
        const py_word_t *w = py_word(idx[i]);
        if (tone == 0 || w->tone == tone) {
            return w;
        }
    }
    return fallback;
}

static void card_audio_aid(char *aid, size_t cap)
{
    uint16_t item;
    uint8_t tone;
    if (!deck_at(s_lesson, s_card_pos, &item, &tone)) {
        aid[0] = '\0';
        return;
    }
    const py_item_t *it = py_item(item);
    if (it->kind == PY_K_INITIAL) {
        py_aid_initial(it->initial_id, aid, cap);
    } else if (it->kind == PY_K_WHOLE) {
        py_aid_whole(it->whole_id, tone, aid, cap);
    } else {
        py_aid_final(it->final_id, tone, aid, cap);
    }
}

static void card_refresh(void)
{
    uint16_t item;
    uint8_t tone;
    if (!deck_at(s_lesson, s_card_pos, &item, &tone)) {
        return;
    }
    const py_item_t *it = py_item(item);

    char title[32];
    snprintf(title, sizeof(title), "第%d课 · %u/%u", s_lesson,
             (unsigned)(s_card_pos + 1), (unsigned)deck_size(s_lesson));
    lv_label_set_text(s_card_title, title);

    lv_label_set_text(s_kind_chip, kind_text(it));
    lv_obj_set_style_text_color(s_kind_chip, lv_color_hex(kind_color(it)), 0);
    lv_obj_set_style_bg_color(s_kind_chip, lv_color_hex(kind_wash(it)), 0);
    lv_obj_set_style_bg_color(s_example_bar, lv_color_hex(kind_color(it)), 0);

    char buf[PY_BUF];
    if (it->kind == PY_K_INITIAL) {
        lv_label_set_text(s_big, py_initials[it->initial_id]);
        /* 声母卡：声调条位置显示呼读音（加宽第一个药丸，其余隐去）*/
        char call[PY_BUF];
        snprintf(call, sizeof(call), "呼读 %s", py_initial_calls[it->initial_id]);
        for (int i = 0; i < 4; i++) {
            lv_obj_t *lab = lv_obj_get_child(s_tones[i], 0);
            lv_obj_set_style_bg_color(s_tones[i], lv_color_hex(PY_BG), 0);
            lv_label_set_text(lab, "");
        }
        lv_obj_set_width(s_tones[0], 168);
        lv_obj_set_style_bg_color(s_tones[0], lv_color_hex(PY_WASH_RED), 0);
        lv_label_set_text(lv_obj_get_child(s_tones[0], 0), call);
        lv_obj_set_style_text_color(lv_obj_get_child(s_tones[0], 0),
                                    lv_color_hex(PY_RED), 0);
    } else {
        if (it->kind == PY_K_WHOLE) {
            py_compose_whole(it->whole_id, tone, buf, sizeof(buf));
        } else {
            py_compose_final_marked(it->final_id, tone, buf, sizeof(buf));
        }
        lv_label_set_text(s_big, buf);
        static const char *k_tone_names[4] = {"一声", "二声", "三声", "四声"};
        for (int i = 0; i < 4; i++) {
            lv_obj_t *lab = lv_obj_get_child(s_tones[i], 0);
            lv_obj_set_width(s_tones[i], 48);
            lv_label_set_text(lab, k_tone_names[i]);
            bool active = (uint8_t)(i + 1) == tone;
            lv_obj_set_style_bg_color(s_tones[i],
                lv_color_hex(active ? PY_RED : PY_SLOT), 0);
            lv_obj_set_style_text_color(lab,
                lv_color_hex(active ? PY_CARD : PY_SUB), 0);
        }
    }

    const py_word_t *w = example_for(item, tone);
    if (w != NULL) {
        char ex[32];
        py_compose_word(w, buf, sizeof(buf));
        snprintf(ex, sizeof(ex), "%s %s", buf, w->hanzi);
        lv_label_set_text(s_example, ex);
    } else {
        lv_label_set_text(s_example, "");
    }

    if (audio_player_speech_ok()) {
        lv_label_set_text(s_hint, "OK 播放 · 双击换调 · 长按返回");
    } else {
        lv_label_set_text(s_hint, "双击换调 · 长按返回");
    }
}

/* ---- 选课列表实现 ---- */

static void lessons_refresh(void)
{
    for (int l = 1; l <= 13; l++) {
        lv_obj_set_style_bg_color(s_lesson_rows[l - 1],
            lv_color_hex(l == s_lesson_sel + 1 ? PY_WASH_AMBER : PY_BG), 0);
        lv_obj_set_style_outline_width(s_lesson_rows[l - 1], 0, 0);
    }
    /* 选中态用 outline（画在行外侧）：border 画在内侧会使内容区缩水
     * 22px 行高 − 上下边框，装不下 22px 文字 → 选中行被裁底 */
    lv_obj_set_style_outline_width(s_lesson_rows[s_lesson_sel], 2, 0);
    lv_obj_set_style_outline_color(s_lesson_rows[s_lesson_sel], lv_color_hex(PY_AMBER), 0);
    lv_obj_set_style_outline_pad(s_lesson_rows[s_lesson_sel], 0, 0);
    /* 滚动跟随：13 行内容超出可视区，让选中行滚入视野
     * （scroll_to_view 自带边界与内边距处理；此前误滚屏幕对象是空操作，
     * 第 11 课起永远在折叠线下方）*/
    lv_obj_scroll_to_view(s_lesson_rows[s_lesson_sel], LV_ANIM_OFF);
}

void app_lessons_enter(void)
{
    s_lesson_scr = app_screen_create("卡片浏览 · 选课");
    s_lesson_sel = (int8_t)(pinyin_progress()->last_lesson - 1);
    if (s_lesson_sel < 0 || s_lesson_sel >= 13) {
        s_lesson_sel = 0;
    }

    lv_obj_t *list = lv_obj_create(s_lesson_scr);
    lv_obj_set_pos(list, 12, 44);
    lv_obj_set_size(list, 216, 264);
    lv_obj_set_style_bg_color(list, lv_color_hex(PY_BG), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_style_radius(list, 0, 0);

    for (int l = 1; l <= 13; l++) {
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_set_size(row, 206, 22);
        lv_obj_set_pos(row, 0, (l - 1) * 24);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(PY_BG), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lab = lv_label_create(row);
        lv_obj_set_style_text_font(lab, &g_font_ui, 0);
        lv_obj_set_style_text_color(lab, lv_color_hex(PY_INK), 0);
        lv_label_set_text(lab, py_lesson_title(l));
        lv_obj_set_pos(lab, 8, 0);

        char stars[12];
        snprintf(stars, sizeof(stars), "\xe2\x98\x85 %u",
                 (unsigned)lesson_star_sum((uint8_t)l));
        s_lesson_stars[l - 1] = lv_label_create(row);
        lv_obj_set_style_text_font(s_lesson_stars[l - 1], &g_font_small, 0);
        lv_obj_set_style_text_color(s_lesson_stars[l - 1],
                                    lv_color_hex(PY_AMBER), 0);
        lv_label_set_text(s_lesson_stars[l - 1], stars);
        lv_obj_align(s_lesson_stars[l - 1], LV_ALIGN_RIGHT_MID, -8, 0);

        s_lesson_rows[l - 1] = row;
    }
    (void)list;
    /* 行高 22 + 间距 2，列表容器本身作为滚动区 */
    lv_screen_load(s_lesson_scr);
    /* 刷新须在屏幕加载后：scroll_to_view 依赖已布局的坐标，
     * 提前调用是空操作（续期 last_lesson=13 时首屏看不到选中行）*/
    lessons_refresh();
}

void app_lessons_exit(void)
{
    app_screen_destroy(s_lesson_scr);
    s_lesson_scr = NULL;
}

bool app_lessons_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) {
        return false;
    }
    if (btn == BSP_BTN_UP) {
        s_lesson_sel = (s_lesson_sel + 12) % 13;
        audio_player_play_fx(FX_PAGE);
    } else if (btn == BSP_BTN_DOWN) {
        s_lesson_sel = (s_lesson_sel + 1) % 13;
        audio_player_play_fx(FX_PAGE);
    } else if (btn == BSP_BTN_OK) {
        pinyin_progress()->last_lesson = (uint8_t)(s_lesson_sel + 1);
        pinyin_progress()->last_card = 0;
        pinyin_progress_commit();
        audio_player_play_fx(FX_PAGE);
        app_nav_goto(APP_SCR_CARD);
        return true;
    }
    lessons_refresh();
    return false;
}

/* ---- 卡片视图实现 ---- */

void app_card_enter(void)
{
    s_lesson = pinyin_progress()->last_lesson;
    if (s_lesson < 1 || s_lesson > 13) {
        s_lesson = 1;
    }
    s_card_pos = pinyin_progress()->last_card;
    if (s_card_pos >= deck_size(s_lesson)) {
        s_card_pos = 0;
    }

    s_card_scr = app_screen_create("");

    s_card_title = app_label_create(s_card_scr, &g_font_ui, PY_INK, "");
    lv_obj_set_pos(s_card_title, 12, 8);

    s_kind_chip = lv_label_create(s_card_scr);
    lv_obj_set_style_text_font(s_kind_chip, &g_font_small, 0);
    lv_obj_set_style_text_color(s_kind_chip, lv_color_hex(PY_RED), 0);
    lv_obj_set_style_bg_color(s_kind_chip, lv_color_hex(PY_WASH_RED), 0);
    lv_obj_set_style_bg_opa(s_kind_chip, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_kind_chip, 8, 0);
    lv_obj_set_style_pad_ver(s_kind_chip, 2, 0);
    lv_obj_set_pos(s_kind_chip, 12, 40);

    s_big = app_label_create(s_card_scr, &g_font_big, PY_INK, "");
    lv_obj_set_style_text_align(s_big, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_big, 240);
    lv_obj_set_pos(s_big, 0, 84);

    for (int i = 0; i < 4; i++) {
        /* 声调药丸：容器 + 居中小字（当前调实心红底白字）*/
        s_tones[i] = lv_obj_create(s_card_scr);
        lv_obj_set_pos(s_tones[i], 24 + i * 52, 172);
        lv_obj_set_size(s_tones[i], 48, 28);
        lv_obj_set_style_radius(s_tones[i], 14, 0);
        lv_obj_set_style_bg_color(s_tones[i], lv_color_hex(PY_SLOT), 0);
        lv_obj_set_style_border_width(s_tones[i], 0, 0);
        lv_obj_set_style_pad_all(s_tones[i], 0, 0);
        lv_obj_clear_flag(s_tones[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lab = app_label_create(s_tones[i], &g_font_small, PY_SUB, "");
        lv_obj_center(lab);
    }

    lv_obj_t *panel = app_panel_create(s_card_scr, 20, 210, 200, 56, PY_CARD, 12);
    /* 分类色左侧装饰条 */
    s_example_bar = lv_obj_create(panel);
    lv_obj_set_pos(s_example_bar, 0, 0);
    lv_obj_set_size(s_example_bar, 4, 56);
    lv_obj_set_style_radius(s_example_bar, 0, 0);
    lv_obj_set_style_bg_color(s_example_bar, lv_color_hex(PY_AMBER), 0);
    lv_obj_set_style_border_width(s_example_bar, 0, 0);
    lv_obj_set_style_pad_all(s_example_bar, 0, 0);
    lv_obj_clear_flag(s_example_bar, LV_OBJ_FLAG_SCROLLABLE);
    s_example = app_label_create(panel, &g_font_ui, PY_INK, "");
    lv_obj_set_style_text_align(s_example, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_example, 192);
    lv_obj_align(s_example, LV_ALIGN_CENTER, 4, 0);

    s_hint = app_label_create(s_card_scr, &g_font_small, PY_SUB, "");
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_hint, 240);
    lv_obj_set_pos(s_hint, 0, 292);

    card_refresh();
    lv_screen_load(s_card_scr);

    char aid[16];
    card_audio_aid(aid, sizeof(aid));
    audio_player_play_clip(aid);
}

void app_card_exit(void)
{
    pinyin_progress()->last_lesson = s_lesson;
    pinyin_progress()->last_card = s_card_pos;
    pinyin_progress_commit();
    app_screen_destroy(s_card_scr);
    s_card_scr = NULL;
}

bool app_card_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    uint16_t deck = deck_size(s_lesson);
    if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
        char aid[16];
        card_audio_aid(aid, sizeof(aid));
        audio_player_play_clip(aid);
        app_anim_pop(s_big); /* 播放反馈：大字弹跳 */
        return true;
    }
    if (btn == BSP_BTN_OK && ev == BSP_BTN_DOUBLE) {
        /* 换声调：仅韵母/整体认读卡 */
        uint16_t item;
        uint8_t tone;
        if (deck_at(s_lesson, s_card_pos, &item, &tone) && tone > 0) {
            s_card_pos = (uint16_t)((s_card_pos / 4) * 4 + (tone % 4));
            card_refresh();
            char aid[16];
            card_audio_aid(aid, sizeof(aid));
            audio_player_play_clip(aid);
            app_anim_pop(s_big);
        }
        return true;
    }
    if (ev != BSP_BTN_CLICK) {
        return false;
    }
    if (btn == BSP_BTN_UP) {
        s_card_pos = (s_card_pos + deck - 1) % deck;
        audio_player_play_fx(FX_PAGE);
        card_refresh();
        return true;
    }
    if (btn == BSP_BTN_DOWN) {
        s_card_pos = (s_card_pos + 1) % deck;
        audio_player_play_fx(FX_PAGE);
        card_refresh();
        return true;
    }
    return false;
}
