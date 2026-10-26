/* 拼读引擎实现。
 * 标调规则（课本口诀"有 a 不放过，没 a 找 o e，i u 并列标在后"）：
 *   对书面韵母字母串按优先级扫描：a > o > e > ü > （仅剩 i/u 时）最后一个 i/u。
 * ü 去点规则：j q x y 与 ü 相拼时两点省略（先去点再标调，标记落在普通 u 上）；
 *   n l 保留两点（nǚ lǜ）。
 * 音频 id 为发音的 ASCII 拼音 + 声调数字（ü→v；jqxy 后写 u），相同 id 即相同发音，
 * 声母呼读 zhi1/chi1/.../yi1/wu1 与对应整体认读音节 1 声 id 天然一致，按同一 clip 去重。
 */
#include "pinyin_blend.h"

#include <string.h>

/* 两拼合法表：每行一个声母可拼的韵母下标（-1 结尾）。
 * 依据普通话音节表（一年级教材范围；边际音节 zhei/zei/bou 等不收）。*/
static const int8_t k_two_part[PY_N_INITIALS][18] = {
    /* b  */ {0, 1, 3, 4, 6, 7, 9, 15, 16, 20, 21, -1},
    /* p  */ {0, 1, 3, 4, 6, 7, 9, 10, 15, 16, 20, 21, -1},
    /* m  */ {0, 1, 3, 4, 6, 7, 9, 10, 15, 16, 20, 21, -1},
    /* f  */ {0, 1, 4, 7, 10, 15, 16, 20, 21, -1},
    /* d  */ {0, 1, 2, 3, 4, 6, 7, 9, 10, 11, 15, 16, 20, 21, 23, -1},
    /* t  */ {0, 1, 2, 3, 4, 6, 9, 10, 15, 18, 20, 21, 23, -1},
    /* n  */ {0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 15, 16, 17, 20, 21, 23, -1},
    /* l  */ {0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 15, 18, 17, 20, 21, 23, -1},
    /* g  */ {0, 1, 2, 4, 6, 7, 9, 10, 15, 16, 20, 21, 23, -1},
    /* k  */ {0, 1, 2, 4, 6, 7, 9, 10, 15, 16, 20, 21, 23, -1},
    /* h  */ {0, 1, 2, 4, 6, 7, 9, 10, 15, 16, 20, 21, 23, -1},
    /* j  */ {3, 5, 12, 13, 17, 22, 19, -1},
    /* q  */ {3, 5, 12, 13, 17, 22, 19, -1},
    /* x  */ {3, 5, 12, 13, 17, 22, 19, -1},
    /* zh */ {0, 2, 4, 6, 9, 10, 8, 15, 16, 20, 21, 23, -1},
    /* ch */ {0, 2, 4, 6, 9, 10, 8, 15, 16, 20, 21, 23, -1},
    /* sh */ {0, 2, 4, 6, 9, 10, 8, 15, 16, 20, 21, -1},
    /* r  */ {2, 4, 9, 10, 16, 20, 21, 23, -1},
    /* z  */ {0, 2, 4, 6, 9, 10, 8, 15, 16, 20, 21, 23, -1},
    /* c  */ {0, 2, 4, 6, 9, 10, 8, 15, 16, 20, 21, 23, -1},
    /* s  */ {0, 2, 4, 6, 9, 10, 8, 15, 16, 20, 21, 23, -1},
    /* y  */ {0, 9, 15, 20, 23, 10, -1},
    /* w  */ {0, 1, 6, 7, 15, 16, 20, 21, -1},
};

