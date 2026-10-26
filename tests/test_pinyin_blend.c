/* 拼读引擎测试：标调穷举、ü 规则、合法性、组串、音频 id。*/
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pinyin_blend.h"
#include "pinyin_data.h"

static void test_tone_mark_pos(void)
{
    /* 有 a 标 a */
    assert(py_tone_mark_pos(py_final_id("a")) == 0);
    assert(py_tone_mark_pos(py_final_id("ai")) == 0);
    assert(py_tone_mark_pos(py_final_id("ao")) == 0);
    assert(py_tone_mark_pos(py_final_id("ang")) == 0);
    /* 没 a 找 o */
    assert(py_tone_mark_pos(py_final_id("o")) == 0);
    assert(py_tone_mark_pos(py_final_id("ou")) == 0);
    assert(py_tone_mark_pos(py_final_id("ong")) == 0);
    /* 没 a o 找 e */
    assert(py_tone_mark_pos(py_final_id("e")) == 0);
    assert(py_tone_mark_pos(py_final_id("ei")) == 0);
    assert(py_tone_mark_pos(py_final_id("ie")) == 1);
    assert(py_tone_mark_pos(py_final_id("üe")) == 2); /* ü 两字节(C3 BC)，e 在字节 2 */
    assert(py_tone_mark_pos(py_final_id("er")) == 0);
    assert(py_tone_mark_pos(py_final_id("en")) == 0);
    assert(py_tone_mark_pos(py_final_id("eng")) == 0);
    /* ü 单独（ü、ün）标 ü */
    assert(py_tone_mark_pos(py_final_id("ü")) == 0);
    assert(py_tone_mark_pos(py_final_id("ün")) == 0);
    /* i u 并列标在后 */
    assert(py_tone_mark_pos(py_final_id("i")) == 0);
    assert(py_tone_mark_pos(py_final_id("u")) == 0);
    assert(py_tone_mark_pos(py_final_id("ui")) == 1); /* i 在后 */
    assert(py_tone_mark_pos(py_final_id("iu")) == 1); /* u 在后 */
    assert(py_tone_mark_pos(py_final_id("in")) == 0);
    assert(py_tone_mark_pos(py_final_id("un")) == 0); /* u-n：u 标 */
    assert(py_tone_mark_pos(py_final_id("ing")) == 0);
}

static void test_compose_final(void)
{
    char buf[PY_BUF];
    assert(py_compose_final_marked(py_final_id("a"), 1, buf, sizeof(buf)) > 0);
    assert(strcmp(buf, "ā") == 0);
    py_compose_final_marked(py_final_id("a"), 3, buf, sizeof(buf));
    assert(strcmp(buf, "ǎ") == 0);
    py_compose_final_marked(py_final_id("üe"), 4, buf, sizeof(buf));
    assert(strcmp(buf, "üè") == 0);
    py_compose_final_marked(py_final_id("ui"), 4, buf, sizeof(buf));
    assert(strcmp(buf, "uì") == 0);
    py_compose_final_marked(py_final_id("ün"), 2, buf, sizeof(buf));
    assert(strcmp(buf, "ǘn") == 0);
    /* 越界参数 */
    assert(py_compose_final_marked(-1, 1, buf, sizeof(buf)) < 0);
    assert(py_compose_final_marked(0, 5, buf, sizeof(buf)) < 0);
    /* 缓冲不足 */
    char tiny[2];
    assert(py_compose_final_marked(py_final_id("üe"), 1, tiny, sizeof(tiny)) < 0);
}

