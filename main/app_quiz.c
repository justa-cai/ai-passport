/* 闯关测验：10 题一轮（选声母/选韵母/辨声调/整体认读），计分与星级。*/
#include <stdio.h>

#include "app.h"
#include "app_fonts.h"
#include "app_progress.h"
#include "app_screens.h"
#include "app_ui.h"
#include "audio_player.h"
#include "esp_random.h"
#include "lvgl.h"
#include "pinyin_blend.h"
#include "quiz_engine.h"

#define ROUND_QUESTIONS 10
#define MAX_MASK_LESSON 13

typedef enum {
    QZ_SHOW = 0, /* 答题中 */
    QZ_FEEDBACK, /* 已作答，等待自动下一题 */
    QZ_RESULT,   /* 结果页 */
} qz_state_t;

static lv_obj_t *s_scr;
static qz_state_t s_state;
static py_quiz_rng_t s_rng;
static py_question_t s_q;
static uint8_t s_index;      /* 0..9 */
static uint8_t s_correct_count;
static uint8_t s_selected;   /* 当前高亮选项 0..2 */
static lv_timer_t *s_timer;  /* 反馈后自动下一题 */

static lv_obj_t *s_progress_label;
static lv_obj_t *s_dots[ROUND_QUESTIONS]; /* 进度点 */
static lv_obj_t *s_prompt;   /* 题型指令 */
static lv_obj_t *s_hanzi;    /* 题面汉字 */
static lv_obj_t *s_pinyin;   /* 题面拼音 */
static lv_obj_t *s_rows[3];
static lv_obj_t *s_row_labels[3];
static lv_obj_t *s_row_badges[3];

/* 结果页控件（复用同一屏幕重建）*/
static void build_quiz_ui(void);
static void build_result_ui(uint8_t stars);

static void question_audio(void)
{
    if (!audio_player_speech_ok()) {
        return;
    }
    char aid[16];
    if (s_q.type == PY_Q_WHOLE_PICK) {
        /* 逐个读选项：正确项即目标词 */
        py_aid_word(py_word(s_q.word_index), aid, sizeof(aid));
        audio_player_play_clip(aid);
        return;
    }
    py_aid_word(py_word(s_q.word_index), aid, sizeof(aid));
    audio_player_play_clip(aid);
}

static void show_question(void)
{
    s_state = QZ_SHOW;
    s_selected = 0;

    char prog[28];
    snprintf(prog, sizeof(prog), "闯关测验 %d/%d", s_index + 1, ROUND_QUESTIONS);
    lv_label_set_text(s_progress_label, prog);

    switch (s_q.type) {
    case PY_Q_PICK_INITIAL:
        lv_label_set_text(s_prompt, "选出这个音节的声母");
        lv_label_set_text(s_hanzi, s_q.prompt_hanzi);
        lv_label_set_text(s_pinyin, s_q.prompt_pinyin);
        break;
    case PY_Q_PICK_FINAL:
        lv_label_set_text(s_prompt, "选出这个音节的韵母");
        lv_label_set_text(s_hanzi, s_q.prompt_hanzi);
        lv_label_set_text(s_pinyin, s_q.prompt_pinyin);
        break;
    case PY_Q_TONE:
        lv_label_set_text(s_prompt, "听一听，选出声调");
        lv_label_set_text(s_hanzi, s_q.prompt_hanzi);
        lv_label_set_text(s_pinyin, s_q.prompt_pinyin);
        break;
    case PY_Q_WHOLE_PICK:
        lv_label_set_text(s_prompt, "哪个是整体认读音节？");
        lv_label_set_text(s_hanzi, "");
        lv_label_set_text(s_pinyin, "");
        break;
    }

    char row[24];
    for (int i = 0; i < 3; i++) {
        snprintf(row, sizeof(row), "%s", s_q.options[i]);
        lv_label_set_text(s_row_labels[i], row);
        lv_obj_set_style_bg_color(s_rows[i], lv_color_hex(PY_CARD), 0);
        lv_obj_set_style_border_color(s_rows[i], lv_color_hex(PY_LINE), 0);
        lv_obj_set_style_bg_color(s_row_badges[i], lv_color_hex(PY_SLOT), 0);
        lv_obj_set_style_text_color(app_badge_label(s_row_badges[i]),
                                    lv_color_hex(PY_SUB), 0);
    }
    lv_obj_set_style_border_color(s_rows[0], lv_color_hex(PY_BLUE), 0);

    /* 进度点：已答=琥珀 当前=天蓝 未答=发丝线 */
    for (int i = 0; i < ROUND_QUESTIONS; i++) {
        lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(
            i < s_index ? PY_AMBER : (i == s_index ? PY_BLUE : PY_LINE)), 0);
    }

    question_audio();
}

