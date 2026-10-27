// music_player_sm 播放状态机的 host 测试:全转移表 + 解码失败跳曲策略。
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>

#include "music_player_sm.h"

int main(void) {
    music_player_sm_t sm;
    music_player_action_t act;

    // ---- IDLE:无效事件忽略,SELECT 发起解析 ----
    music_player_sm_init(&sm);
    assert(sm.state == MUS_PS_IDLE);
    assert(music_player_sm_step(&sm, MUS_PE_TOGGLE, &act) == MUS_PS_IDLE);
    assert(!act.begin_resolve && !act.begin_stream);
    assert(music_player_sm_step(&sm, MUS_PE_SELECT, &act) == MUS_PS_RESOLVING);
    assert(act.begin_resolve && !act.begin_stream);

    // ---- 正常播放主链 ----
    assert(music_player_sm_step(&sm, MUS_PE_RESOLVE_OK, &act) == MUS_PS_BUFFERING);
    assert(act.begin_stream && !act.begin_resolve);
    assert(music_player_sm_step(&sm, MUS_PE_STREAM_OK, &act) == MUS_PS_PLAYING);
    assert(!act.begin_stream && !act.set_paused);

    // ---- 暂停/续播 ----
    assert(music_player_sm_step(&sm, MUS_PE_TOGGLE, &act) == MUS_PS_PAUSED);
    assert(act.set_paused && !act.set_resumed);
    assert(music_player_sm_step(&sm, MUS_PE_TOGGLE, &act) == MUS_PS_PLAYING);
    assert(act.set_resumed && !act.set_paused);

    // ---- 播放中切歌:停流 + 对新曲重新解析 ----
    assert(music_player_sm_step(&sm, MUS_PE_NEXT, &act) == MUS_PS_RESOLVING);
    assert(act.stop_stream && act.begin_resolve);
    assert(music_player_sm_step(&sm, MUS_PE_RESOLVE_OK, &act) == MUS_PS_BUFFERING);
    assert(music_player_sm_step(&sm, MUS_PE_STREAM_OK, &act) == MUS_PS_PLAYING);
    assert(music_player_sm_step(&sm, MUS_PE_PREV, &act) == MUS_PS_RESOLVING);

    // ---- 解码失败:第 1 次重试当前曲,第 2 次请求跳曲 ----
    assert(music_player_sm_step(&sm, MUS_PE_RESOLVE_OK, &act) == MUS_PS_BUFFERING);
    assert(music_player_sm_step(&sm, MUS_PE_STREAM_OK, &act) == MUS_PS_PLAYING);
    assert(music_player_sm_step(&sm, MUS_PE_DECODE_FAIL, &act) == MUS_PS_RESOLVING);
    assert(act.stop_stream && act.begin_resolve && !act.skip_notice);
    assert(sm.fail_streak == 1);
    assert(music_player_sm_step(&sm, MUS_PE_RESOLVE_OK, &act) == MUS_PS_BUFFERING);
    assert(music_player_sm_step(&sm, MUS_PE_STREAM_OK, &act) == MUS_PS_PLAYING);
    assert(music_player_sm_step(&sm, MUS_PE_DECODE_FAIL, &act) == MUS_PS_RESOLVING);
    assert(act.skip_notice && act.begin_resolve);
    assert(sm.fail_streak == 0);  // 跳曲后计数复位

    // ---- 失败计数按"曲"累计:重试成功后再失败,继续计数直至跳曲 ----
    assert(music_player_sm_step(&sm, MUS_PE_RESOLVE_OK, &act) == MUS_PS_BUFFERING);
    assert(music_player_sm_step(&sm, MUS_PE_DECODE_FAIL, &act) == MUS_PS_RESOLVING);
    assert(sm.fail_streak == 1);
    assert(music_player_sm_step(&sm, MUS_PE_RESOLVE_OK, &act) == MUS_PS_BUFFERING);
    assert(music_player_sm_step(&sm, MUS_PE_STREAM_OK, &act) == MUS_PS_PLAYING);
    assert(music_player_sm_step(&sm, MUS_PE_DECODE_FAIL, &act) == MUS_PS_RESOLVING);
    assert(sm.fail_streak == 0);   // 第二次失败即跳曲,计数复位
    assert(act.skip_notice);

    // ---- 解析失败:回 IDLE 并停流 ----
    assert(music_player_sm_step(&sm, MUS_PE_RESOLVE_FAIL, &act) == MUS_PS_IDLE);
    assert(act.stop_stream && !act.begin_resolve);

    // ---- 缓冲中建流失败:与解码失败同策略 ----
    music_player_sm_init(&sm);
    music_player_sm_step(&sm, MUS_PE_SELECT, &act);
    music_player_sm_step(&sm, MUS_PE_RESOLVE_OK, &act);
    assert(music_player_sm_step(&sm, MUS_PE_STREAM_FAIL, &act) == MUS_PS_RESOLVING);
    assert(act.begin_resolve && sm.fail_streak == 1);

    // ---- 暂停态切歌/停止 ----
    music_player_sm_init(&sm);
    music_player_sm_step(&sm, MUS_PE_SELECT, &act);
    music_player_sm_step(&sm, MUS_PE_RESOLVE_OK, &act);
    music_player_sm_step(&sm, MUS_PE_STREAM_OK, &act);
    music_player_sm_step(&sm, MUS_PE_TOGGLE, &act);  // PAUSED
    assert(music_player_sm_step(&sm, MUS_PE_SELECT, &act) == MUS_PS_PLAYING);
    assert(act.set_resumed);
    music_player_sm_step(&sm, MUS_PE_TOGGLE, &act);  // PAUSED
    assert(music_player_sm_step(&sm, MUS_PE_NEXT, &act) == MUS_PS_RESOLVING);
    assert(act.stop_stream && act.begin_resolve);
    music_player_sm_step(&sm, MUS_PE_RESOLVE_OK, &act);
    music_player_sm_step(&sm, MUS_PE_STREAM_OK, &act);
    assert(music_player_sm_step(&sm, MUS_PE_STOP, &act) == MUS_PS_IDLE);
    assert(act.stop_stream);

    // ---- 各状态 STOP 一律回 IDLE ----
    music_player_sm_init(&sm);
    music_player_sm_step(&sm, MUS_PE_SELECT, &act);
    assert(music_player_sm_step(&sm, MUS_PE_STOP, &act) == MUS_PS_IDLE);
    music_player_sm_step(&sm, MUS_PE_SELECT, &act);
    music_player_sm_step(&sm, MUS_PE_RESOLVE_OK, &act);
    assert(music_player_sm_step(&sm, MUS_PE_STOP, &act) == MUS_PS_IDLE);

    puts("music_player_sm tests: PASS");
    return 0;
}