/* 三拼合法表 {声母, 介音(0=i,1=u,2=ü), 韵母}。*/
static const int8_t k_three_part[][3] = {
    {8, 1, 0},  /* gua */  {8, 1, 1},  /* guo */  {9, 1, 0},  /* kua */  {9, 1, 1},  /* kuo */
    {10, 1, 0}, /* hua */  {10, 1, 1}, /* huo */
    {8, 1, 6},  /* guai */ {9, 1, 6},  /* kuai */ {10, 1, 6}, /* huai */
    {8, 1, 15}, /* guan */ {9, 1, 15}, /* kuan */ {10, 1, 15}, /* huan */
    {4, 1, 1},  /* duo */  {5, 1, 1},  /* tuo */  {6, 1, 1},  /* nuo */  {7, 1, 1}, /* luo */
    {4, 1, 15}, /* duan */ {5, 1, 15}, /* tuan */ {6, 1, 15}, /* nuan */ {7, 1, 15}, /* luan */
    {18, 1, 1}, /* zuo */  {19, 1, 1}, /* cuo */  {20, 1, 1}, /* suo */
    {18, 1, 15}, /* zuan */ {19, 1, 15}, /* cuan */ {20, 1, 15}, /* suan */
    {14, 1, 0}, /* zhua */ {14, 1, 1}, /* zhuo */ {14, 1, 15}, /* zhuan */
    {15, 1, 1}, /* chuo */ {16, 1, 0}, /* shua */ {16, 1, 1}, /* shuo */
    {17, 1, 1}, /* ruo */
    {0, 0, 9},  /* biao */ {0, 0, 15}, /* bian */
    {1, 0, 9},  /* piao */ {1, 0, 15}, /* pian */
    {2, 0, 9},  /* miao */ {2, 0, 15}, /* mian */
    {4, 0, 9},  /* diao */ {4, 0, 15}, /* dian */
    {5, 0, 9},  /* tiao */ {5, 0, 15}, /* tian */
    {6, 0, 9},  /* niao */ {6, 0, 15}, /* nian */
    {7, 0, 9},  /* liao */ {7, 0, 15}, /* lian */
    {11, 0, 0}, /* jia */  {11, 0, 9}, /* jiao */ {11, 0, 15}, /* jian */ {11, 0, 20}, /* jiang */
    {12, 0, 0}, /* qia */  {12, 0, 9}, /* qiao */ {12, 0, 15}, /* qian */ {12, 0, 20}, /* qiang */
    {13, 0, 0}, /* xia */  {13, 0, 9}, /* xiao */ {13, 0, 15}, /* xian */ {13, 0, 20}, /* xiang */
    {13, 0, 23}, /* xiong */
    {6, 0, 20}, /* niang */ {7, 0, 20}, /* liang */
    {11, 2, 15}, /* juan */ {12, 2, 15}, /* quan */ {13, 2, 15}, /* xuan */
};

#define PY_N_THREE_PART (sizeof(k_three_part) / sizeof(k_three_part[0]))

/* 声母呼读音频 id（与 py_initial_calls 一一对应）。*/
static const char *const k_initial_aids[PY_N_INITIALS] = {
    "bo1", "po1", "mo1", "fo1",
    "de1", "te1", "ne1", "le1",
    "ge1", "ke1", "he1",
    "ji1", "qi1", "xi1",
    "zhi1", "chi1", "shi1", "ri4",
    "zi1", "ci1", "si1",
    "yi1", "wu1",
};

/* NFC 带调元音表 [字母][声调 1..4]，行序 a o e i u ü。*/
static const char *const k_marked[6][4] = {
    {"ā", "á", "ǎ", "à"},
    {"ō", "ó", "ǒ", "ò"},
    {"ē", "é", "ě", "è"},
    {"ī", "í", "ǐ", "ì"},
    {"ū", "ú", "ǔ", "ù"},
    {"ǖ", "ǘ", "ǚ", "ǜ"},
};

static int letter_row(char c)
{
    switch (c) {
    case 'a': return 0;
    case 'o': return 1;
    case 'e': return 2;
    case 'i': return 3;
    case 'u': return 4;
    default: return -1;
    }
}

bool py_initial_dots_u(int8_t initial_id)
{
    /* j(11) q(12) x(13) y(21) */
    return initial_id == 11 || initial_id == 12 || initial_id == 13 || initial_id == 21;
}

bool py_two_part_valid(int8_t initial_id, int8_t final_id)
{
    if (initial_id < 0 || initial_id >= PY_N_INITIALS || final_id < 0 || final_id >= PY_N_FINALS) {
        return false;
    }
    /* j q x y 与 u 相拼视为 ü（输入侧归一化：ju qu xu yu 即 jü qü xü yü）*/
    if (py_initial_dots_u(initial_id) && final_id == 4) {
        final_id = 5;
    }
    for (int i = 0; i < 18; i++) {
        if (k_two_part[initial_id][i] == final_id) {
            return true;
        }
        if (k_two_part[initial_id][i] < 0) {
            break;
        }
    }
    return false;
}

