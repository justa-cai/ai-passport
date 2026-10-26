/* 由 tools/gen_pinyin_audio.py 生成——勿手改。*/
#ifndef PINYIN_AUDIO_INDEX_H
#define PINYIN_AUDIO_INDEX_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *id;    /* ASCII 拼音+声调数字（ü→v；jqxy 后 ü 写 u）*/
    uint32_t offset;   /* blob 内偏移（含 4 字节 ADPCM 头）*/
    uint32_t length;   /* ADPCM 字节数（含头）*/
    uint16_t samples;  /* PCM 样本数（16 kHz）*/
} py_audio_entry_t;

extern const py_audio_entry_t k_pinyin_audio_entries[];
extern const size_t k_pinyin_audio_count;

/* 二分查找；未命中返回 NULL。*/
const py_audio_entry_t *py_audio_lookup(const char *id);

#endif /* PINYIN_AUDIO_INDEX_H */
