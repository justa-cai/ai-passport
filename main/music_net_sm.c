// main/music_net_sm.c —— music_net_sm.h 的实现。
#include "music_net_sm.h"

#include <string.h>

// ---- 内部工具 -------------------------------------------------------------

static void copy_bounded(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void act_reset(music_net_action_t *act) {
    if (act) memset(act, 0, sizeof(*act));
}

static void notify(music_net_action_t *act, uint32_t bits) {
    if (act) act->notify |= bits;
}

static void enter_ap_wait(music_net_sm_t *sm, uint32_t now_ms, music_net_action_t *act) {
    sm->state = MUS_NS_AP_WAIT;
    sm->state_since = now_ms;
    sm->fail_count = 0;
    sm->has_candidate = false;  // 候选已判失败,弃用
    if (act) {
        act->ap_enable = true;
        act->sta_enable = false;
    }
    notify(act, MUS_NET_NOTIFY_BACK_TO_AP | MUS_NET_NOTIFY_AP_UP);
}

// 发起一次 STA 连接(use_candidate 决定用哪组凭据)。
static void enter_sta_testing(music_net_sm_t *sm, uint32_t now_ms, bool use_candidate,
                              music_net_action_t *act) {
    sm->state = MUS_NS_STA_TESTING;
    sm->state_since = now_ms;
    if (act) {
        act->ap_enable = false;
        act->sta_enable = true;
        act->sta_connect = true;
        copy_bounded(act->sta_ssid, sizeof(act->sta_ssid),
                     use_candidate ? sm->candidate_ssid : sm->committed_ssid);
        copy_bounded(act->sta_pass, sizeof(act->sta_pass),
                     use_candidate ? sm->candidate_pass : sm->committed_pass);
    }
    notify(act, MUS_NET_NOTIFY_CONNECTING);
}

// 连接失败统一处理:未超次数→重试;超次数→回落 AP。
static void sta_failed(music_net_sm_t *sm, uint32_t now_ms, music_net_action_t *act) {
    sm->fail_count++;
    if (sm->fail_count < MUSIC_NET_STA_RETRY_MAX) {
        enter_sta_testing(sm, now_ms, sm->has_candidate, act);
    } else {
        enter_ap_wait(sm, now_ms, act);
        notify(act, MUS_NET_NOTIFY_FAIL);
    }
}

// ---- 公开接口 -------------------------------------------------------------

void music_net_sm_init(music_net_sm_t *sm) {
    memset(sm, 0, sizeof(*sm));
    sm->state = MUS_NS_AP_WAIT;
}

void music_net_sm_set_committed(music_net_sm_t *sm, const char *ssid, const char *pass) {
    copy_bounded(sm->committed_ssid, sizeof(sm->committed_ssid), ssid);
    copy_bounded(sm->committed_pass, sizeof(sm->committed_pass), pass);
    sm->has_committed = true;
}

void music_net_sm_set_candidate(music_net_sm_t *sm, const char *ssid, const char *pass) {
    copy_bounded(sm->candidate_ssid, sizeof(sm->candidate_ssid), ssid);
    copy_bounded(sm->candidate_pass, sizeof(sm->candidate_pass), pass);
    sm->has_candidate = true;
}

const char *music_ns_name(music_ns_t s) {
    switch (s) {
        case MUS_NS_AP_WAIT: return "AP_WAIT";
        case MUS_NS_STA_TESTING: return "STA_TESTING";
        case MUS_NS_ONLINE: return "ONLINE";
        default: return "?";
    }
}

music_ns_t music_net_sm_step(music_net_sm_t *sm, music_net_ev_t ev, uint32_t now_ms,
                             music_net_action_t *act) {
    act_reset(act);
    if (ev == MUS_NET_EV_TICK) sm->last_tick = now_ms;

    switch (sm->state) {
        case MUS_NS_AP_WAIT:
            switch (ev) {
                case MUS_NET_EV_BOOT:
                    // 有已存凭据直接连 STA;否则保持 AP 等配网。
                    if (sm->has_committed)
                        enter_sta_testing(sm, now_ms, false, act);
                    else
                        enter_ap_wait(sm, now_ms, act);
                    break;
                case MUS_NET_EV_PROVISIONED:
                    enter_sta_testing(sm, now_ms, true, act);
                    break;
                case MUS_NET_EV_CLIENT_JOINED:
                    sm->ap_client = true;
                    break;
                case MUS_NET_EV_CLIENT_LEFT:
                    sm->ap_client = false;
                    break;
                default:
                    break;
            }
            break;

        case MUS_NS_STA_TESTING:
            switch (ev) {
                case MUS_NET_EV_STA_GOT_IP:
                    if (sm->has_candidate) {
                        // 候选凭据验证成功:此刻才允许覆盖已存凭据。
                        copy_bounded(sm->committed_ssid, sizeof(sm->committed_ssid),
                                     sm->candidate_ssid);
                        copy_bounded(sm->committed_pass, sizeof(sm->committed_pass),
                                     sm->candidate_pass);
                        sm->has_committed = true;
                        sm->has_candidate = false;
                        if (act) act->commit_candidate = true;
                    }
                    sm->state = MUS_NS_ONLINE;
                    sm->state_since = now_ms;
                    sm->fail_count = 0;
                    if (act) {
                        act->ap_enable = false;
                        act->sta_enable = true;
                    }
                    notify(act, MUS_NET_NOTIFY_ONLINE);
                    break;
                case MUS_NET_EV_STA_FAIL:
                    sta_failed(sm, now_ms, act);
                    break;
                case MUS_NET_EV_TICK:
                    if (now_ms - sm->state_since > MUSIC_NET_STA_TEST_TIMEOUT_MS)
                        sta_failed(sm, now_ms, act);
                    break;
                default:
                    break;
            }
            break;

        case MUS_NS_ONLINE:
            switch (ev) {
                case MUS_NET_EV_STA_FAIL:
                    sta_failed(sm, now_ms, act);
                    notify(act, MUS_NET_NOTIFY_FAIL);
                    break;
                case MUS_NET_EV_PROVISIONED:
                    // 在线重新配网:用新候选回到测试流程。
                    sm->fail_count = 0;
                    enter_sta_testing(sm, now_ms, true, act);
                    break;
                case MUS_NET_EV_CLIENT_JOINED:
                case MUS_NET_EV_CLIENT_LEFT:
                    break;  // STA 模式下无热点客户端概念
                default:
                    break;
            }
            break;

        default:
            break;
    }
    return sm->state;
}
