// main/music_json.c —— music_json.h 的实现:单遍字节状态机。
//
// 设计要点:
//  * 无动态分配,状态全部在 parser 结构体内,跨 feed 分块保持。
//  * 字符串值边解码边收集,缓冲预留 4 字节 UTF-8 余量,任何截断都
//    落在完整码点边界上(不会出现半个汉字)。
//  * 结构错误(括号错配、非法字符)置 error 并停止;响应被截断不算
//    错误——已解析出的歌曲保留。
#include "music_json.h"

#include <string.h>

// ---- 内部小工具 -----------------------------------------------------------

// 追加一个原始字节;留足 4 字节 UTF-8 完成空间,放不下就丢弃。
static void val_push(music_json_parser_t *p, char c) {
    if (p->val_len < sizeof(p->val) - 4) p->val[p->val_len++] = c;
}

// 追加一个 Unicode 码点(UTF-8 编码)。放不下整个码点则整体丢弃。
static void val_push_cp(music_json_parser_t *p, uint32_t cp) {
    char tmp[4];
    uint8_t n;
    if (cp < 0x80) {
        tmp[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        tmp[0] = (char)(0xC0 | (cp >> 6));
        tmp[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        tmp[0] = (char)(0xE0 | (cp >> 12));
        tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        tmp[0] = (char)(0xF0 | (cp >> 18));
        tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    if (p->val_len + n <= sizeof(p->val) - 1) {
        memcpy(p->val + p->val_len, tmp, n);
        p->val_len = (uint16_t)(p->val_len + n);
    }
}

// 从 val 中解析首个连续十进制数字串(允许 "MUSIC_123" 前缀形式),
// 无数字返回 0,溢出饱和到 0xFFFFFFFF。
static uint32_t parse_dec(const char *s, uint16_t len) {
    uint32_t v = 0;
    bool any = false;
    for (uint16_t i = 0; i < len; i++) {
        const char c = s[i];
        if (c >= '0' && c <= '9') {
            any = true;
            if (v > (0xFFFFFFFFu - (uint32_t)(c - '0')) / 10u) return 0xFFFFFFFFu;
            v = v * 10u + (uint32_t)(c - '0');
        } else if (any) {
            break;
        }
    }
    return any ? v : 0;
}

// 拷贝 val 到目标字段,截断退回到完整 UTF-8 码点边界。
static void copy_utf8_bounded(char *dst, size_t dst_cap, const char *src, uint16_t len) {
    size_t n = len < dst_cap - 1 ? len : dst_cap - 1;
    while (n > 0 && (src[n] & 0xC0) == 0x80) n--;  // 退掉被切断的尾字节
    memcpy(dst, src, n);
    dst[n] = '\0';
}

// ---- 初始化 ---------------------------------------------------------------

static void parser_reset(music_json_parser_t *p) {
    memset(p, 0, sizeof(*p));
    p->st = MJ_TOP;
    p->depth = -1;
}

void music_json_songs_init(music_json_parser_t *p, music_json_list_t kind,
                           uint8_t max_songs, music_json_song_cb_t cb, void *user) {
    parser_reset(p);
    p->list_kind = kind;
    p->max_songs = max_songs ? max_songs : 1;
    p->cb = cb;
    p->user = user;
}

void music_json_url_init(music_json_parser_t *p, char *url_out, size_t cap) {
    parser_reset(p);
    p->url_mode = true;
    p->url_out = url_out;
    p->url_cap = cap;
    if (cap) url_out[0] = '\0';
}

uint8_t music_json_song_count(const music_json_parser_t *p) {
    return p->song_count;
}

// ---- 字段识别 -------------------------------------------------------------

enum { F_NONE = 0, F_RID, F_TITLE, F_ARTIST, F_DUR };

static uint8_t field_for_key(const music_json_parser_t *p) {
    if (p->key_trunc || p->key_len == 0) return F_NONE;
    if (p->list_kind == MUSIC_JSON_LIST_SEARCH) {
        if (strcmp(p->key, "MUSICRID") == 0) return F_RID;
        if (strcmp(p->key, "SONGNAME") == 0) return F_TITLE;
        if (strcmp(p->key, "ARTIST") == 0) return F_ARTIST;
        if (strcmp(p->key, "DURATION") == 0) return F_DUR;
    } else {
        if (strcmp(p->key, "id") == 0) return F_RID;
        if (strcmp(p->key, "name") == 0) return F_TITLE;
        if (strcmp(p->key, "artist") == 0) return F_ARTIST;
        if (strcmp(p->key, "duration") == 0) return F_DUR;
    }
    return F_NONE;
}

// 字符串值收尾:按键名分发到 song 字段或 url 输出。
static void finish_string_value(music_json_parser_t *p) {
    p->val[p->val_len] = '\0';
    if (p->url_mode) {
        if (!p->url_seen && p->depth == 0 && p->stack[0] == MJ_OBJ && !p->key_trunc &&
            p->key_len == 3 && memcmp(p->key, "url", 3) == 0) {
            copy_utf8_bounded(p->url_out, p->url_cap, p->val, p->val_len);
            p->url_seen = true;
            p->done = true;
        }
    } else if (p->depth >= 1 && p->stack[p->depth] == MJ_SONG &&
               p->song_count < p->max_songs) {
        switch (field_for_key(p)) {
            case F_RID:
                p->song.rid = parse_dec(p->val, p->val_len);
                break;
            case F_TITLE:
                copy_utf8_bounded(p->song.title, sizeof(p->song.title), p->val, p->val_len);
                break;
            case F_ARTIST:
                copy_utf8_bounded(p->song.artist, sizeof(p->song.artist), p->val, p->val_len);
                break;
            case F_DUR: {
                const uint32_t secs = parse_dec(p->val, p->val_len);
                p->song.duration_s = (uint16_t)(secs > 0xFFFF ? 0xFFFF : secs);
                break;
            }
            default:
                break;
        }
    }
    p->val_len = 0;
    p->capturing = false;
}

// 歌曲对象闭合:rid 有效才回调,随后重置拼装缓冲。
static void finish_song(music_json_parser_t *p) {
    if (p->url_mode || !p->in_list) return;
    if (p->song_count >= p->max_songs) {
        p->done = true;
        return;
    }
    if (p->song.rid != 0) {
        p->cb(p->user, &p->song, p->song_count);
        p->song_count++;
        if (p->song_count >= p->max_songs) p->done = true;
    }
    memset(&p->song, 0, sizeof(p->song));
}

static void close_container(music_json_parser_t *p) {
    // 关闭当前容器('}' 或 ']' 已消费,栈顶待弹出)。
    const uint8_t kind = p->stack[p->depth];
    p->depth--;
    if (kind == MJ_ARR && p->in_list && p->depth < (int8_t)p->list_depth) {
        p->in_list = false;
    }
    if (kind == MJ_SONG) finish_song(p);
    p->st = p->depth < 0 ? MJ_TOP : MJ_AFTER_VALUE;
}

// 值结束后的分隔符:',' / ']' / '}'。返回 false 表示结构错误。
static bool after_value(music_json_parser_t *p, char c) {
    if (c == ',') {
        // 对象里逗号后是键,数组里逗号后是值。
        p->st = (p->stack[p->depth] == MJ_ARR) ? MJ_VALUE : MJ_OBJ_KEY;
        return true;
    }
    if (c == ']' && p->stack[p->depth] == MJ_ARR) {
        close_container(p);
        return true;
    }
    if (c == '}' && p->stack[p->depth] != MJ_ARR) {
        close_container(p);
        return true;
    }
    p->error = true;
    return false;
}

// 是否为目标歌曲数组(顶层对象里键名匹配的数组)。
static bool key_is_target_array(const music_json_parser_t *p) {
    if (p->url_mode || p->in_list || p->depth != 0) return false;
    if (p->list_kind == MUSIC_JSON_LIST_SEARCH)
        return !p->key_trunc && p->key_len == 7 && memcmp(p->key, "abslist", 7) == 0;
    return !p->key_trunc && p->key_len == 9 && memcmp(p->key, "musiclist", 9) == 0;
}

// ---- 主状态机 -------------------------------------------------------------

bool music_json_feed(music_json_parser_t *p, const char *data, size_t len) {
    if (p->error) return false;
    for (size_t i = 0; i < len; i++) {
        const char c = data[i];

        if (p->st == MJ_IN_KEY || p->st == MJ_IN_STR) {
            // 字符串内部:唯一不跳空白的状态。
            if (p->esc) {
                p->esc = false;
                switch (c) {
                    case '"': val_push(p, '"'); break;
                    case '\\': val_push(p, '\\'); break;
                    case '/': val_push(p, '/'); break;
                    case 'b': val_push(p, '\b'); break;
                    case 'f': val_push(p, '\f'); break;
                    case 'n': val_push(p, '\n'); break;
                    case 'r': val_push(p, '\r'); break;
                    case 't': val_push(p, '\t'); break;
                    case 'u':
                        p->uni_left = 4;
                        p->uni_val = 0;
                        break;
                    default:
                        // 未知转义:降级为替换符,尽量保住后续内容。
                        if (p->st == MJ_IN_STR) val_push_cp(p, 0xFFFD);
                        break;
                }
                continue;
            }
            if (p->uni_left > 0) {
                uint8_t hex;
                if (c >= '0' && c <= '9') hex = (uint8_t)(c - '0');
                else if (c >= 'a' && c <= 'f') hex = (uint8_t)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') hex = (uint8_t)(c - 'A' + 10);
                else {
                    p->error = true;
                    return false;
                }
                p->uni_val = (uint16_t)((p->uni_val << 4) | hex);
                if (--p->uni_left == 0) {
                    if (p->uni_val >= 0xD800 && p->uni_val <= 0xDBFF) {
                        // 高代理项:等待紧随的 \uDC00-\uDFFF 配对。
                        if (p->uni_hi != 0 && p->st == MJ_IN_STR) val_push_cp(p, 0xFFFD);
                        p->uni_hi = p->uni_val;
                    } else if (p->uni_val >= 0xDC00 && p->uni_val <= 0xDFFF) {
                        if (p->uni_hi != 0) {
                            if (p->st == MJ_IN_STR) {
                                val_push_cp(p, 0x10000u +
                                                   (((uint32_t)(p->uni_hi - 0xD800) << 10) |
                                                    (uint32_t)(p->uni_val - 0xDC00)));
                            }
                            p->uni_hi = 0;
                        } else if (p->st == MJ_IN_STR) {
                            val_push_cp(p, 0xFFFD);  // 孤立低代理项
                        }
                    } else {
                        if (p->uni_hi != 0) {
                            if (p->st == MJ_IN_STR) val_push_cp(p, 0xFFFD);
                            p->uni_hi = 0;
                        }
                        if (p->st == MJ_IN_STR) val_push_cp(p, p->uni_val);
                    }
                }
                continue;
            }
            // 高代理项后紧跟的不是转义(可能配对 \uDC00-DFFF):必是孤立
            // 代理,原位补 U+FFFD,保住与后续字符的相对顺序。
            if (p->st == MJ_IN_STR && p->uni_hi != 0 && c != '\\') {
                val_push_cp(p, 0xFFFD);
                p->uni_hi = 0;
            }
            if (c == '\\') {
                p->esc = true;
                continue;
            }
            if (c == '"') {
                if (p->st == MJ_IN_KEY) {
                    p->key[p->key_len] = '\0';
                    p->st = MJ_COLON;
                } else {
                    finish_string_value(p);
                    p->st = MJ_AFTER_VALUE;
                }
                continue;
            }
            if (p->st == MJ_IN_KEY) {
                if (p->key_len < sizeof(p->key) - 1) p->key[p->key_len++] = c;
                else p->key_trunc = true;
            } else if (p->capturing) {
                val_push(p, c);  // 原始 UTF-8 字节直通
            }
            continue;
        }

        // 其余状态:统一跳过空白。
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;

        switch (p->st) {
            case MJ_TOP:
                if (c != '{') {
                    p->error = true;
                    return false;
                }
                p->stack[++p->depth] = MJ_OBJ;
                p->st = MJ_OBJ_KEY;
                break;

            case MJ_OBJ_KEY:
                if (c == '"') {
                    p->key_len = 0;
                    p->key_trunc = false;
                    p->val_len = 0;
                    p->st = MJ_IN_KEY;
                } else if (c == '}') {
                    close_container(p);  // 空对象/对象结束
                } else {
                    p->error = true;
                    return false;
                }
                break;

            case MJ_COLON:
                if (c != ':') {
                    p->error = true;
                    return false;
                }
                p->st = MJ_VALUE;
                break;

            case MJ_VALUE:
                if (c == '"') {
                    // 决定这个字符串值是否需要收集(收尾时再按键名分发)。
                    p->val_len = 0;
                    p->esc = false;
                    p->uni_left = 0;
                    p->uni_hi = 0;
                    p->capturing = false;
                    if (p->url_mode) {
                        if (!p->url_seen && p->depth == 0 && !p->key_trunc &&
                            p->key_len == 3 && memcmp(p->key, "url", 3) == 0)
                            p->capturing = true;
                    } else if (p->depth >= 1 && p->stack[p->depth] == MJ_SONG &&
                               p->song_count < p->max_songs) {
                        p->capturing = true;
                    }
                    p->st = MJ_IN_STR;
                } else if (c == '{') {
                    if (p->depth >= 7) {
                        p->error = true;
                        return false;
                    }
                    // 目标数组里的元素对象标记为 MJ_SONG;更深层(歌曲对象
                    // 内部可能的嵌套数组)一律按普通对象跳过收集。
                    {
                        const int8_t parent = p->depth;
                        p->depth++;
                        p->stack[p->depth] =
                            (p->in_list && p->stack[parent] == MJ_ARR &&
                             parent == (int8_t)p->list_depth)
                                ? MJ_SONG
                                : MJ_OBJ;
                    }
                    p->st = MJ_OBJ_KEY;
                } else if (c == '[') {
                    if (p->depth >= 7) {
                        p->error = true;
                        return false;
                    }
                    // 先在压栈前判定:目标数组位于顶层对象(depth 0)内。
                    const bool is_target = key_is_target_array(p);
                    p->stack[++p->depth] = MJ_ARR;
                    if (is_target) {
                        p->in_list = true;
                        p->list_depth = (uint8_t)p->depth;
                    }
                    p->st = MJ_VALUE;  // 空数组由紧随的 ']' 关闭
                } else if (c == ']') {
                    if (p->stack[p->depth] != MJ_ARR) {
                        p->error = true;
                        return false;
                    }
                    close_container(p);  // 空数组
                } else if (c == '}' || c == ',' || c == ':') {
                    p->error = true;
                    return false;
                } else {
                    p->st = MJ_IN_LIT;  // 数字/true/false/null:跳过
                }
                break;

            case MJ_IN_LIT:
                if (c == ',' || c == ']' || c == '}') {
                    if (!after_value(p, c)) return false;
                }  // 其余字符都是字面量内容,直接吞掉
                break;

            case MJ_AFTER_VALUE:
                if (!after_value(p, c)) return false;
                break;

            default:
                p->error = true;
                return false;
        }
    }
    return true;
}
