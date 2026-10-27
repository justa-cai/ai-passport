// main/music_json.h —— 酷我响应的增量流式 JSON 解析(SAX 风格)。
//
// 搜索/榜单响应可达数十 KB,设备内存放不下整包:调用方按任意分块
// (例如 2KB)顺序 feed,解析器在内部状态机里跨块推进,每凑齐一首
// 歌立即回调,绝不缓存整个响应。全程 UTF-8(接口已实测:请求与响应
// 均为 UTF-8,无需 GBK 转换)。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "music_song.h"

// 两种列表 schema:字段名不同,结构同为 {<数组键>:[{...},{...}],...}。
typedef enum {
    MUSIC_JSON_LIST_SEARCH = 0,  // search.kuwo.cn:r.s,数组键 abslist,
                                 // 字段 MUSICRID/SONGNAME/ARTIST/DURATION
    MUSIC_JSON_LIST_CHART,       // kbangserver:ksong.s,数组键 musiclist,
                                 // 字段 id/name/artist/duration
} music_json_list_t;

// 每解析出一首(对象闭合且 rid 有效)即回调;index 从 0 递增。
typedef void (*music_json_song_cb_t)(void *user, const music_song_t *song, uint8_t index);

typedef struct {
    // ---- 配置(init 时设定,之后只读) ----
    bool url_mode;  // true: convert_url3 响应,只提取顶层 "url" 字符串
    music_json_list_t list_kind;
    music_json_song_cb_t cb;
    void *user;
    uint8_t max_songs;        // 收够即停(后续对象照常解析但不再收集)
    char *url_out;            // url 模式输出缓冲(调用方提供)
    size_t url_cap;

    // ---- 解析状态(feed 间保持) ----
    enum {
        MJ_TOP,          // 期待顶层 '{'
        MJ_OBJ_KEY,      // 对象内:期待键 '"' 或 '}'
        MJ_IN_KEY,       // 键字符串内部
        MJ_COLON,        // 期待 ':'
        MJ_VALUE,        // 期待值首字符
        MJ_IN_STR,       // 字符串值内部
        MJ_IN_LIT,       // 数字/true/false/null 内部(跳过)
        MJ_AFTER_VALUE,  // 值结束:期待 ',' 或闭括号
    } st;
    int8_t depth;             // 当前嵌套深度(栈数组下标)
    enum { MJ_NONE = 0, MJ_OBJ, MJ_ARR, MJ_SONG } stack[8];  // 容器栈
    bool in_list;             // 已进入目标歌曲数组
    uint8_t list_depth;       // 目标数组所在栈深度
    bool url_seen;            // url 已捕获,不再重复写
    music_song_t song;        // 正在拼装的歌曲
    char key[20];             // 当前键(截断的键永不匹配目标字段)
    uint8_t key_len;
    bool key_trunc;
    char val[264];            // 当前字符串值(解码后);预留 UTF-8 截断余量
    uint16_t val_len;
    bool capturing;           // 当前字符串值需要收集
    bool esc;                 // 字符串内反斜杠转义进行中
    uint8_t uni_left;         // \uXXXX 剩余十六进制位
    uint16_t uni_val;
    uint16_t uni_hi;          // 待配对的高代理项(0=无)
    uint8_t song_count;
    bool done;                // url 已取到或歌曲收满,可提前停
    bool error;               // 畸形输入:置位后 feed 恒 false
} music_json_parser_t;

// 列表模式:kind + max_songs + 回调。
void music_json_songs_init(music_json_parser_t *p, music_json_list_t kind,
                           uint8_t max_songs, music_json_song_cb_t cb, void *user);

// URL 模式:提取顶层 "url" 到 url_out(最多 cap-1 字节,NUL 终止)。
void music_json_url_init(music_json_parser_t *p, char *url_out, size_t cap);

// 喂入一段响应字节。false = 畸形输入(此后解析器不再工作)。
// 分块边界任意:逐字节 feed 与整包 feed 结果完全一致。
bool music_json_feed(music_json_parser_t *p, const char *data, size_t len);

// 已回调的歌曲数。
uint8_t music_json_song_count(const music_json_parser_t *p);
