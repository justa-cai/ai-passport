// main/music_player_sm.c —— music_player_sm.h 的实现。
#include "music_player_sm.h"

#include <string.h>

// 连续失败达到此次数即跳曲。
#define FAIL_STREAK_SKIP 2

static void act_reset(music_player_action_t *act) {
    if (act) memset(act, 0, sizeof(*act));
}

void music_player_sm_init(music_player_sm_t *sm) {
    memset(sm, 0, sizeof(*sm));
    sm->state = MUS_PS_IDLE;
}

const char *music_ps_name(music_ps_t s) {
    switch (s) {
        case MUS_PS_IDLE: return "IDLE";
        case MUS_PS_RESOLVING: return "RESOLVING";
        case MUS_PS_BUFFERING: return "BUFFERING";
        case MUS_PS_PLAYING: return "PLAYING";
        case MUS_PS_PAUSED: return "PAUSED";
        default: return "?";
    }
}

// 失败处理:第 1 次重试当前曲,第 2 次置 skip_notice 请求跳曲(调用方
// 推进队列游标;若已无下一首,调用方发 STOP 回 IDLE)。
static music_ps_t handle_fail(music_player_sm_t *sm, music_player_action_t *act) {
    sm->fail_streak++;
    if (sm->fail_streak < FAIL_STREAK_SKIP) {
        // 重试当前曲:重新解析(播放 URL 可能有时效,不复用)。
        if (act) act->begin_resolve = true;
        return MUS_PS_RESOLVING;
    }
    sm->fail_streak = 0;
    if (act) {
        act->begin_resolve = true;
        act->skip_notice = true;
    }
    return MUS_PS_RESOLVING;
}

music_ps_t music_player_sm_step(music_player_sm_t *sm, music_pe_t ev,
                                music_player_action_t *act) {
    act_reset(act);
    switch (sm->state) {
        case MUS_PS_IDLE:
            if (ev == MUS_PE_SELECT) {
                sm->fail_streak = 0;
                if (act) act->begin_resolve = true;
                sm->state = MUS_PS_RESOLVING;
            }
            break;

        case MUS_PS_RESOLVING:
            if (ev == MUS_PE_RESOLVE_OK) {
                if (act) act->begin_stream = true;
                sm->state = MUS_PS_BUFFERING;
            } else if (ev == MUS_PE_RESOLVE_FAIL || ev == MUS_PE_STOP) {
                if (act) act->stop_stream = true;
                sm->fail_streak = 0;
                sm->state = MUS_PS_IDLE;
            }
            // NEXT/PREV 在解析中:允许直接切目标(重新解析新选中曲)。
            else if (ev == MUS_PE_NEXT || ev == MUS_PE_PREV || ev == MUS_PE_SELECT) {
                sm->fail_streak = 0;
                if (act) act->begin_resolve = true;
                sm->state = MUS_PS_RESOLVING;
            }
            break;

        case MUS_PS_BUFFERING:
            if (ev == MUS_PE_STREAM_OK) {
                // 注意:此处不清 fail_streak —— 失败计数按"曲"累计。
                // 坏曲重试即便短暂成功,再次失败也要继续计数,否则会
                // 无限重试同一首;计数只在换曲(SELECT/NEXT/PREV/跳曲)清零。
                sm->state = MUS_PS_PLAYING;
            } else if (ev == MUS_PE_STREAM_FAIL || ev == MUS_PE_DECODE_FAIL) {
                sm->state = handle_fail(sm, act);
            } else if (ev == MUS_PE_STOP) {
                if (act) act->stop_stream = true;
                sm->fail_streak = 0;
                sm->state = MUS_PS_IDLE;
            } else if (ev == MUS_PE_NEXT || ev == MUS_PE_PREV) {
                sm->fail_streak = 0;
                if (act) {
                    act->stop_stream = true;
                    act->begin_resolve = true;
                }
                sm->state = MUS_PS_RESOLVING;
            }
            break;

        case MUS_PS_PLAYING:
            if (ev == MUS_PE_TOGGLE) {
                if (act) act->set_paused = true;
                sm->state = MUS_PS_PAUSED;
            } else if (ev == MUS_PE_NEXT || ev == MUS_PE_PREV) {
                if (act) {
                    act->stop_stream = true;
                    act->begin_resolve = true;
                }
                sm->fail_streak = 0;
                sm->state = MUS_PS_RESOLVING;
            } else if (ev == MUS_PE_DECODE_FAIL || ev == MUS_PE_STREAM_FAIL) {
                if (act) act->stop_stream = true;
                sm->state = handle_fail(sm, act);
            } else if (ev == MUS_PE_STOP) {
                if (act) act->stop_stream = true;
                sm->fail_streak = 0;
                sm->state = MUS_PS_IDLE;
            }
            break;

        case MUS_PS_PAUSED:
            if (ev == MUS_PE_TOGGLE || ev == MUS_PE_SELECT) {
                if (act) act->set_resumed = true;
                sm->state = MUS_PS_PLAYING;
            } else if (ev == MUS_PE_NEXT || ev == MUS_PE_PREV) {
                if (act) {
                    act->stop_stream = true;
                    act->begin_resolve = true;
                }
                sm->fail_streak = 0;
                sm->state = MUS_PS_RESOLVING;
            } else if (ev == MUS_PE_STOP) {
                if (act) act->stop_stream = true;
                sm->fail_streak = 0;
                sm->state = MUS_PS_IDLE;
            }
            break;

        default:
            break;
    }
    return sm->state;
}
