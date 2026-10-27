// main/music_song.h —— 曲目条目(各平台协议层的公共输出)。
#pragma once

#include <stdint.h>

// 标题/歌手按 UTF-8 存储,NUL 终止;64 字节上限覆盖截断后的最长歌名
// (列表展示宽度远小于此,超长在解析层按 UTF-8 边界截断)。
#define MUSIC_SONG_FIELD_MAX 65

typedef struct {
    uint32_t rid;                        // 曲目 id(酷我 MUSIC_<rid> 的数字部分)
    char title[MUSIC_SONG_FIELD_MAX];    // UTF-8
    char artist[MUSIC_SONG_FIELD_MAX];   // UTF-8
    uint16_t duration_s;                 // 时长(秒);未知为 0
} music_song_t;
