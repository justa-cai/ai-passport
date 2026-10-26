/* 测验引擎实现。
 * 随机源：LCG s = s*1664525 + 1013904223（32 位回绕），确定性、无库依赖。
 * 干扰项取易混集合：声母按发音部位分组，韵母取最小对立对，声调取同音节其余声调，
 * 整体认读题保证三选一恰含一个整体认读音节。
 */
#include "quiz_engine.h"

#include "pinyin_blend.h"

#include <string.h>

/* 声母易混分组（组内互为干扰项优先）。*/
static const int8_t k_initial_groups[][5] = {
    {0, 1, 2, 3, -1},          /* b p m f */
    {4, 5, 6, 7, -1},          /* d t n l */
    {8, 9, 10, -1},            /* g k h */
    {11, 12, 13, -1},          /* j q x */
    {14, 15, 16, 17, -1},      /* zh ch sh r */
    {18, 19, 20, -1},          /* z c s */
    {21, 22, -1},              /* y w */
};
#define PY_N_INIT_GROUPS (sizeof(k_initial_groups) / sizeof(k_initial_groups[0]))

/* 韵母最小对立对（同组互为干扰项优先）。*/
static const int8_t k_final_pairs[][4] = {
    {6, 7, 8},   /* ai ei ui */
    {9, 10, 11}, /* ao ou iu */
    {12, 13, -1},/* ie üe */
    {15, 20, -1},/* an ang */
    {16, 21, -1},/* en eng */
    {17, 22, -1},/* in ing */
    {18, 19, -1},/* un ün */
    {3, 4, -1},  /* i u */
    {0, 1, 2, -1}, /* a o e */
    {14, 5, -1}, /* er ü（单韵母兜底组）*/
};
#define PY_N_FINAL_PAIRS (sizeof(k_final_pairs) / sizeof(k_final_pairs[0]))

static uint32_t lcg_next(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

static uint32_t rand_below(uint32_t *s, uint32_t n)
{
    if (n == 0) {
        return 0;
    }
    return lcg_next(s) % n;
}

/* 题池：word 的推导课次在 mask 内且不在 recent 窗口。
 * 返回词下标写入 out（容量 cap），返回数量。*/
static uint16_t build_pool(const py_quiz_rng_t *r, uint16_t *out, uint16_t cap)
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < py_word_count() && n < cap; i++) {
        uint8_t lesson = py_word_lesson(i);
        if (lesson == 0 || lesson > PY_N_LESSONS) {
            continue;
        }
        if (!(r->lesson_mask & (1u << (lesson - 1)))) {
            continue;
        }
        bool recent = false;
        for (uint8_t k = 0; k < r->recent_len && k < 6; k++) {
            if (r->recent[k] == i) {
                recent = true;
                break;
            }
        }
        if (!recent) {
            out[n++] = i;
        }
    }
    return n;
}

static void push_recent(py_quiz_rng_t *r, uint16_t word_index)
{
    if (r->recent_len < 6) {
        r->recent[r->recent_len++] = word_index;
    } else {
        memmove(&r->recent[0], &r->recent[1], sizeof(uint16_t) * 5);
        r->recent[5] = word_index;
    }
}

/* 词对应的课件条目下标（掌握度 key）：优先声母，其次韵母，最后整体认读。*/
static int16_t word_item(const py_word_t *w)
{
    for (uint16_t i = 0; i < py_item_count(); i++) {
        const py_item_t *it = py_item(i);
        if (w->initial_id >= 0 && it->kind == PY_K_INITIAL && it->initial_id == w->initial_id) {
            return (int16_t)i;
        }
    }
    for (uint16_t i = 0; i < py_item_count(); i++) {
        const py_item_t *it = py_item(i);
        if (w->final_id >= 0 && it->kind == PY_K_FINAL && it->final_id == w->final_id) {
            return (int16_t)i;
        }
    }
    for (uint16_t i = 0; i < py_item_count(); i++) {
        const py_item_t *it = py_item(i);
        if (w->whole_id >= 0 && it->kind == PY_K_WHOLE && it->whole_id == w->whole_id) {
            return (int16_t)i;
        }
    }
    return -1;
}

