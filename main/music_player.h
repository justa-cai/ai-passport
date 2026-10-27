// main/music_player.h —— 播放管线(解析→建流→解码→I2S)。
//
// player_task 单线程驱动 music_player_sm:对外 API 只设请求标志或投递
// 播放命令,全部副作用(网络/解码/音频)都在 player_task 里执行。
// 暂停语义:继续拉流但丢弃解码输出 —— 连接保活、恢复零延迟,代价是
// 暂停期间仍消耗带宽(头注释与文档都注明)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "music_player_sm.h"
#include "music_song.h"

typedef struct {
    music_ps_t state;
    music_song_t song;       // 当前曲(rid=0 = 无)
    bool track_ended;        // 自然播完/连续失败跳曲:主应用据此走下一首
    uint8_t progress_pct;    // 0..100(total 未知时恒 0)
    uint32_t cur_s;
    uint32_t total_s;        // 0 = 元数据缺时长
} music_player_status_t;

typedef void (*music_player_event_fn)(const music_player_status_t *st, void *user);

// 创建 player 任务(幂等)。必须在 LVGL/网络起来后、开始播歌前调用。
void music_player_start(void);
void music_player_set_event_cb(music_player_event_fn fn, void *user);

// 播放/切歌:中断当前流,解析新曲并起播(拷贝 song,调用方可复用)。
bool music_player_play(const music_song_t *song);

// 暂停/续播(播放中生效;空闲时无操作)。
void music_player_toggle_pause(void);

// 停止并回到空闲(浏览返回时不清背景播放,由主应用决定何时调用)。
void music_player_stop(void);

// 音量 0..100:播放中经 player_task 生效(避免与音频写并发)。
uint8_t music_player_volume(void);
void music_player_set_volume(uint8_t pct);

// 状态快照(任意线程;可能比事件回调旧一个节拍)。
void music_player_get_status(music_player_status_t *out);