bool py_three_part_valid(int8_t initial_id, int8_t medial, int8_t final_id)
{
    if (initial_id < 0 || medial < 0 || final_id < 0) {
        return false;
    }
    for (size_t i = 0; i < PY_N_THREE_PART; i++) {
        if (k_three_part[i][0] == initial_id && k_three_part[i][1] == medial &&
            k_three_part[i][2] == final_id) {
            return true;
        }
    }
    return false;
}

/* 返回书面韵母（应用 ü 去点）拷贝到 out，返回长度；用于标调与 id。*/
static int written_final(int8_t initial_id, int8_t final_id, char *out, size_t cap)
{
    const char *f = py_finals[final_id];
    size_t len = strlen(f);
    if (len + 1 > cap) {
        return -1;
    }
    memcpy(out, f, len + 1);
    /* ü 的 UTF-8 为 C3 BC；j q x y 后首字母 ü → u（仅替换首字母）*/
    if (py_initial_dots_u(initial_id) && len >= 2 &&
        (unsigned char)out[0] == 0xC3 && (unsigned char)out[1] == 0xBC) {
        out[0] = 'u';
        memmove(&out[1], &out[2], len - 1); /* 含 NUL */
        len -= 1;
    }
    return (int)len;
}

/* 标调位置扫描：a > o > e > ü > 最后一个 i/u；返回 -1 未找到。*/
static int scan_mark_pos(const char *s, int len)
{
    static const char priority[] = {'a', 'o', 'e'};
    for (size_t k = 0; k < sizeof(priority); k++) {
        for (int i = 0; i < len; i++) {
            if (s[i] == priority[k]) {
                return i;
            }
        }
    }
    for (int i = 0; i < len; i++) {
        if ((unsigned char)s[i] == 0xC3) { /* ü 首字节 */
            return i;
        }
    }
    int last = -1;
    for (int i = 0; i < len; i++) {
        if (s[i] == 'i' || s[i] == 'u') {
            last = i;
        }
    }
    return last;
}

uint8_t py_tone_mark_pos(int8_t final_id)
{
    char f[8];
    int len = written_final(-1, final_id, f, sizeof(f));
    if (len <= 0) {
        return 0;
    }
    int pos = scan_mark_pos(f, len);
    return (uint8_t)(pos < 0 ? 0 : pos);
}

/* 在书面韵母串上标调：返回新串字节数（不含 NUL）。*/
static int mark_tone(int8_t initial_id, int8_t final_id, uint8_t tone, char *buf, size_t cap)
{
    char f[8];
    int len = written_final(initial_id, final_id, f, sizeof(f));
    if (len <= 0 || tone < 1 || tone > 4) {
        return -1;
    }
    /* 在（可能已去点的）书面韵母串上按优先级扫描标调位置 */
    int mark_pos = scan_mark_pos(f, len);
    if (mark_pos < 0) {
        return -1;
    }
    int row = letter_row(f[mark_pos]);
    if (row < 0) {
        /* ü（C3 BC）→ 行 5 */
        if ((unsigned char)f[mark_pos] == 0xC3) {
            row = 5;
        } else {
            return -1;
        }
    }
    const char *marked = k_marked[row][tone - 1];
    size_t mlen = strlen(marked);
    /* 原 mark_pos 处字母占 1 字节（ASCII）或 2 字节（ü）*/
    size_t consumed = ((unsigned char)f[mark_pos] == 0xC3) ? 2 : 1;
    size_t out_len = (size_t)len - consumed + mlen;
    if (out_len + 1 > cap) {
        return -1;
    }
    memcpy(buf, f, (size_t)mark_pos);
    memcpy(buf + mark_pos, marked, mlen);
    memcpy(buf + mark_pos + mlen, f + mark_pos + consumed, (size_t)len - (size_t)mark_pos - consumed);
    buf[out_len] = '\0';
    return (int)out_len;
}

