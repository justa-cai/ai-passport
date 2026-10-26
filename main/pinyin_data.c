/* 拼音课程数据表：部编版（统编版）一年级上册，13 课。
 * 内容依据课本目录：
 *   1 a o e | 2 i u ü y w yi wu yu | 3 b p m f | 4 d t n l | 5 g k h
 *   6 j q x | 7 z c s zi ci si | 8 zh ch sh r zhi chi shi ri
 *   9 ai ei ui | 10 ao ou iu | 11 ie üe er ye yue
 *   12 an en in un ün yuan yin yun | 13 ang eng ing ong ying
 * 例词为一年级常见单字，拼音分解（声母/介音/韵母/整体认读/声调）逐条核对。
 * 字符串字面量一律 NFC 形式的 UTF-8（进入字体覆盖清单）。
 */
#include "pinyin_data.h"

#include <string.h>

const char *const py_initials[PY_N_INITIALS] = {
    "b", "p", "m", "f", "d", "t", "n", "l", "g", "k", "h", "j", "q",
    "x", "zh", "ch", "sh", "r", "z", "c", "s", "y", "w",
};

const char *const py_finals[PY_N_FINALS] = {
    "a", "o", "e", "i", "u", "ü",          /* 单韵母 L1/L2 */
    "ai", "ei", "ui", "ao", "ou", "iu", "ie", "üe", "er", /* 复韵母 L9-L11 */
    "an", "en", "in", "un", "ün",          /* 前鼻韵母 L12 */
    "ang", "eng", "ing", "ong",            /* 后鼻韵母 L13 */
};

const char *const py_whole[PY_N_WHOLE] = {
    "zhi", "chi", "shi", "ri", "zi", "ci", "si", /* L7/L8 */
    "yi", "wu", "yu",                             /* L2 */
    "ye", "yue",                                  /* L11 */
    "yuan", "yin", "yun",                         /* L12 */
    "ying",                                       /* L13 */
};

/* 声母呼读音：bō pō mō fō dē tē nē lē gē kē hē jī qī xī zhī chī shī rì zī cī sī yī wū */
const char *const py_initial_calls[PY_N_INITIALS] = {
    "bō", "pō", "mō", "fō",
    "dē", "tē", "nē", "lē",
    "gē", "kē", "hē",
    "jī", "qī", "xī",
    "zhī", "chī", "shī", "rì",
    "zī", "cī", "sī",
    "yī", "wū",
};

/* 63 个课件条目，按课排序。*/
static const py_item_t k_items[PY_N_ITEMS] = {
    /* 第 1 课 a o e */
    {1, PY_K_FINAL, -1, 0, -1}, {1, PY_K_FINAL, -1, 1, -1}, {1, PY_K_FINAL, -1, 2, -1},
    /* 第 2 课 i u ü y w yi wu yu */
    {2, PY_K_FINAL, -1, 3, -1}, {2, PY_K_FINAL, -1, 4, -1}, {2, PY_K_FINAL, -1, 5, -1},
    {2, PY_K_INITIAL, 21, -1, -1}, {2, PY_K_INITIAL, 22, -1, -1},
    {2, PY_K_WHOLE, -1, -1, 7}, {2, PY_K_WHOLE, -1, -1, 8}, {2, PY_K_WHOLE, -1, -1, 9},
    /* 第 3 课 b p m f */
    {3, PY_K_INITIAL, 0, -1, -1}, {3, PY_K_INITIAL, 1, -1, -1},
    {3, PY_K_INITIAL, 2, -1, -1}, {3, PY_K_INITIAL, 3, -1, -1},
    /* 第 4 课 d t n l */
    {4, PY_K_INITIAL, 4, -1, -1}, {4, PY_K_INITIAL, 5, -1, -1},
    {4, PY_K_INITIAL, 6, -1, -1}, {4, PY_K_INITIAL, 7, -1, -1},
    /* 第 5 课 g k h */
    {5, PY_K_INITIAL, 8, -1, -1}, {5, PY_K_INITIAL, 9, -1, -1},
    {5, PY_K_INITIAL, 10, -1, -1},
    /* 第 6 课 j q x */
    {6, PY_K_INITIAL, 11, -1, -1}, {6, PY_K_INITIAL, 12, -1, -1},
    {6, PY_K_INITIAL, 13, -1, -1},
    /* 第 7 课 z c s zi ci si */
    {7, PY_K_INITIAL, 18, -1, -1}, {7, PY_K_INITIAL, 19, -1, -1},
    {7, PY_K_INITIAL, 20, -1, -1},
    {7, PY_K_WHOLE, -1, -1, 4}, {7, PY_K_WHOLE, -1, -1, 5}, {7, PY_K_WHOLE, -1, -1, 6},
    /* 第 8 课 zh ch sh r zhi chi shi ri */
    {8, PY_K_INITIAL, 14, -1, -1}, {8, PY_K_INITIAL, 15, -1, -1},
    {8, PY_K_INITIAL, 16, -1, -1}, {8, PY_K_INITIAL, 17, -1, -1},
    {8, PY_K_WHOLE, -1, -1, 0}, {8, PY_K_WHOLE, -1, -1, 1},
    {8, PY_K_WHOLE, -1, -1, 2}, {8, PY_K_WHOLE, -1, -1, 3},
    /* 第 9 课 ai ei ui */
    {9, PY_K_FINAL, -1, 6, -1}, {9, PY_K_FINAL, -1, 7, -1}, {9, PY_K_FINAL, -1, 8, -1},
    /* 第 10 课 ao ou iu */
    {10, PY_K_FINAL, -1, 9, -1}, {10, PY_K_FINAL, -1, 10, -1}, {10, PY_K_FINAL, -1, 11, -1},
    /* 第 11 课 ie üe er ye yue */
    {11, PY_K_FINAL, -1, 12, -1}, {11, PY_K_FINAL, -1, 13, -1}, {11, PY_K_FINAL, -1, 14, -1},
    {11, PY_K_WHOLE, -1, -1, 10}, {11, PY_K_WHOLE, -1, -1, 11},
    /* 第 12 课 an en in un ün yuan yin yun */
    {12, PY_K_FINAL, -1, 15, -1}, {12, PY_K_FINAL, -1, 16, -1},
    {12, PY_K_FINAL, -1, 17, -1}, {12, PY_K_FINAL, -1, 18, -1},
    {12, PY_K_FINAL, -1, 19, -1},
    {12, PY_K_WHOLE, -1, -1, 12}, {12, PY_K_WHOLE, -1, -1, 13},
    {12, PY_K_WHOLE, -1, -1, 14},
    /* 第 13 课 ang eng ing ong ying */
    {13, PY_K_FINAL, -1, 20, -1}, {13, PY_K_FINAL, -1, 21, -1},
    {13, PY_K_FINAL, -1, 22, -1}, {13, PY_K_FINAL, -1, 23, -1},
    {13, PY_K_WHOLE, -1, -1, 15},
};

