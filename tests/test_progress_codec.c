/* 进度 blob 编解码测试。*/
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "progress_codec.h"

static void test_roundtrip(void)
{
    pinyin_progress_t p;
    pinyin_progress_defaults(&p);
    p.last_lesson = 7;
    p.last_card = 42;
    for (int i = 0; i < PY_N_ITEMS; i++) {
        p.stars[i] = (uint8_t)(i % 4);
    }
    p.quiz_attempts = 120;
    p.quiz_correct = 87;
    for (int i = 0; i < PY_N_LESSONS; i++) {
        p.lesson_best[i] = (uint8_t)(50 + i);
    }
    uint8_t blob[PROGRESS_BLOB_SIZE];
    assert(progress_encode(&p, blob, sizeof(blob)));
    pinyin_progress_t out;
    assert(progress_decode(blob, sizeof(blob), &out));
    assert(out.last_lesson == 7);
    assert(out.last_card == 42);
    assert(out.quiz_attempts == 120);
    assert(out.quiz_correct == 87);
    for (int i = 0; i < PY_N_ITEMS; i++) {
        assert(out.stars[i] == i % 4);
    }
    for (int i = 0; i < PY_N_LESSONS; i++) {
        assert(out.lesson_best[i] == 50 + i);
    }
}

static void test_clamping(void)
{
    pinyin_progress_t p;
    pinyin_progress_defaults(&p);
    p.last_lesson = 99;
    p.stars[0] = 200;
    p.lesson_best[0] = 250;
    uint8_t blob[PROGRESS_BLOB_SIZE];
    assert(progress_encode(&p, blob, sizeof(blob)));
    pinyin_progress_t out;
    assert(progress_decode(blob, sizeof(blob), &out));
    assert(out.last_lesson == 13); /* clamp 到 13 */
    assert(out.stars[0] == 3);
    assert(out.lesson_best[0] == 100);
}

static void test_corruption(void)
{
    pinyin_progress_t p;
    pinyin_progress_defaults(&p);
    uint8_t blob[PROGRESS_BLOB_SIZE];
    assert(progress_encode(&p, blob, sizeof(blob)));
    pinyin_progress_t out;
    /* 翻转数据位 → CRC 失败 */
    blob[20] ^= 0x01;
    assert(!progress_decode(blob, sizeof(blob), &out));
    blob[20] ^= 0x01;
    /* 翻转 CRC 位 */
    blob[100] ^= 0xFF;
    assert(!progress_decode(blob, sizeof(blob), &out));
    blob[100] ^= 0xFF;
    assert(progress_decode(blob, sizeof(blob), &out));
    /* 错误 magic */
    blob[0] = 'X';
    assert(!progress_decode(blob, sizeof(blob), &out));
    /* 错误长度 */
    assert(!progress_decode(blob, sizeof(blob) - 1, &out));
    assert(!progress_decode(blob, sizeof(blob) + 4, &out));
    assert(!progress_decode(NULL, sizeof(blob), &out));
    /* NULL 参数 */
    assert(!progress_encode(NULL, blob, sizeof(blob)));
    assert(!progress_encode(&p, NULL, sizeof(blob)));
    assert(!progress_encode(&p, blob, 8));
}

static void test_defaults_and_crc(void)
{
    pinyin_progress_t p;
    pinyin_progress_defaults(&p);
    assert(p.last_lesson == 1);
    assert(p.last_card == 0);
    assert(p.quiz_attempts == 0 && p.quiz_correct == 0);
    /* CRC32 已知向量："123456789" → 0xCBF43926 */
    assert(progress_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);
    assert(progress_crc32(NULL, 0) == 0x00000000u);
}

int main(void)
{
    test_roundtrip();
    test_clamping();
    test_corruption();
    test_defaults_and_crc();
    printf("test_progress_codec: PASS\n");
    return 0;
}