int py_compose_final_marked(int8_t final_id, uint8_t tone, char *buf, size_t cap)
{
    if (final_id < 0 || final_id >= PY_N_FINALS || buf == NULL) {
        return -1;
    }
    return mark_tone(-1, final_id, tone, buf, cap);
}

int py_compose_syllable(int8_t initial_id, int8_t final_id, uint8_t tone,
                        char *buf, size_t cap)
{
    if (initial_id < 0 || initial_id >= PY_N_INITIALS || final_id < 0 ||
        final_id >= PY_N_FINALS || buf == NULL) {
        return -1;
    }
    char marked[8];
    int mlen = mark_tone(initial_id, final_id, tone, marked, sizeof(marked));
    if (mlen < 0) {
        return -1;
    }
    const char *ini = py_initials[initial_id];
    size_t ilen = strlen(ini);
    if (ilen + (size_t)mlen + 1 > cap) {
        return -1;
    }
    memcpy(buf, ini, ilen);
    memcpy(buf + ilen, marked, (size_t)mlen + 1);
    return (int)(ilen + (size_t)mlen);
}

int py_compose_syllable3(int8_t initial_id, int8_t medial, int8_t final_id,
                         uint8_t tone, char *buf, size_t cap)
{
    if (initial_id < 0 || initial_id >= PY_N_INITIALS || medial < 0 || medial > 2 ||
        final_id < 0 || final_id >= PY_N_FINALS || buf == NULL) {
        return -1;
    }
    char marked[8];
    int mlen = mark_tone(initial_id, final_id, tone, marked, sizeof(marked));
    if (mlen < 0) {
        return -1;
    }
    static const char *const medials[] = {"i", "u", "ü"};
    const char *ini = py_initials[initial_id];
    /* 介音 ü 在 j q x y 后同样去两点（juan quan xuan）*/
    const char *med = (medial == 2 && py_initial_dots_u(initial_id)) ? "u" : medials[medial];
    size_t ilen = strlen(ini), dlen = strlen(med);
    if (ilen + dlen + (size_t)mlen + 1 > cap) {
        return -1;
    }
    memcpy(buf, ini, ilen);
    memcpy(buf + ilen, med, dlen);
    memcpy(buf + ilen + dlen, marked, (size_t)mlen + 1);
    return (int)(ilen + dlen + (size_t)mlen);
}

int py_compose_whole(int8_t whole_id, uint8_t tone, char *buf, size_t cap)
{
    if (whole_id < 0 || whole_id >= PY_N_WHOLE || buf == NULL) {
        return -1;
    }
    const char *base = py_whole[whole_id];
    size_t len = strlen(base);
    int mark_pos = scan_mark_pos(base, (int)len);
    if (mark_pos < 0) {
        return -1;
    }
    int row = letter_row(base[mark_pos]);
    if (row < 0) {
        if ((unsigned char)base[mark_pos] == 0xC3) {
            row = 5;
        } else {
            return -1;
        }
    }
    if (tone < 1 || tone > 4) {
        return -1;
    }
    const char *marked = k_marked[row][tone - 1];
    size_t mlen = strlen(marked);
    size_t consumed = ((unsigned char)base[mark_pos] == 0xC3) ? 2 : 1;
    size_t out_len = len - consumed + mlen;
    if (out_len + 1 > cap) {
        return -1;
    }
    memcpy(buf, base, (size_t)mark_pos);
    memcpy(buf + mark_pos, marked, mlen);
    memcpy(buf + mark_pos + mlen, base + (size_t)mark_pos + consumed,
           len - (size_t)mark_pos - consumed);
    buf[out_len] = '\0';
    return (int)out_len;
}

int py_compose_word(const py_word_t *w, char *buf, size_t cap)
{
    if (w == NULL || buf == NULL) {
        return -1;
    }
    if (w->whole_id >= 0) {
        return py_compose_whole(w->whole_id, w->tone, buf, cap);
    }
    if (w->final_id < 0) {
        return -1;
    }
    if (w->medial_id >= 0) {
        return py_compose_syllable3(w->initial_id, w->medial_id, w->final_id, w->tone, buf, cap);
    }
    if (w->initial_id >= 0) {
        return py_compose_syllable(w->initial_id, w->final_id, w->tone, buf, cap);
    }
    return py_compose_final_marked(w->final_id, w->tone, buf, cap);
}

