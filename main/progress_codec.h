/* 学习进度 blob 编解码：NVS 存储的单键版本化格式（magic+version+CRC32）。
 * 纯逻辑模块：编码/解码/校验与默认值，主机可测；NVS 读写由 app_progress 完成。
 */
#ifndef PROGRESS_CODEC_H
#define PROGRESS_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pinyin_data.h"

#define PROGRESS_MAGIC 0x50594E43u /* "PYNC" */
#define PROGRESS_VERSION 1u
#define PROGRESS_BLOB_SIZE 104u

typedef struct {
    uint8_t last_lesson;   /* 1..13，0 视为未设置 */
    uint16_t last_card;    /* 课内卡片序号 */
    uint8_t stars[PY_N_ITEMS];      /* 每课件条目 0..3 */
    uint16_t quiz_attempts;
    uint16_t quiz_correct;
    uint8_t lesson_best[PY_N_LESSONS]; /* 每课最佳正确率百分比 0..100 */
} pinyin_progress_t;

void pinyin_progress_defaults(pinyin_progress_t *p);

/* 编码到 blob（PROGRESS_BLOB_SIZE 字节）；buf/cap 不足返回 false。*/
bool progress_encode(const pinyin_progress_t *p, uint8_t *buf, size_t cap);
/* 解码并校验（magic/version/长度/CRC）；任何损坏返回 false 且 *p 保持不变或置默认。*/
bool progress_decode(const uint8_t *buf, size_t len, pinyin_progress_t *p);

/* CRC32（IEEE 802.3，反射多项式 0xEDB88320）。*/
uint32_t progress_crc32(const uint8_t *data, size_t len);

#endif /* PROGRESS_CODEC_H */
