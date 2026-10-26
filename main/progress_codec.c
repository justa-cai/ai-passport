/* 进度 blob 编解码实现。
 * 布局（小端，共 104 字节）：
 *   [0..3]   magic "PYNC"
 *   [4..5]   version
 *   [6..7]   payload 长度
 *   [8]      last_lesson
 *   [9..10]  last_card
 *   [11..73] stars[63]
 *   [74..75] quiz_attempts
 *   [76..77] quiz_correct
 *   [78..90] lesson_best[13]
 *   [91..99] 保留（0）
 *   [100..103] CRC32（覆盖字节 0..99）
 */
#include "progress_codec.h"

#include <string.h>

void pinyin_progress_defaults(pinyin_progress_t *p)
{
    if (p == NULL) {
        return;
    }
    memset(p, 0, sizeof(*p));
    p->last_lesson = 1;
}

uint32_t progress_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

static void put_u16(uint8_t *b, uint16_t v)
{
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)(v >> 8);
}

static uint16_t get_u16(const uint8_t *b)
{
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
}

static void put_u32(uint8_t *b, uint32_t v)
{
    put_u16(b, (uint16_t)(v & 0xFFFF));
    put_u16(b + 2, (uint16_t)(v >> 16));
}

static uint32_t get_u32(const uint8_t *b)
{
    return (uint32_t)get_u16(b) | ((uint32_t)get_u16(b + 2) << 16);
}

static uint8_t clamp_u8(int v, int lo, int hi)
{
    if (v < lo) {
        return (uint8_t)lo;
    }
    if (v > hi) {
        return (uint8_t)hi;
    }
    return (uint8_t)v;
}

bool progress_encode(const pinyin_progress_t *p, uint8_t *buf, size_t cap)
{
    if (p == NULL || buf == NULL || cap < PROGRESS_BLOB_SIZE) {
        return false;
    }
    memset(buf, 0, PROGRESS_BLOB_SIZE);
    put_u32(buf, PROGRESS_MAGIC);
    put_u16(buf + 4, PROGRESS_VERSION);
    put_u16(buf + 6, 92u); /* payload 长度（CRC 之前）*/
    buf[8] = clamp_u8(p->last_lesson, 0, PY_N_LESSONS);
    put_u16(buf + 9, p->last_card);
    for (int i = 0; i < PY_N_ITEMS; i++) {
        buf[11 + i] = clamp_u8(p->stars[i], 0, 3);
    }
    put_u16(buf + 74, p->quiz_attempts);
    put_u16(buf + 76, p->quiz_correct);
    for (int i = 0; i < PY_N_LESSONS; i++) {
        buf[78 + i] = clamp_u8(p->lesson_best[i], 0, 100);
    }
    put_u32(buf + 100, progress_crc32(buf, 100));
    return true;
}

bool progress_decode(const uint8_t *buf, size_t len, pinyin_progress_t *p)
{
    if (buf == NULL || p == NULL || len != PROGRESS_BLOB_SIZE) {
        return false;
    }
    if (get_u32(buf) != PROGRESS_MAGIC || get_u16(buf + 4) != PROGRESS_VERSION ||
        get_u16(buf + 6) != 92u) {
        return false;
    }
    if (get_u32(buf + 100) != progress_crc32(buf, 100)) {
        return false;
    }
    p->last_lesson = buf[8];
    p->last_card = get_u16(buf + 9);
    for (int i = 0; i < PY_N_ITEMS; i++) {
        p->stars[i] = buf[11 + i];
    }
    p->quiz_attempts = get_u16(buf + 74);
    p->quiz_correct = get_u16(buf + 76);
    for (int i = 0; i < PY_N_LESSONS; i++) {
        p->lesson_best[i] = buf[78 + i];
    }
    return true;
}