/* ---- 音频 id ---- */

static int ascii_append(char *buf, size_t cap, size_t *len, const char *s)
{
    size_t slen = strlen(s);
    if (*len + slen + 1 > cap) {
        return -1;
    }
    memcpy(buf + *len, s, slen + 1);
    *len += slen;
    return 0;
}

/* ASCII 化韵母：ü→v（jqxy 去点后已是 u）。*/
static int ascii_final(int8_t initial_id, int8_t final_id, char *buf, size_t cap)
{
    char f[8];
    int len = written_final(initial_id, final_id, f, sizeof(f));
    if (len <= 0) {
        return -1;
    }
    size_t o = 0;
    for (int i = 0; i < len; i++) {
        char c = f[i];
        if ((unsigned char)c == 0xC3 && i + 1 < len && (unsigned char)f[i + 1] == 0xBC) {
            if (o + 2 > cap - 1) {
                return -1;
            }
            buf[o++] = 'v';
            i++;
        } else {
            if (o + 1 > cap - 1) {
                return -1;
            }
            buf[o++] = c;
        }
    }
    buf[o] = '\0';
    return (int)o;
}

static int append_tone_digit(char *buf, size_t cap, size_t *len, uint8_t tone)
{
    if (tone < 1 || tone > 4 || *len + 2 > cap) {
        return -1;
    }
    buf[(*len)++] = '0' + tone;
    buf[*len] = '\0';
    return 0;
}

int py_aid_final(int8_t final_id, uint8_t tone, char *buf, size_t cap)
{
    if (buf == NULL) {
        return -1;
    }
    size_t len = 0;
    if (ascii_final(-1, final_id, buf, cap) < 0) {
        return -1;
    }
    len = strlen(buf);
    if (append_tone_digit(buf, cap, &len, tone) < 0) {
        return -1;
    }
    return (int)len;
}

int py_aid_initial(int8_t initial_id, char *buf, size_t cap)
{
    if (initial_id < 0 || initial_id >= PY_N_INITIALS || buf == NULL) {
        return -1;
    }
    const char *aid = k_initial_aids[initial_id];
    size_t len = 0;
    if (ascii_append(buf, cap, &len, aid) < 0) {
        return -1;
    }
    return (int)len;
}

int py_aid_whole(int8_t whole_id, uint8_t tone, char *buf, size_t cap)
{
    if (whole_id < 0 || whole_id >= PY_N_WHOLE || buf == NULL) {
        return -1;
    }
    size_t len = 0;
    if (ascii_append(buf, cap, &len, py_whole[whole_id]) < 0) {
        return -1;
    }
    if (append_tone_digit(buf, cap, &len, tone) < 0) {
        return -1;
    }
    return (int)len;
}

int py_aid_word(const py_word_t *w, char *buf, size_t cap)
{
    if (w == NULL || buf == NULL) {
        return -1;
    }
    size_t len = 0;
    if (w->whole_id >= 0) {
        return py_aid_whole(w->whole_id, w->tone, buf, cap);
    }
    if (w->final_id < 0) {
        return -1;
    }
    if (w->initial_id >= 0) {
        if (ascii_append(buf, cap, &len, py_initials[w->initial_id]) < 0) {
            return -1;
        }
    }
    if (w->medial_id >= 0) {
        /* 介音 ü 在 j q x y 后写作 u（与组串规则一致）*/
        const char *med = (w->medial_id == 2 && py_initial_dots_u(w->initial_id)) ? "u"
            : (w->medial_id == 0 ? "i" : (w->medial_id == 1 ? "u" : "v"));
        if (ascii_append(buf, cap, &len, med) < 0) {
            return -1;
        }
    }
    char f[8];
    if (ascii_final(w->initial_id, w->final_id, f, sizeof(f)) < 0) {
        return -1;
    }
    if (ascii_append(buf, cap, &len, f) < 0) {
        return -1;
    }
    if (append_tone_digit(buf, cap, &len, w->tone) < 0) {
        return -1;
    }
    return (int)len;
}
