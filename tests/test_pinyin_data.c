/* pinyin_data 表完整性测试。*/
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pinyin_data.h"

static void test_counts(void)
{
    assert(py_item_count() == 63);
    assert(py_word_count() >= 100 && py_word_count() <= 160);
    /* 课内条目计数与总和 */
    uint16_t total = 0;
    for (uint8_t l = 1; l <= 13; l++) {
        uint16_t n = py_item_lesson_count(l);
        assert(n > 0);
        total += n;
    }
    assert(total == 63);
    assert(py_item_lesson_count(0) == 0);
    assert(py_item_lesson_count(14) == 0);
    assert(py_item_first_in_lesson(0) == -1);
    assert(py_item_first_in_lesson(14) == -1);
    int16_t first = py_item_first_in_lesson(1);
    assert(first == 0);
    assert(py_item(0)->lesson == 1);
}

static void test_items_unique_and_consistent(void)
{
    bool seen_ini[PY_N_INITIALS] = {0};
    bool seen_fin[PY_N_FINALS] = {0};
    bool seen_whole[PY_N_WHOLE] = {0};
    for (uint16_t i = 0; i < py_item_count(); i++) {
        const py_item_t *it = py_item(i);
        assert(it != NULL);
        assert(it->lesson >= 1 && it->lesson <= 13);
        switch (it->kind) {
        case PY_K_INITIAL:
            assert(it->initial_id >= 0 && it->initial_id < PY_N_INITIALS);
            assert(it->final_id < 0 && it->whole_id < 0);
            assert(!seen_ini[it->initial_id]);
            seen_ini[it->initial_id] = true;
            break;
        case PY_K_FINAL:
            assert(it->final_id >= 0 && it->final_id < PY_N_FINALS);
            assert(it->initial_id < 0 && it->whole_id < 0);
            assert(!seen_fin[it->final_id]);
            seen_fin[it->final_id] = true;
            break;
        case PY_K_WHOLE:
            assert(it->whole_id >= 0 && it->whole_id < PY_N_WHOLE);
            assert(it->initial_id < 0 && it->final_id < 0);
            assert(!seen_whole[it->whole_id]);
            seen_whole[it->whole_id] = true;
            break;
        default:
            assert(0);
        }
    }
    for (int i = 0; i < PY_N_INITIALS; i++) {
        assert(seen_ini[i]);
    }
    for (int i = 0; i < PY_N_FINALS; i++) {
        assert(seen_fin[i]);
    }
    for (int i = 0; i < PY_N_WHOLE; i++) {
        assert(seen_whole[i]);
    }
    assert(py_item(63) == NULL);
}

static void test_word_pool(void)
{
    for (uint16_t i = 0; i < py_word_count(); i++) {
        const py_word_t *w = py_word(i);
        assert(w != NULL && w->hanzi != NULL && w->hanzi[0] != '\0');
        assert(w->tone >= 1 && w->tone <= 4);
        int fields = (w->initial_id >= 0) + (w->final_id >= 0) + (w->whole_id >= 0);
        if (w->whole_id >= 0) {
            assert(fields == 1);
            assert(w->medial_id < 0);
        } else {
            assert(w->final_id >= 0);
            assert(fields >= 1);
        }
        uint8_t lesson = py_word_lesson(i);
        assert(lesson >= 1 && lesson <= 13);
    }
    assert(py_word(py_word_count()) == NULL);
    /* 具体词例（下标按词表顺序）：衣(4) whole-yi 归第 2 课；爸(11) b-a-4 归第 3 课；
     * 果(32) g-u-o 三拼归第 5 课 */
    assert(py_word_lesson(4) == 2);
    assert(py_word_lesson(11) == 3);
    assert(py_word_lesson(32) == 5);
}

static void test_lookups(void)
{
    assert(py_initial_id("b") == 0);
    assert(py_initial_id("zh") == 14);
    assert(py_initial_id("w") == 22);
    assert(py_initial_id("xx") == -1);
    assert(py_final_id("a") == 0);
    assert(py_final_id("ü") == 5);
    assert(py_final_id("ün") == 19);
    assert(py_final_id("ong") == 23);
    assert(py_final_id("iou") == -1);
    assert(py_whole_id("zhi") == 0);
    assert(py_whole_id("ying") == 15);
    assert(py_whole_id("zhi ") == -1);
    assert(strcmp(py_lesson_title(1), "第1课 a o e") == 0);
    assert(strstr(py_lesson_title(11), "üe") != NULL);
    assert(py_lesson_title(0) == NULL);
    assert(py_lesson_desc(13) != NULL);
    /* 呼读音表：zh→zhī，r→rì，y→yī */
    assert(strcmp(py_initial_calls[14], "zhī") == 0);
    assert(strcmp(py_initial_calls[17], "rì") == 0);
    assert(strcmp(py_initial_calls[21], "yī") == 0);
}

static void test_words_for_item(void)
{
    uint16_t idx[4];
    /* 声母 b（条目下标 11，第 3 课）有例词 */
    uint8_t n = py_words_for_item(11, idx, 4);
    assert(n >= 2);
    for (uint8_t i = 0; i < n; i++) {
        assert(py_word(idx[i])->initial_id == 0);
    }
    /* 越界 */
    assert(py_words_for_item(63, idx, 4) == 0);
}

int main(void)
{
    test_counts();
    test_items_unique_and_consistent();
    test_word_pool();
    test_lookups();
    test_words_for_item();
    printf("test_pinyin_data: PASS\n");
    return 0;
}