static void next_question(void)
{
    if (s_index + 1 >= ROUND_QUESTIONS || !py_quiz_next(&s_rng, &s_q)) {
        /* 结束 → 结果页 */
        pinyin_progress_t *p = pinyin_progress();
        p->quiz_attempts += ROUND_QUESTIONS;
        p->quiz_correct += s_correct_count;
        uint8_t stars = py_quiz_round_stars(s_correct_count);
        uint8_t pct = (uint8_t)((s_correct_count * 100) / ROUND_QUESTIONS);
        uint8_t lesson = 1; /* 结果不绑定课；写入当前课最佳 */
        if (p->lesson_best[lesson - 1] < pct) {
            p->lesson_best[lesson - 1] = pct;
        }
        pinyin_progress_commit();
        build_result_ui(stars);
        if (stars >= 2) {
            audio_player_play_fx(FX_STAR);
        }
        return;
    }
    s_index++;
    show_question();
}

static void timer_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);
    s_timer = NULL;
    next_question();
}

static void apply_answer(uint8_t chosen)
{
    s_state = QZ_FEEDBACK;
    bool right = py_quiz_check(&s_q, chosen);

    /* 掌握度：目标条目 ±1 星 */
    if (s_q.item_index >= 0 && (uint16_t)s_q.item_index < PY_N_ITEMS) {
        pinyin_progress_t *p = pinyin_progress();
        uint8_t *star = &p->stars[s_q.item_index];
        *star = (uint8_t)(*star + (right ? 1 : -1));
        if (*star > 3) {
            *star = 3;
        }
        pinyin_progress_commit();
    }

    lv_obj_set_style_bg_color(s_rows[s_q.correct],
        lv_color_hex(PY_WASH_GREEN), 0);
    lv_obj_set_style_border_color(s_rows[s_q.correct], lv_color_hex(PY_GREEN), 0);
    lv_obj_set_style_bg_color(s_row_badges[s_q.correct], lv_color_hex(PY_GREEN), 0);
    lv_obj_set_style_text_color(app_badge_label(s_row_badges[s_q.correct]),
        lv_color_hex(PY_CARD), 0);
    app_anim_pop(s_row_badges[s_q.correct]);
    if (!right) {
        lv_obj_set_style_bg_color(s_rows[chosen], lv_color_hex(PY_WASH_RED), 0);
        lv_obj_set_style_border_color(s_rows[chosen], lv_color_hex(PY_WRONG), 0);
        lv_obj_set_style_bg_color(s_row_badges[chosen], lv_color_hex(PY_WRONG), 0);
        lv_obj_set_style_text_color(app_badge_label(s_row_badges[chosen]),
            lv_color_hex(PY_CARD), 0);
    }
    if (right) {
        s_correct_count++;
        audio_player_play_fx(FX_CORRECT);
    } else {
        audio_player_play_fx(FX_WRONG);
    }
    s_timer = lv_timer_create(timer_cb, 900, NULL);
}