/* 例词池：lesson 由 py_word_lesson() 按分解条目课次最大值推导。*/
static const py_word_t k_words[] = {
    /* 第 1 课 a o e */
    {"阿", -1, -1, 0, -1, 1},
    {"鹅", -1, -1, 2, -1, 2},
    {"恶", -1, -1, 2, -1, 4},
    {"哦", -1, -1, 1, -1, 2},
    /* 第 2 课 i u ü y w yi wu yu */
    {"衣", -1, -1, -1, 7, 1},
    {"屋", -1, -1, -1, 8, 1},
    {"鱼", -1, -1, -1, 9, 2},
    {"雨", -1, -1, -1, 9, 3},
    {"五", -1, -1, -1, 8, 3},
    {"蛙", 22, -1, 0, -1, 1},
    {"鸭", 21, -1, 0, -1, 1},
    /* 第 3 课 b p m f */
    {"爸", 0, -1, 0, -1, 4},
    {"妈", 2, -1, 0, -1, 1},
    {"马", 2, -1, 0, -1, 3},
    {"米", 2, -1, 3, -1, 3},
    {"木", 2, -1, 4, -1, 4},
    {"布", 0, -1, 4, -1, 4},
    {"波", 0, -1, 1, -1, 1},
    {"婆", 1, -1, 1, -1, 2},
    {"发", 3, -1, 0, -1, 1},
    {"佛", 3, -1, 1, -1, 2},
    /* 第 4 课 d t n l */
    {"大", 4, -1, 0, -1, 4},
    {"地", 4, -1, 3, -1, 4},
    {"兔", 5, -1, 4, -1, 4},
    {"他", 5, -1, 0, -1, 1},
    {"你", 6, -1, 3, -1, 3},
    {"拉", 7, -1, 0, -1, 1},
    {"路", 7, -1, 4, -1, 4},
    {"拿", 6, -1, 0, -1, 2},
    {"乐", 7, -1, 2, -1, 4},
    {"特", 5, -1, 2, -1, 4},
    /* 第 5 课 g k h（三拼 guǒ huā huà）*/
    {"哥", 8, -1, 2, -1, 1},
    {"果", 8, 1, 1, -1, 3},
    {"花", 10, 1, 0, -1, 1},
    {"画", 10, 1, 0, -1, 4},
    {"哭", 9, -1, 4, -1, 1},
    {"河", 10, -1, 2, -1, 2},
    {"古", 8, -1, 4, -1, 3},
    {"课", 9, -1, 2, -1, 4},
    /* 第 6 课 j q x（三拼 jiā xià xiā；ü 去点 jú qù）*/
    {"鸡", 11, -1, 3, -1, 1},
    {"七", 12, -1, 3, -1, 1},
    {"西", 13, -1, 3, -1, 1},
    {"家", 11, 0, 0, -1, 1},
    {"下", 13, 0, 0, -1, 4},
    {"虾", 13, 0, 0, -1, 1},
    {"菊", 11, -1, 5, -1, 2},
    {"去", 12, -1, 5, -1, 4},
    /* 第 7 课 z c s zi ci si（三拼 zuò）*/
    {"字", -1, -1, -1, 4, 4},
    {"刺", -1, -1, -1, 5, 4},
    {"丝", -1, -1, -1, 6, 1},
    {"早", 18, -1, 9, -1, 3},
    {"苏", 20, -1, 4, -1, 1},
    {"座", 18, 1, 1, -1, 4},
    /* 第 8 课 zh ch sh r zhi chi shi ri */
    {"纸", -1, -1, -1, 0, 3},
    {"尺", -1, -1, -1, 1, 3},
    {"十", -1, -1, -1, 2, 2},
    {"日", -1, -1, -1, 3, 4},
    {"茶", 15, -1, 0, -1, 2},
    {"沙", 16, -1, 0, -1, 1},
    {"竹", 14, -1, 4, -1, 2},
    {"热", 17, -1, 2, -1, 4},
    {"肉", 17, -1, 10, -1, 4},
    /* 第 9 课 ai ei ui */
    {"白", 0, -1, 6, -1, 2},
    {"菜", 19, -1, 6, -1, 4},
    {"来", 7, -1, 6, -1, 2},
    {"飞", 3, -1, 7, -1, 1},
    {"北", 0, -1, 7, -1, 3},
    {"妹", 2, -1, 7, -1, 4},
    {"雷", 7, -1, 7, -1, 2},
    {"水", 16, -1, 8, -1, 3},
    {"嘴", 18, -1, 8, -1, 3},
    /* 第 10 课 ao ou iu */
    {"猫", 2, -1, 9, -1, 1},
    {"高", 8, -1, 9, -1, 1},
    {"桃", 5, -1, 9, -1, 2},
    {"草", 19, -1, 9, -1, 3},
    {"跑", 1, -1, 9, -1, 3},
    {"头", 5, -1, 10, -1, 2},
    {"狗", 8, -1, 10, -1, 3},
    {"楼", 7, -1, 10, -1, 2},
    {"牛", 6, -1, 11, -1, 2},
    {"六", 7, -1, 11, -1, 4},
    /* 第 11 课 ie üe er ye yue */
    {"叶", -1, -1, -1, 10, 4},
    {"月", -1, -1, -1, 11, 4},
    {"耳", -1, -1, 14, -1, 3},
    {"二", -1, -1, 14, -1, 4},
    {"姐", 11, -1, 12, -1, 3},
    {"鞋", 13, -1, 12, -1, 2},
    {"雪", 13, -1, 13, -1, 3},
    /* 第 12 课 an en in un ün yuan yin yun */
    {"山", 16, -1, 15, -1, 1},
    {"蓝", 7, -1, 15, -1, 2},
    {"门", 2, -1, 16, -1, 2},
    {"本", 0, -1, 16, -1, 3},
    {"林", 7, -1, 17, -1, 2},
    {"心", 13, -1, 17, -1, 1},
    {"村", 19, -1, 18, -1, 1},
    {"云", -1, -1, -1, 14, 2},
    {"圆", -1, -1, -1, 12, 2},
    {"银", -1, -1, -1, 13, 2},
    {"裙", 12, -1, 19, -1, 2},
    /* 第 13 课 ang eng ing ong ying */
    {"羊", 21, -1, 20, -1, 2},
    {"上", 16, -1, 20, -1, 4},
    {"灯", 4, -1, 21, -1, 1},
    {"风", 3, -1, 21, -1, 1},
    {"星", 13, -1, 22, -1, 1},
    {"明", 2, -1, 22, -1, 2},
    {"虫", 15, -1, 23, -1, 2},
    {"中", 14, -1, 23, -1, 1},
    {"龙", 7, -1, 23, -1, 2},
    {"鹰", -1, -1, -1, 15, 1},
    {"松", 20, -1, 23, -1, 1},
};

