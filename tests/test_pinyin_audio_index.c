/* 音频索引测试：C 侧枚举的全部 aid 必须命中生成器产出的索引表。
 * 绑定 tools/gen_pinyin_audio.py（python 枚举）与 main/pinyin_blend.c（C 构造），
 * 两侧规则漂移在此暴露。
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pinyin_audio_index.h"
#include "pinyin_blend.h"
#include "pinyin_data.h"

static void assert_found(const char *aid)
{
    const py_audio_entry_t *e = py_audio_lookup(aid);
    if (e == NULL) {
        printf("MISSING aid=%s\n", aid);
    }
    assert(e != NULL);
    assert(e->length >= 5); /* 4B 头 + 至少 1 字节 */
}

int main(void)
{
    /* 韵母 ×4 声 */
    for (int8_t f = 0; f < PY_N_FINALS; f++) {
        for (uint8_t t = 1; t <= 4; t++) {
            char aid[PY_BUF];
            assert(py_aid_final(f, t, aid, sizeof(aid)) > 0);
            assert_found(aid);
        }
    }
    /* 声母呼读 */
    for (int8_t i = 0; i < PY_N_INITIALS; i++) {
        char aid[PY_BUF];
        assert(py_aid_initial(i, aid, sizeof(aid)) > 0);
        assert_found(aid);
    }
    /* 整体认读 ×4 声 */
    for (int8_t w = 0; w < PY_N_WHOLE; w++) {
        for (uint8_t t = 1; t <= 4; t++) {
            char aid[PY_BUF];
            assert(py_aid_whole(w, t, aid, sizeof(aid)) > 0);
            assert_found(aid);
        }
    }
    /* 例词池 */
    for (uint16_t i = 0; i < py_word_count(); i++) {
        char aid[PY_BUF];
        assert(py_aid_word(py_word(i), aid, sizeof(aid)) > 0);
        assert_found(aid);
    }
    /* 索引表排序（二分查找前提）与唯一性 */
    for (size_t i = 1; i < k_pinyin_audio_count; i++) {
        assert(strcmp(k_pinyin_audio_entries[i - 1].id, k_pinyin_audio_entries[i].id) < 0);
    }
    /* 未命中样例 */
    assert(py_audio_lookup("zz9") == NULL);
    assert(py_audio_lookup("") == NULL);
    assert(py_audio_lookup(NULL) == NULL);
    printf("test_pinyin_audio_index: PASS (%zu clips)\n", k_pinyin_audio_count);
    return 0;
}