static void build_quiz_ui(void)
{
    s_scr = app_screen_create("");

    s_progress_label = app_label_create(s_scr, &g_font_ui, PY_INK, "");
    lv_obj_set_pos(s_progress_label, 12, 8);

    for (int i = 0; i < ROUND_QUESTIONS; i++) {
        s_dots[i] = lv_obj_create(s_scr);
        lv_obj_set_pos(s_dots[i], 108 + i * 12, 16);
        lv_obj_set_size(s_dots[i], 8, 8);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(PY_LINE), 0);
        lv_obj_set_style_border_width(s_dots[i], 0, 0);
        lv_obj_set_style_pad_all(s_dots[i], 0, 0);
        lv_obj_clear_flag(s_dots[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    s_prompt = app_label_create(s_scr, &g_font_small, PY_RED, "");
    lv_obj_set_style_text_align(s_prompt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_prompt, 240);
    lv_obj_set_pos(s_prompt, 0, 40);

    s_hanzi = app_label_create(s_scr, &g_font_hanzi, PY_INK, "");
    lv_obj_set_style_text_align(s_hanzi, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_hanzi, 240);
    lv_obj_set_pos(s_hanzi, 0, 64);

    s_pinyin = app_label_create(s_scr, &g_font_ui, PY_SUB, "");
    lv_obj_set_style_text_align(s_pinyin, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_pinyin, 240);
    lv_obj_set_pos(s_pinyin, 0, 122);

    static const char letters[3] = {'A', 'B', 'C'};
    for (int i = 0; i < 3; i++) {
        s_rows[i] = app_panel_create(s_scr, 20, 156 + i * 50, 200, 44, PY_CARD, 10);
        lv_obj_set_style_border_width(s_rows[i], 2, 0);
        lv_obj_set_style_border_color(s_rows[i], lv_color_hex(PY_LINE), 0);
        char l[2] = {letters[i], '\0'};
        s_row_badges[i] = app_badge_create(s_rows[i], 10, 11, 22, PY_SLOT,
                                           &g_font_ui, PY_SUB, l);
        s_row_labels[i] = app_label_create(s_rows[i], &g_font_ui, PY_INK, "");
        lv_obj_align(s_row_labels[i], LV_ALIGN_LEFT_MID, 44, 0);
    }
}

static void build_result_ui(uint8_t stars)
{
    /* 结果页重建在当前屏幕上：先清空题面控件（由屏幕统一销毁前手动删除）*/
    app_screen_destroy(s_scr);
    s_scr = app_screen_create("闯关测验 · 完成");

    char score[24];
    snprintf(score, sizeof(score), "得分 %d/%d", s_correct_count, ROUND_QUESTIONS);
    lv_obj_t *score_label = app_label_create(s_scr, &g_font_ui, PY_INK, score);
    lv_obj_set_style_text_align(score_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(score_label, 240);
    lv_obj_set_pos(score_label, 0, 84);

    char star_text[8];
    for (int i = 0; i < 3; i++) {
        star_text[i] = (uint8_t)i < stars ? 1 : 0;
    }
    /* ★=U+2605 ☆=U+2606，手工拼 UTF-8 */
    char stars_buf[16] = "";
    char *p = stars_buf;
    for (int i = 0; i < 3; i++) {
        if ((uint8_t)i < stars) {
            *p++ = (char)0xE2;
            *p++ = (char)0x98;
            *p++ = (char)0x85;
        } else {
            *p++ = (char)0xE2;
            *p++ = (char)0x98;
            *p++ = (char)0x86;
        }
    }
    *p = '\0';
    lv_obj_t *stars_label = app_label_create(s_scr, &g_font_hanzi, PY_AMBER, stars_buf);
    lv_obj_set_style_text_align(stars_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(stars_label, 240);
    lv_obj_set_pos(stars_label, 0, 130);
    app_anim_pop(stars_label); /* 得星反馈 */

    lv_obj_t *hint = app_label_create(s_scr, &g_font_small, PY_SUB,
                                      "OK 再来一轮 · 长按返回");
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hint, 240);
    lv_obj_set_pos(hint, 0, 250);
    (void)star_text;

    lv_screen_load(s_scr);
    s_state = QZ_RESULT;
}

void app_quiz_enter(void)
{
    build_quiz_ui();

    pinyin_progress_t *p = pinyin_progress();
    uint8_t max_lesson = p->last_lesson;
    if (max_lesson < 3) {
        max_lesson = 3; /* 首次至少覆盖前 3 课词池 */
    }
    if (max_lesson > MAX_MASK_LESSON) {
        max_lesson = MAX_MASK_LESSON;
    }
    s_rng.seed = esp_random();
    s_rng.lesson_mask = (uint16_t)((1u << max_lesson) - 1);
    s_rng.recent_len = 0;

    s_index = 0;
    s_correct_count = 0;
    if (!py_quiz_next(&s_rng, &s_q)) {
        /* 词池异常兜底：全范围 */
        s_rng.lesson_mask = 0x1FFF;
        py_quiz_next(&s_rng, &s_q);
    }
    show_question();
    lv_screen_load(s_scr);
}

void app_quiz_exit(void)
{
    if (s_timer != NULL) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    app_screen_destroy(s_scr);
    s_scr = NULL;
}

bool app_quiz_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (s_state == QZ_RESULT) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            audio_player_play_fx(FX_PAGE);
            app_quiz_exit();
            app_quiz_enter();
            return true;
        }
        return false;
    }
    if (btn == BSP_BTN_OK && ev == BSP_BTN_DOUBLE) {
        audio_player_replay();
        return true;
    }
    if (ev != BSP_BTN_CLICK || s_state != QZ_SHOW) {
        return false;
    }
    switch (btn) {
    case BSP_BTN_UP:
        s_selected = (s_selected + 2) % 3;
        break;
    case BSP_BTN_DOWN:
        s_selected = (s_selected + 1) % 3;
        break;
    case BSP_BTN_OK:
        apply_answer(s_selected);
        return true;
    default:
        return false;
    }
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_border_color(s_rows[i],
            lv_color_hex(i == s_selected ? PY_BLUE : PY_LINE), 0);
    }
    return true;
}