#define PY_WORD_COUNT (sizeof(k_words) / sizeof(k_words[0]))

static const char *const k_lesson_titles[PY_N_LESSONS] = {
    "第1课 a o e",
    "第2课 i u ü y w",
    "第3课 b p m f",
    "第4课 d t n l",
    "第5课 g k h",
    "第6课 j q x",
    "第7课 z c s",
    "第8课 zh ch sh r",
    "第9课 ai ei ui",
    "第10课 ao ou iu",
    "第11课 ie üe er",
    "第12课 an en in un ün",
    "第13课 ang eng ing ong",
};

static const char *const k_lesson_descs[PY_N_LESSONS] = {
    "单韵母 · 认识四声",
    "声母 y w · 整体认读",
    "声母 · 两拼音节",
    "声母 · 区分 n l",
    "声母 · 三拼音节",
    "ü 遇 j q x 去两点",
    "平舌音 · 整体认读",
    "翘舌音 · 整体认读",
    "复韵母 · 标调规则",
    "复韵母 ao ou iu",
    "复韵母 · 特殊韵母 er",
    "前鼻韵母 · 整体认读",
    "后鼻韵母 · 整体认读",
};

const py_item_t *py_item(uint16_t index)
{
    if (index >= PY_N_ITEMS) {
        return NULL;
    }
    return &k_items[index];
}

