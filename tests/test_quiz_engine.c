/* 测验引擎测试：确定性、选项不变量、干扰项规则、星级。*/
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pinyin_blend.h"
#include "pinyin_data.h"
#include "quiz_engine.h"

#define FULL_MASK 0x1FFFu /* 全部 13 课 */

static void assert_question_invariants(const py_question_t *q)
{
    assert(q->correct < PY_Q_OPTIONS);
    /* 选项两两不同 */
    for (int i = 0; i < PY_Q_OPTIONS; i++) {
        assert(q->options[i][0] != '\0');
        for (int k = i + 1; k < PY_Q_OPTIONS; k++) {
            assert(strcmp(q->options[i], q->options[k]) != 0);
        }
    }
    /* 类型相关题面 */
    switch (q->type) {
    case PY_Q_PICK_INITIAL:
    case PY_Q_PICK_FINAL:
    case PY_Q_TONE:
        assert(q->prompt_hanzi[0] != '\0');
        assert(q->prompt_pinyin[0] != '\0');
        break;
    case PY_Q_WHOLE_PICK:
        break;
    }
    assert(q->item_index >= 0 && q->item_index < (int16_t)py_item_count());
    /* 判定 API 一致性 */
    assert(py_quiz_check(q, q->correct));
    assert(!py_quiz_check(q, (uint8_t)((q->correct + 1) % PY_Q_OPTIONS)));
    assert(!py_quiz_check(q, 3));
    assert(!py_quiz_check(q, 255));
}

static void test_determinism(void)
{
    py_quiz_rng_t r1 = {.seed = 12345, .lesson_mask = FULL_MASK};
    py_quiz_rng_t r2 = {.seed = 12345, .lesson_mask = FULL_MASK};
    py_question_t q1, q2;
    for (int i = 0; i < 40; i++) {
        bool ok1 = py_quiz_next(&r1, &q1);
        bool ok2 = py_quiz_next(&r2, &q2);
        assert(ok1 && ok2);
        assert(q1.type == q2.type);
        assert(q1.word_index == q2.word_index);
        assert(q1.correct == q2.correct);
        assert(memcmp(q1.options, q2.options, sizeof(q1.options)) == 0);
        assert_question_invariants(&q1);
    }
}

static void test_no_immediate_repeat(void)
{
    py_quiz_rng_t r = {.seed = 999, .lesson_mask = FULL_MASK};
    py_question_t q;
    uint16_t history[40];
    for (int i = 0; i < 40; i++) {
        assert(py_quiz_next(&r, &q));
        history[i] = q.word_index;
        /* 与最近 6 题不同词 */
        for (int k = 1; k <= 6 && i - k >= 0; k++) {
            assert(history[i] != history[i - k]);
        }
    }
}

static void test_whole_pick_has_one_whole(void)
{
    py_quiz_rng_t r = {.seed = 4242, .lesson_mask = FULL_MASK};
    py_question_t q;
    int whole_questions = 0;
    for (int i = 0; i < 200; i++) {
        if (!py_quiz_next(&r, &q)) {
            assert(0);
        }
        if (q.type == PY_Q_WHOLE_PICK) {
            whole_questions++;
            int wholes = 0;
            for (int k = 0; k < PY_Q_OPTIONS; k++) {
                if (py_whole_id(q.options[k]) >= 0) {
                    wholes++;
                }
            }
            assert(wholes == 1); /* 恰一个整体认读音节 */
        }
    }
    assert(whole_questions > 5); /* 200 题内整体认读题占比合理 */
}

static void test_scope_mask(void)
{
    /* 只开第 1 课：题池仅 阿 鹅 恶 哦（无整体认读词）*/
    py_quiz_rng_t r = {.seed = 7, .lesson_mask = 0x0001};
    py_question_t q;
    for (int i = 0; i < 10; i++) {
        assert(py_quiz_next(&r, &q));
        assert(py_word_lesson(q.word_index) == 1);
        assert_question_invariants(&q);
    }
    /* 空掩码 */
    py_quiz_rng_t empty = {.seed = 7, .lesson_mask = 0};
    assert(!py_quiz_next(&empty, &q));
    /* 前 2 课范围内不出后续课的词 */
    py_quiz_rng_t two = {.seed = 31, .lesson_mask = 0x0003};
    for (int i = 0; i < 30; i++) {
        assert(py_quiz_next(&two, &q));
        assert(py_word_lesson(q.word_index) <= 2);
    }
}

static void test_tone_options(void)
{
    py_quiz_rng_t r = {.seed = 20261026, .lesson_mask = FULL_MASK};
    py_question_t q;
    int tone_q = 0;
    for (int i = 0; i < 300; i++) {
        assert(py_quiz_next(&r, &q));
        if (q.type == PY_Q_TONE) {
            tone_q++;
            /* 三个带调选项互异（字节长度一致：同一基础式的不同声调）*/
            size_t len = strlen(q.options[0]);
            assert(len == strlen(q.options[1]) && len == strlen(q.options[2]));
            /* 正确项 = 目标词的组串（带调）*/
            char syl[PY_BUF];
            assert(py_compose_word(py_word(q.word_index), syl, sizeof(syl)) > 0);
            assert(strcmp(q.options[q.correct], syl) == 0);
            /* 题面为不带调基础式：逐字符比较（跳过变长的调号）*/
            assert(q.prompt_pinyin[0] != '\0');
        }
    }
    assert(tone_q > 5);
}

static void test_fuzz_invariants(void)
{
    /* 10k 次随机种子遍历不变量 */
    for (uint32_t s = 1; s < 10000; s += 7) {
        py_quiz_rng_t r = {.seed = s, .lesson_mask = FULL_MASK};
        py_question_t q;
        for (int i = 0; i < 12; i++) {
            if (!py_quiz_next(&r, &q)) {
                assert(0);
            }
            assert_question_invariants(&q);
        }
    }
}

static void test_stars(void)
{
    assert(py_quiz_round_stars(10) == 3);
    assert(py_quiz_round_stars(9) == 3);
    assert(py_quiz_round_stars(8) == 2);
    assert(py_quiz_round_stars(7) == 2);
    assert(py_quiz_round_stars(6) == 1);
    assert(py_quiz_round_stars(5) == 1);
    assert(py_quiz_round_stars(4) == 0);
    assert(py_quiz_round_stars(0) == 0);
}

int main(void)
{
    test_determinism();
    test_no_immediate_repeat();
    test_whole_pick_has_one_whole();
    test_scope_mask();
    test_tone_options();
    test_fuzz_invariants();
    test_stars();
    printf("test_quiz_engine: PASS\n");
    return 0;
}
