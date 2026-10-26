/* 闯关测验引擎：确定性出题（同种子同题序）、干扰项、判分。
 * 纯逻辑模块：LCG 随机源内建，无外部依赖，主机可测。
 */
#ifndef QUIZ_ENGINE_H
#define QUIZ_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#include "pinyin_data.h"

#define PY_Q_OPTIONS 3
#define PY_Q_TEXT 16

typedef enum {
    PY_Q_PICK_INITIAL = 0, /* 给汉字音节，选出声母 */
    PY_Q_PICK_FINAL,       /* 给汉字音节，选出韵母 */
    PY_Q_TONE,             /* 给汉字与发音，选出声调（选项为带调形式）*/
    PY_Q_WHOLE_PICK,       /* 三个音节中选出整体认读音节 */
} py_q_type_t;

typedef struct {
    py_q_type_t type;
    uint16_t word_index;             /* 目标例词（PY_Q_WHOLE_PICK 为正确项所在词）*/
    char prompt_hanzi[8];            /* 题面汉字（WHOLE_PICK 为空串）*/
    char prompt_pinyin[PY_Q_TEXT];   /* 题面拼音（TONE 题为不带调基础式）*/
    char options[PY_Q_OPTIONS][PY_Q_TEXT];
    uint8_t correct;                 /* 0..PY_Q_OPTIONS-1 */
    /* 掌握度关联条目（答题后 stars[item_index] ±1）*/
    int16_t item_index;
} py_question_t;

typedef struct {
    uint32_t seed;         /* LCG 状态 */
    uint16_t lesson_mask;  /* bit0=第1课 … bit12=第13课；参与课次允许 */
    uint16_t recent[6];    /* 最近出过的词下标（避免短期重复）*/
    uint8_t recent_len;
} py_quiz_rng_t;

/* 生成下一题；题池为空（mask 无可用词）返回 false。*/
bool py_quiz_next(py_quiz_rng_t *r, py_question_t *q);
/* 判定作答（chosen 0..2）*/
bool py_quiz_check(const py_question_t *q, uint8_t chosen);
/* 一轮星级：>=9 → 3，>=7 → 2，>=5 → 1，否则 0。*/
uint8_t py_quiz_round_stars(uint8_t correct_count);

#endif /* QUIZ_ENGINE_H */
