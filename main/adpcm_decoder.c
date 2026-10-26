/* IMA-ADPCM 解码实现（Sun Microsystems adpcm.c 参考表，公版算法）。
 * 编码器（python）与解码器（本文件）由 tests/test_adpcm.c 的 golden 样本绑定。
 */
#include "adpcm_decoder.h"

#include <string.h>

static const int16_t k_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
};

static const int8_t k_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static int16_t decode_nibble(adpcm_ima_t *d, uint8_t code)
{
    int step = k_step_table[d->step_index];
    int diff = step >> 3;
    if (code & 4) {
        diff += step;
    }
    if (code & 2) {
        diff += step >> 1;
    }
    if (code & 1) {
        diff += step >> 2;
    }
    int predictor = d->predictor + ((code & 8) ? -diff : diff);
    d->predictor = (int16_t)clamp_int(predictor, -32768, 32767);
    d->step_index = (uint8_t)clamp_int((int)d->step_index + k_index_table[code], 0, 88);
    return d->predictor;
}

_Bool adpcm_ima_init(adpcm_ima_t *d, const uint8_t *data, size_t len)
{
    if (d == NULL || data == NULL || len < 4) {
        return 0;
    }
    d->data = data;
    d->len = len;
    d->pos = 4;
    d->predictor = (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
    d->step_index = data[2] > 88 ? 88 : data[2];
    d->nibble_high = 0;
    d->head_pending = 1; /* predictor 即首样本 */
    return 1;
}

size_t adpcm_ima_read(adpcm_ima_t *d, int16_t *out, size_t max_samples)
{
    if (d == NULL || out == NULL || d->data == NULL) {
        return 0;
    }
    size_t n = 0;
    if (d->head_pending) {
        if (max_samples == 0) {
            return 0;
        }
        out[n++] = d->predictor;
        d->head_pending = 0;
    }
    while (n < max_samples && d->pos < d->len) {
        uint8_t byte = d->data[d->pos];
        uint8_t code = d->nibble_high ? (uint8_t)(byte >> 4) : (uint8_t)(byte & 0x0F);
        out[n++] = decode_nibble(d, code);
        if (d->nibble_high) {
            d->pos++;
            d->nibble_high = 0;
        } else {
            d->nibble_high = 1;
        }
    }
    return n;
}
