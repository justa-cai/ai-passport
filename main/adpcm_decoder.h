/* IMA-ADPCM 流式解码器：与 tools/gen_pinyin_audio.py 编码器严格互逆。
 * 纯逻辑模块：Sun 风格 IMA 表，clip 布局 = 4 字节头 + 连续 nibble（低 nibble 在前）。
 * 头部：int16 predictor（小端）、uint8 step_index、uint8 保留。
 */
#ifndef ADPCM_DECODER_H
#define ADPCM_DECODER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data; /* clip 起始（含 4 字节头）*/
    size_t len;          /* clip 总字节数（含头）*/
    size_t pos;          /* 当前字节位置 */
    int16_t predictor;
    uint8_t step_index;
    uint8_t nibble_high; /* 当前字节高半字节待读 */
    uint8_t head_pending; /* 头部 predictor 样本待输出 */
} adpcm_ima_t;

/* 初始化（消费 4 字节头）。data/len 非法返回 false。*/
_Bool adpcm_ima_init(adpcm_ima_t *d, const uint8_t *data, size_t len);

/* 解码最多 max_samples 个样本到 out，返回实际解码数；0 = 已到末尾。*/
size_t adpcm_ima_read(adpcm_ima_t *d, int16_t *out, size_t max_samples);

#endif /* ADPCM_DECODER_H */