static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    for (; src[i] != '\0' && i + 1 < cap; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* 从 group（-1 结尾）挑 2 个不等于 exclude 的成员写入 out。*/
static bool pick_two_from_group(uint32_t *seed, const int8_t *group, int8_t exclude,
                                int8_t *out /* [2] */)
{
    int8_t cands[5];
    int n = 0;
    for (int i = 0; i < 5 && group[i] >= 0; i++) {
        if (group[i] != exclude) {
            cands[n++] = group[i];
        }
    }
    if (n < 2) {
        return false;
    }
    /* 前两位随机交换取样的简易抽样 */
    uint32_t a = rand_below(seed, (uint32_t)n);
    uint32_t b = rand_below(seed, (uint32_t)n);
    if (b == a) {
        b = (b + 1) % (uint32_t)n;
    }
    out[0] = cands[a];
    out[1] = cands[b];
    return true;
}

/* 生成"选出声母/韵母"题。*/
static bool build_pick(py_quiz_rng_t *r, py_question_t *q, const py_word_t *w, bool pick_initial)
{
    int8_t target = pick_initial ? w->initial_id : w->final_id;
    if (target < 0) {
        return false;
    }
    memset(q->options, 0, sizeof(q->options));
    int8_t distractors[2];
    bool ok = false;
    if (pick_initial) {
        for (size_t g = 0; g < PY_N_INIT_GROUPS && !ok; g++) {
            for (int i = 0; i < 5 && k_initial_groups[g][i] >= 0 && !ok; i++) {
                if (k_initial_groups[g][i] == target) {
                    ok = pick_two_from_group(&r->seed, k_initial_groups[g], target, distractors);
                }
            }
        }
        if (!ok) { /* 全局兜底 */
            int8_t cands[PY_N_INITIALS];
            int n = 0;
            for (int i = 0; i < PY_N_INITIALS; i++) {
                if (i != target) {
                    cands[n++] = (int8_t)i;
                }
            }
            uint32_t a = rand_below(&r->seed, (uint32_t)n);
            uint32_t b = rand_below(&r->seed, (uint32_t)n);
            if (b == a) {
                b = (b + 1) % (uint32_t)n;
            }
            distractors[0] = cands[a];
            distractors[1] = cands[b];
        }
        copy_str(q->options[0], PY_Q_TEXT, py_initials[target]);
        copy_str(q->options[1], PY_Q_TEXT, py_initials[distractors[0]]);
        copy_str(q->options[2], PY_Q_TEXT, py_initials[distractors[1]]);
    } else {
        for (size_t g = 0; g < PY_N_FINAL_PAIRS && !ok; g++) {
            for (int i = 0; i < 4 && k_final_pairs[g][i] >= 0 && !ok; i++) {
                if (k_final_pairs[g][i] == target) {
                    ok = pick_two_from_group(&r->seed, k_final_pairs[g], target, distractors);
                }
            }
        }
        if (!ok) {
            int8_t cands[PY_N_FINALS];
            int n = 0;
            for (int i = 0; i < PY_N_FINALS; i++) {
                if (i != target) {
                    cands[n++] = (int8_t)i;
                }
            }
            uint32_t a = rand_below(&r->seed, (uint32_t)n);
            uint32_t b = rand_below(&r->seed, (uint32_t)n);
            if (b == a) {
                b = (b + 1) % (uint32_t)n;
            }
            distractors[0] = cands[a];
            distractors[1] = cands[b];
        }
        copy_str(q->options[0], PY_Q_TEXT, py_finals[target]);
        copy_str(q->options[1], PY_Q_TEXT, py_finals[distractors[0]]);
        copy_str(q->options[2], PY_Q_TEXT, py_finals[distractors[1]]);
    }
    q->type = pick_initial ? PY_Q_PICK_INITIAL : PY_Q_PICK_FINAL;
    return true;
}

/* 生成"选出声调"题：选项为目标音节三个带调形式（含正确项）。*/
static bool build_tone(py_quiz_rng_t *r, py_question_t *q, const py_word_t *w)
{
    memset(q->options, 0, sizeof(q->options));
    q->type = PY_Q_TONE;
    /* 词必须可组串（用于音频与选项）*/
    char syl[PY_BUF];
    if (py_compose_word(w, syl, sizeof(syl)) < 0) {
        return false;
    }
    /* 题面拼音：不带调基础式（声母+介音+韵母 / 整体认读原形）*/
    char base[PY_BUF];
    size_t len = 0;
    if (w->whole_id >= 0) {
        copy_str(base + len, sizeof(base) - len, py_whole[w->whole_id]);
    } else {
        if (w->initial_id >= 0) {
            copy_str(base + len, sizeof(base) - len, py_initials[w->initial_id]);
            len = strlen(base);
        }
        if (w->medial_id >= 0) {
            static const char *const medials[] = {"i", "u", "ü"};
            copy_str(base + len, sizeof(base) - len, medials[w->medial_id]);
            len = strlen(base);
        }
        if (w->final_id >= 0) {
            copy_str(base + len, sizeof(base) - len, py_finals[w->final_id]);
        }
    }
    copy_str(q->prompt_pinyin, PY_Q_TEXT, base);
    /* 选项：3 个声调（正确声调必含，另两个从其余声调抽）*/
    uint8_t others[3];
    int n = 0;
    for (uint8_t t = 1; t <= 4; t++) {
        if (t != w->tone) {
            others[n++] = t;
        }
    }
    uint32_t a = rand_below(&r->seed, 3);
    uint32_t b = rand_below(&r->seed, 3);
    if (b == a) {
        b = (b + 1) % 3;
    }
    uint8_t tones[3] = {w->tone, others[a], others[b]};
    for (int i = 0; i < 3; i++) {
        char marked[PY_BUF];
        if (w->whole_id >= 0) {
            py_compose_whole(w->whole_id, tones[i], marked, sizeof(marked));
        } else if (w->medial_id >= 0) {
            py_compose_syllable3(w->initial_id, w->medial_id, w->final_id, tones[i], marked, sizeof(marked));
        } else if (w->initial_id >= 0) {
            py_compose_syllable(w->initial_id, w->final_id, tones[i], marked, sizeof(marked));
        } else {
            py_compose_final_marked(w->final_id, tones[i], marked, sizeof(marked));
        }
        copy_str(q->options[i], PY_Q_TEXT, marked);
    }
    return true;
}

/* 生成"哪个是整体认读音节"题：正确项 = 整体认读词，干扰 = 普通音节。*/
static bool build_whole_pick(py_quiz_rng_t *r, py_question_t *q, const py_word_t *w)
{
    if (w->whole_id < 0) {
        return false;
    }
    memset(q->options, 0, sizeof(q->options));
    q->type = PY_Q_WHOLE_PICK;
    copy_str(q->options[0], PY_Q_TEXT, py_whole[w->whole_id]);
    /* 干扰项：两个非整体认读的带调音节（从普通词池抽，避免凭长度猜）*/
    uint16_t used[2] = {0xFFFF, 0xFFFF};
    int filled = 0;
    for (int attempt = 0; attempt < 64 && filled < 2; attempt++) {
        uint16_t wi = (uint16_t)rand_below(&r->seed, py_word_count());
        const py_word_t *cand = py_word(wi);
        if (cand == NULL || cand->whole_id >= 0) {
            continue;
        }
        bool dup = (wi == used[0] || wi == used[1]);
        if (dup) {
            continue;
        }
        char syl[PY_BUF];
        if (py_compose_word(cand, syl, sizeof(syl)) >= 0) {
            copy_str(q->options[1 + filled], PY_Q_TEXT, syl);
            used[filled] = wi;
            filled++;
        }
    }
    if (filled < 2) {
        return false;
    }
    q->prompt_hanzi[0] = '\0';
    q->prompt_pinyin[0] = '\0';
    return true;
}

bool py_quiz_next(py_quiz_rng_t *r, py_question_t *q)
{
    if (r == NULL || q == NULL || r->lesson_mask == 0) {
        return false;
    }
    uint16_t pool[128];
    uint16_t pool_n = build_pool(r, pool, 128);
    if (pool_n == 0) {
        /* 放宽 recent 窗口再试一次 */
        uint8_t saved = r->recent_len;
        r->recent_len = 0;
        pool_n = build_pool(r, pool, 128);
        r->recent_len = saved;
        if (pool_n == 0) {
            return false;
        }
    }
    for (int attempt = 0; attempt < 16; attempt++) {
        uint16_t wi = pool[rand_below(&r->seed, pool_n)];
        const py_word_t *w = py_word(wi);
        if (w == NULL) {
            continue;
        }
        uint32_t roll = rand_below(&r->seed, 100);
        bool built = false;
        if (roll < 30) {
            built = build_pick(r, q, w, true);   /* 选声母 */
        } else if (roll < 60) {
            built = build_pick(r, q, w, false);  /* 选韵母 */
        } else if (roll < 80) {
            built = build_tone(r, q, w);         /* 辨声调 */
        } else {
            built = build_whole_pick(r, q, w);   /* 整体认读 */
        }
        if (!built) {
            /* 类型不适用（如零声母词的选声母）→ 换类型重试 */
            if (!(built = build_pick(r, q, w, false)) && !(built = build_tone(r, q, w))) {
                continue;
            }
        }
        q->word_index = wi;
        if (q->type != PY_Q_WHOLE_PICK) {
            copy_str(q->prompt_hanzi, sizeof(q->prompt_hanzi), w->hanzi);
            if (q->type != PY_Q_TONE) {
                char syl[PY_BUF];
                if (py_compose_word(w, syl, sizeof(syl)) >= 0) {
                    copy_str(q->prompt_pinyin, PY_Q_TEXT, syl);
                }
            }
        }
        q->item_index = word_item(w);
        /* 洗牌三选项 */
        uint8_t correct_value = 0;
        for (int i = 2; i > 0; i--) {
            uint32_t j = rand_below(&r->seed, (uint32_t)i + 1);
            if (j != (uint32_t)i) {
                char tmp[PY_Q_TEXT];
                memcpy(tmp, q->options[i], PY_Q_TEXT);
                memcpy(q->options[i], q->options[j], PY_Q_TEXT);
                memcpy(q->options[j], tmp, PY_Q_TEXT);
            }
            if (correct_value == i) {
                correct_value = (uint8_t)j;
            } else if (correct_value == (uint8_t)j) {
                correct_value = (uint8_t)i;
            }
        }
        q->correct = correct_value;
        push_recent(r, wi);
        return true;
    }
    return false;
}

bool py_quiz_check(const py_question_t *q, uint8_t chosen)
{
    if (q == NULL || chosen >= PY_Q_OPTIONS) {
        return false;
    }
    return chosen == q->correct;
}

uint8_t py_quiz_round_stars(uint8_t correct_count)
{
    if (correct_count >= 9) {
        return 3;
    }
    if (correct_count >= 7) {
        return 2;
    }
    if (correct_count >= 5) {
        return 1;
    }
    return 0;
}
