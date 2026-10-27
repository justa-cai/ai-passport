// main/music_queue.h —— 定长播放队列(静态分配,无动态内存)。
//
// 既是榜单/搜索结果的展示列表,也是播放列表:容量 16 首 ≈ 2.2KB,
// 放静态区不占堆。仅存数据与游标,不做任何 IO。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "music_song.h"

#define MUSIC_QUEUE_CAP 16

typedef struct {
    music_song_t items[MUSIC_QUEUE_CAP];
    uint8_t count;
    int8_t current;  // 当前播放/选中的绝对下标;-1 无
} music_queue_t;

void music_queue_clear(music_queue_t *q);
bool music_queue_push(music_queue_t *q, const music_song_t *song);  // 满 return false
uint8_t music_queue_count(const music_queue_t *q);
const music_song_t *music_queue_at(const music_queue_t *q, uint8_t index);  // 越界 NULL
bool music_queue_remove(music_queue_t *q, uint8_t index);  // 移除并整体前移
void music_queue_set_current(music_queue_t *q, int8_t index);
const music_song_t *music_queue_current(const music_queue_t *q);
// 下一首/上一首下标(clamp 不循环;无下一首返回 -1)。
int8_t music_queue_next_index(const music_queue_t *q);
int8_t music_queue_prev_index(const music_queue_t *q);