static void test_compose_syllable(void)
{
    char buf[PY_BUF];
    assert(py_compose_syllable(py_initial_id("b"), py_final_id("a"), 4, buf, sizeof(buf)) > 0);
    assert(strcmp(buf, "bà") == 0);
    /* jqxy 去点：jù qù xǔ yù */
    py_compose_syllable(py_initial_id("j"), py_final_id("ü"), 4, buf, sizeof(buf));
    assert(strcmp(buf, "jù") == 0);
    py_compose_syllable(py_initial_id("q"), py_final_id("ü"), 4, buf, sizeof(buf));
    assert(strcmp(buf, "qù") == 0);
    py_compose_syllable(py_initial_id("x"), py_final_id("ü"), 3, buf, sizeof(buf));
    assert(strcmp(buf, "xǔ") == 0);
    py_compose_syllable(py_initial_id("y"), py_final_id("ü"), 4, buf, sizeof(buf));
    assert(strcmp(buf, "yù") == 0);
    /* ju(qu xu) 输入侧：u 视为 ü */
    assert(py_two_part_valid(py_initial_id("j"), py_final_id("u")));
    /* n l 保留两点：nǚ lǜ */
    py_compose_syllable(py_initial_id("n"), py_final_id("ü"), 3, buf, sizeof(buf));
    assert(strcmp(buf, "nǚ") == 0);
    py_compose_syllable(py_initial_id("l"), py_final_id("ü"), 4, buf, sizeof(buf));
    assert(strcmp(buf, "lǜ") == 0);
    /* 标调综合：duì liù lüè yún zhōng yīng */
    py_compose_syllable(py_initial_id("d"), py_final_id("ui"), 4, buf, sizeof(buf));
    assert(strcmp(buf, "duì") == 0);
    py_compose_syllable(py_initial_id("l"), py_final_id("iu"), 4, buf, sizeof(buf));
    assert(strcmp(buf, "liù") == 0);
    py_compose_syllable(py_initial_id("l"), py_final_id("üe"), 4, buf, sizeof(buf));
    assert(strcmp(buf, "lüè") == 0);
    py_compose_syllable(py_initial_id("y"), py_final_id("un"), 2, buf, sizeof(buf));
    assert(strcmp(buf, "yún") == 0);
    py_compose_syllable(py_initial_id("zh"), py_final_id("ong"), 1, buf, sizeof(buf));
    assert(strcmp(buf, "zhōng") == 0);
    py_compose_syllable(py_initial_id("y"), py_final_id("ing"), 1, buf, sizeof(buf));
    assert(strcmp(buf, "yīng") == 0);
}

static void test_compose_three_part(void)
{
    char buf[PY_BUF];
    assert(py_compose_syllable3(py_initial_id("g"), 1, py_final_id("a"), 1, buf, sizeof(buf)) > 0);
    assert(strcmp(buf, "guā") == 0);
    py_compose_syllable3(py_initial_id("g"), 1, py_final_id("o"), 3, buf, sizeof(buf));
    assert(strcmp(buf, "guǒ") == 0);
    py_compose_syllable3(py_initial_id("j"), 0, py_final_id("a"), 1, buf, sizeof(buf));
    assert(strcmp(buf, "jiā") == 0);
    py_compose_syllable3(py_initial_id("x"), 0, py_final_id("ao"), 4, buf, sizeof(buf));
    assert(strcmp(buf, "xiào") == 0);
    py_compose_syllable3(py_initial_id("j"), 2, py_final_id("an"), 4, buf, sizeof(buf));
    assert(strcmp(buf, "juàn") == 0);
    /* 无效参数 */
    assert(py_compose_syllable3(-1, 1, 0, 1, buf, sizeof(buf)) < 0);
    assert(py_compose_syllable3(0, 3, 0, 1, buf, sizeof(buf)) < 0);
}

static void test_compose_whole(void)
{
    char buf[PY_BUF];
    assert(py_compose_whole(py_whole_id("yu"), 4, buf, sizeof(buf)) > 0);
    assert(strcmp(buf, "yù") == 0);
    py_compose_whole(py_whole_id("yuan"), 2, buf, sizeof(buf));
    assert(strcmp(buf, "yuán") == 0);
    py_compose_whole(py_whole_id("ying"), 1, buf, sizeof(buf));
    assert(strcmp(buf, "yīng") == 0);
    py_compose_whole(py_whole_id("ri"), 4, buf, sizeof(buf));
    assert(strcmp(buf, "rì") == 0);
    py_compose_whole(py_whole_id("wu"), 1, buf, sizeof(buf));
    assert(strcmp(buf, "wū") == 0);
    py_compose_whole(py_whole_id("yue"), 4, buf, sizeof(buf));
    assert(strcmp(buf, "yuè") == 0);
}

