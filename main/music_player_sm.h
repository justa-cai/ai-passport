// main/music_player_sm.h —— 播放器状态机(纯逻辑,主机可测)。
//
// 状态:IDLE → RESOLVING(取播放 URL)→ BUFFERING(建流/首包)→
//       PLAYING ⇄ PAUSED
// 解码失败连续 2 次自动跳下一首(第 1 次重试当前曲)。
// 状态机只做决策;实际网络/解码动作由调用方按 action 位执行。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MUS_PS_IDLE = 0,
    MUS_PS_RESOLVING,
    MUS_PS_BUFFERING,
    MUS_PS_PLAYING,
    MUS_PS_PAUSED,
} music_ps_t;

typedef enum {
    MUS_PE_SELECT = 0,  // 选中某首:立即发起解析
    MUS_PE_RESOLVE_OK,  // 拿到 URL:开始建流
    MUS_PE_RESOLVE_FAIL,
    MUS_PE_STREAM_OK,   // 流建立且首包音频到达:开始解码播放
    MUS_PE_STREAM_FAIL, // 建流失败(404/超时):按解码失败同一策略处理
    MUS_PE_DECODE_FAIL, // 当前曲解码错误
    MUS_PE_TOGGLE,      // OK 键:播放/暂停切换
    MUS_PE_NEXT,        // UP 键(播放页):下一首
    MUS_PE_PREV,        // DOWN 键(播放页):上一首
    MUS_PE_STOP,        // 返回上级/停止
} music_pe_t;

// 状态机要求调用方执行的副作用(一次 step 可组合多个)。
typedef struct {
    bool begin_resolve;  // 对"当前选中曲"发起 URL 解析
    bool begin_stream;   // 用刚解析到的 URL 建流并起播
    bool stop_stream;    // 停流停解码(释放管线)
    bool set_paused;     // 音频输出暂停(时钟停走)
    bool set_resumed;    // 音频输出恢复
    bool skip_notice;    // UI 提示:自动跳曲(解码连续失败)
} music_player_action_t;

typedef struct {
    music_ps_t state;
    uint8_t fail_streak;  // 当前曲连续解码/建流失败次数
} music_player_sm_t;

void music_player_sm_init(music_player_sm_t *sm);

// 推进一个事件,返回新状态;act 清零后按需置位(可为 NULL)。
// 无效事件(如 IDLE 下 TOGGLE)不改变状态,action 全零。
music_ps_t music_player_sm_step(music_player_sm_t *sm, music_pe_t ev,
                                music_player_action_t *act);

const char *music_ps_name(music_ps_t s);  // 日志/UI 用
