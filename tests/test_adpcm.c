/* ADPCM 解码器测试：golden 样本（tools/gen_pinyin_audio.py 生成）逐样本精确解码。
 * golden PCM 为“重构值”（编码器跟踪解码器状态），因此比对必须严格相等。
 */
#include <assert.h>
#include <stdio.h>

#include "adpcm_golden.h"
#include "adpcm_decoder.h"

int main(void)
{
    assert(k_golden_adpcm_len >= 5);
    assert(k_golden_pcm_len >= 1);
    assert(k_golden_pcm_len == 1 + 2 * (k_golden_adpcm_len - 4)); /* 无补位幻影样本 */

    adpcm_ima_t d;
    assert(adpcm_ima_init(&d, k_golden_adpcm, k_golden_adpcm_len));

    /* 分块解码（固件按 256 样本块播放），拼接后整体比对 */
    static int16_t out[65536];
    assert((size_t)k_golden_pcm_len <= sizeof(out) / sizeof(out[0]));
    size_t got = 0;
    for (;;) {
        size_t n = adpcm_ima_read(&d, out + got, 256);
        got += n;
        if (n == 0) {
            break;
        }
    }
    assert(got == (size_t)k_golden_pcm_len);
    for (size_t i = 0; i < got; i++) {
        if (out[i] != k_golden_pcm[i]) {
            printf("MISMATCH @%zu: got %d want %d\n", i, out[i], k_golden_pcm[i]);
            assert(0);
        }
    }

    /* 头部防御：非法指针/过短 clip */
    assert(!adpcm_ima_init(&d, NULL, 10));
    assert(!adpcm_ima_init(&d, k_golden_adpcm, 3));
    /* step_index 头字段钳制：0x80 → 88 */
    uint8_t bad_hdr[8] = {0, 0, 0x80, 0, 0, 0, 0, 0};
    assert(adpcm_ima_init(&d, bad_hdr, sizeof(bad_hdr)));
    assert(d.step_index == 88);

    printf("test_adpcm: PASS (%zu samples exact)\n", (size_t)k_golden_pcm_len);
    return 0;
}
