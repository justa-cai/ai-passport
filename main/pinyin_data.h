/* 拼音课程数据：部编版一年级上册 13 课拼音内容表。
 *
 * 纯逻辑模块：仅依赖标准 C 头文件，主机与固件共用（AGENTS.md 可测性约定）。
 * 表内容为课本事实：声母 23、韵母 24（含单韵母 6、复韵母 9、鼻韵母 9）、
 * 整体认读音节 16、课件条目 63、例词表。
 *
 * id 编码约定：
 *   initial_id  0..22  py_initials[] 下标；-1 = 无（零声母）
 *   final_id    0..23  py_finals[]   下标；-1 = 无（整体认读词）
 *   medial_id   0..2   {i, u, ü}     下标；-1 = 无介音（两拼）
 *   whole_id    0..15  py_whole[]    下标；-1 = 非整体认读
 */
#ifndef PINYIN_DATA_H
#define PINYIN_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PY_N_INITIALS 23
#define PY_N_FINALS 24
#define PY_N_WHOLE 16
#define PY_N_LESSONS 13
#define PY_N_ITEMS 63

/* 声母（教学顺序）*/
extern const char *const py_initials[PY_N_INITIALS];
/* 韵母（教学顺序：单韵母 6、复韵母 9、鼻韵母 9）*/
extern const char *const py_finals[PY_N_FINALS];
/* 整体认读音节（教学顺序）*/
extern const char *const py_whole[PY_N_WHOLE];
/* 声母呼读音（TTS 文本，与音频 id 一一对应）*/
extern const char *const py_initial_calls[PY_N_INITIALS];

typedef enum {
    PY_K_FINAL = 0, /* 韵母卡（含单韵母/复韵母/鼻韵母，四声展开）*/
    PY_K_INITIAL,   /* 声母卡（固定呼读音，1 张）*/
    PY_K_WHOLE,     /* 整体认读音节卡（四声展开）*/
} py_kind_t;

typedef struct {
    uint8_t lesson; /* 1..PY_N_LESSONS */
    uint8_t kind;   /* py_kind_t */
    int8_t initial_id;
    int8_t final_id;
    int8_t whole_id;
} py_item_t;

typedef struct {
    const char *hanzi; /* 单字例词 */
    int8_t initial_id; /* -1 = 零声母 */
    int8_t medial_id;  /* -1 = 无介音；0=i 1=u 2=ü */
    int8_t final_id;   /* -1 = 整体认读词 */
    int8_t whole_id;   /* >=0 表示整体认读词 */
    uint8_t tone;      /* 1..4 */
} py_word_t;

/* 课件条目 */
const py_item_t *py_item(uint16_t index); /* NULL 越界 */
uint16_t py_item_count(void);
int16_t py_item_first_in_lesson(uint8_t lesson); /* -1 无效课次 */
uint16_t py_item_lesson_count(uint8_t lesson);
uint8_t py_lesson_of_item(uint16_t index);

/* 例词池 */
const py_word_t *py_word(uint16_t index); /* NULL 越界 */
uint16_t py_word_count(void);
/* 词的归属课次 = 其声母/介音/韵母/整体认读条目课次的最大值（该课学完后可考）*/
uint8_t py_word_lesson(uint16_t index);
/* 某课件条目的例词（最多取 max_out 个，返回实际数量）*/
uint8_t py_words_for_item(uint16_t item_index, uint16_t *word_indices, uint8_t max_out);

/* 课标题与说明（中文 UI 字符串，进入字体清单）*/
const char *py_lesson_title(uint8_t lesson); /* 1..13，越界返回 NULL */
const char *py_lesson_desc(uint8_t lesson);

/* 字符串查找（返回下标，未找到 -1）*/
int8_t py_initial_id(const char *s);
int8_t py_final_id(const char *s);
int8_t py_whole_id(const char *s);

#endif /* PINYIN_DATA_H */