static void test_validity(void)
{
    int8_t b = py_initial_id("b"), j = py_initial_id("j"), g = py_initial_id("g");
    int8_t zhx = py_initial_id("zh"), y = py_initial_id("y"), w = py_initial_id("w");
    int8_t a = py_final_id("a"), o = py_final_id("o"), u = py_final_id("u");
    int8_t ai = py_final_id("ai"), ong = py_final_id("ong"), ang = py_final_id("ang");
    assert(py_two_part_valid(b, a));
    assert(py_two_part_valid(b, o));
    assert(py_two_part_valid(zhx, ai));
    assert(py_two_part_valid(y, ong));  /* yōng */
    assert(py_two_part_valid(w, ang));  /* wáng */
    assert(!py_two_part_valid(b, ong)); /* bong 不存在 */
    assert(!py_two_part_valid(b, ang - 100)); /* 越界 */
    assert(!py_two_part_valid(j, a));   /* ja 不存在（jia 是三拼）*/
    assert(!py_two_part_valid(-1, a));
    assert(py_two_part_valid(j, u));    /* ju = jü 归一化 */
    assert(!py_two_part_valid(zhx, u - 5)); /* zh+i：整体认读，不分解 → zh 与 i 无两拼 */
    /* 三拼 */
    assert(py_three_part_valid(g, 1, a));
    assert(py_three_part_valid(g, 1, o));
    assert(!py_three_part_valid(b, 1, a));  /* bua 不存在 */
    assert(!py_three_part_valid(g, 0, py_final_id("e"))); /* g+i+e 不存在 */
    assert(!py_three_part_valid(g, 0, py_final_id("a"))); /* g+i+a 不存在 */
    /* 去点声母集合 */
    assert(py_initial_dots_u(py_initial_id("j")));
    assert(py_initial_dots_u(py_initial_id("q")));
    assert(py_initial_dots_u(py_initial_id("x")));
    assert(py_initial_dots_u(py_initial_id("y")));
    assert(!py_initial_dots_u(py_initial_id("n")));
    assert(!py_initial_dots_u(py_initial_id("l")));
    assert(!py_initial_dots_u(py_initial_id("w")));
}

static void test_compose_word(void)
{
    char buf[PY_BUF];
    /* 爸 b-a-4 → bà */
    const py_word_t *ba = NULL;
    for (uint16_t i = 0; i < py_word_count(); i++) {
        if (strcmp(py_word(i)->hanzi, "爸") == 0) {
            ba = py_word(i);
        }
    }
    assert(ba != NULL);
    assert(py_compose_word(ba, buf, sizeof(buf)) > 0);
    assert(strcmp(buf, "bà") == 0);
    /* 果 guǒ */
    for (uint16_t i = 0; i < py_word_count(); i++) {
        const py_word_t *w = py_word(i);
        if (strcmp(w->hanzi, "果") == 0) {
            assert(py_compose_word(w, buf, sizeof(buf)) > 0);
            assert(strcmp(buf, "guǒ") == 0);
        }
    }
}

static void test_audio_ids(void)
{
    char buf[PY_BUF];
    assert(py_aid_final(py_final_id("a"), 2, buf, sizeof(buf)) > 0);
    assert(strcmp(buf, "a2") == 0);
    py_aid_final(py_final_id("üe"), 4, buf, sizeof(buf));
    assert(strcmp(buf, "ve4") == 0);
    py_aid_final(py_final_id("ü"), 2, buf, sizeof(buf));
    assert(strcmp(buf, "v2") == 0);
    py_aid_final(py_final_id("ün"), 2, buf, sizeof(buf));
    assert(strcmp(buf, "vn2") == 0);
    /* 声母呼读 */
    py_aid_initial(py_initial_id("b"), buf, sizeof(buf));
    assert(strcmp(buf, "bo1") == 0);
    py_aid_initial(py_initial_id("zh"), buf, sizeof(buf));
    assert(strcmp(buf, "zhi1") == 0);
    py_aid_initial(py_initial_id("r"), buf, sizeof(buf));
    assert(strcmp(buf, "ri4") == 0);
    /* 整体认读：zhi1 与声母 zh 呼读同 id（同一 clip）*/
    py_aid_whole(py_whole_id("zhi"), 1, buf, sizeof(buf));
    assert(strcmp(buf, "zhi1") == 0);
    py_aid_whole(py_whole_id("yu"), 4, buf, sizeof(buf));
    assert(strcmp(buf, "yu4") == 0);
    /* 词 id：爸 ba4、果 guo3、去 qu4（去点后写 u）、裙 qun2、鱼 yu2 */
    const py_word_t *qu = NULL, *qun = NULL, *yu = NULL;
    for (uint16_t i = 0; i < py_word_count(); i++) {
        const py_word_t *w = py_word(i);
        if (strcmp(w->hanzi, "去") == 0) {
            qu = w;
        }
        if (strcmp(w->hanzi, "裙") == 0) {
            qun = w;
        }
        if (strcmp(w->hanzi, "鱼") == 0) {
            yu = w;
        }
    }
    assert(qu && qun && yu);
    py_aid_word(qu, buf, sizeof(buf));
    assert(strcmp(buf, "qu4") == 0);
    py_aid_word(qun, buf, sizeof(buf));
    assert(strcmp(buf, "qun2") == 0);
    py_aid_word(yu, buf, sizeof(buf));
    assert(strcmp(buf, "yu2") == 0);
}

int main(void)
{
    test_tone_mark_pos();
    test_compose_final();
    test_compose_syllable();
    test_compose_three_part();
    test_compose_whole();
    test_validity();
    test_compose_word();
    test_audio_ids();
    printf("test_pinyin_blend: PASS\n");
    return 0;
}
