/* 拼读引擎：合法性判定、ü 规则、标调位置、NFC 组串、ASCII 音频 id。
 * 纯逻辑模块：仅依赖标准 C 头文件与 pinyin_data.h。
 */
#ifndef PINYIN_BLEND_H
#define PINYIN_BLEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pinyin_data.h"

/* 组串缓冲区建议大小（最长 syllable "zhuang"/"chuang" + NUL 也足够）*/
#define PY_BUF 16

/* ---- 合法性（普通话音节表，一年级教材范围）---- */
bool py_two_part_valid(int8_t initial_id, int8_t final_id);            /* 两拼 */
bool py_three_part_valid(int8_t initial_id, int8_t medial, int8_t final_id); /* 三拼 */
/* 声母后 ü 需去两点写作 u（j q x y）；n l 保留 */
bool py_initial_dots_u(int8_t initial_id);

/* ---- 组串（NFC UTF-8；成功返回字节数不含 NUL，参数非法/缓冲不足返回负值）---- */
int py_compose_final_marked(int8_t final_id, uint8_t tone, char *buf, size_t cap); /* "üe",4 → "üè" */
int py_compose_syllable(int8_t initial_id, int8_t final_id, uint8_t tone,
                        char *buf, size_t cap);                          /* b,a,4 → "bà"; j,ü,4 → "jù" */
int py_compose_syllable3(int8_t initial_id, int8_t medial, int8_t final_id,
                         uint8_t tone, char *buf, size_t cap);          /* g,u,a,1 → "guā" */
int py_compose_whole(int8_t whole_id, uint8_t tone, char *buf, size_t cap); /* yu,4 → "yù" */
int py_compose_word(const py_word_t *w, char *buf, size_t cap);         /* 例词拼音 */

/* 标调位置（韵母字母串内下标；用于卡片声调动画与测试）*/
uint8_t py_tone_mark_pos(int8_t final_id);

/* ---- ASCII 音频 id（ü→v，j q x y 后 ü 写作 u；相同 id 即相同发音）---- */
int py_aid_final(int8_t final_id, uint8_t tone, char *buf, size_t cap);   /* "a2","ve4","v3" */
int py_aid_initial(int8_t initial_id, char *buf, size_t cap);             /* "bo1","zhi1","ri4" */
int py_aid_whole(int8_t whole_id, uint8_t tone, char *buf, size_t cap);   /* "yu4","ying2" */
int py_aid_word(const py_word_t *w, char *buf, size_t cap);               /* "ba4","guo3","qu4" */

#endif /* PINYIN_BLEND_H */