uint16_t py_item_count(void)
{
    return PY_N_ITEMS;
}

int16_t py_item_first_in_lesson(uint8_t lesson)
{
    if (lesson < 1 || lesson > PY_N_LESSONS) {
        return -1;
    }
    for (uint16_t i = 0; i < PY_N_ITEMS; i++) {
        if (k_items[i].lesson == lesson) {
            return (int16_t)i;
        }
    }
    return -1;
}

uint16_t py_item_lesson_count(uint8_t lesson)
{
    if (lesson < 1 || lesson > PY_N_LESSONS) {
        return 0;
    }
    uint16_t n = 0;
    for (uint16_t i = 0; i < PY_N_ITEMS; i++) {
        if (k_items[i].lesson == lesson) {
            n++;
        }
    }
    return n;
}

uint8_t py_lesson_of_item(uint16_t index)
{
    if (index >= PY_N_ITEMS) {
        return 0;
    }
    return k_items[index].lesson;
}

const py_word_t *py_word(uint16_t index)
{
    if (index >= PY_WORD_COUNT) {
        return NULL;
    }
    return &k_words[index];
}

uint16_t py_word_count(void)
{
    return (uint16_t)PY_WORD_COUNT;
}

static uint8_t lesson_of_id(int8_t initial_id, int8_t final_id, int8_t whole_id)
{
    uint8_t lesson = 0;
    for (uint16_t i = 0; i < PY_N_ITEMS; i++) {
        const py_item_t *it = &k_items[i];
        if ((initial_id >= 0 && it->kind == PY_K_INITIAL && it->initial_id == initial_id) ||
            (final_id >= 0 && it->kind == PY_K_FINAL && it->final_id == final_id) ||
            (whole_id >= 0 && it->kind == PY_K_WHOLE && it->whole_id == whole_id)) {
            if (it->lesson > lesson) {
                lesson = it->lesson;
            }
        }
    }
    return lesson;
}

uint8_t py_word_lesson(uint16_t index)
{
    if (index >= PY_WORD_COUNT) {
        return 0;
    }
    const py_word_t *w = &k_words[index];
    return lesson_of_id(w->initial_id, w->final_id, w->whole_id);
}

uint8_t py_words_for_item(uint16_t item_index, uint16_t *word_indices, uint8_t max_out)
{
    if (item_index >= PY_N_ITEMS || word_indices == NULL || max_out == 0) {
        return 0;
    }
    const py_item_t *it = &k_items[item_index];
    uint8_t n = 0;
    for (uint16_t i = 0; i < PY_WORD_COUNT && n < max_out; i++) {
        const py_word_t *w = &k_words[i];
        bool match = (it->kind == PY_K_INITIAL && w->initial_id == it->initial_id) ||
                     (it->kind == PY_K_FINAL && w->final_id == it->final_id) ||
                     (it->kind == PY_K_WHOLE && w->whole_id == it->whole_id);
        if (match) {
            word_indices[n++] = i;
        }
    }
    return n;
}

const char *py_lesson_title(uint8_t lesson)
{
    if (lesson < 1 || lesson > PY_N_LESSONS) {
        return NULL;
    }
    return k_lesson_titles[lesson - 1];
}

const char *py_lesson_desc(uint8_t lesson)
{
    if (lesson < 1 || lesson > PY_N_LESSONS) {
        return NULL;
    }
    return k_lesson_descs[lesson - 1];
}

static int8_t id_from_table(const char *const *table, int n, const char *s)
{
    if (s == NULL) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (strcmp(table[i], s) == 0) {
            return (int8_t)i;
        }
    }
    return -1;
}

int8_t py_initial_id(const char *s)
{
    return id_from_table(py_initials, PY_N_INITIALS, s);
}

int8_t py_final_id(const char *s)
{
    return id_from_table(py_finals, PY_N_FINALS, s);
}

int8_t py_whole_id(const char *s)
{
    return id_from_table(py_whole, PY_N_WHOLE, s);
}
