// main/music_net_sm.h —— 网络生命周期状态机(纯逻辑,主机可测)。
//
// 生命周期:AP 等待配网 → (网页提交凭据)→ STA 连接测试 → 成功上线
//   [候选凭据此刻才覆盖已存凭据];失败/超时回落 AP 等待。
// 上电时已有凭据则跳过 AP 直接连 STA;在线后掉线自动重连,连续
//   失败回落 AP(用户可重新配网)。
// 状态机只做决策;Wi-Fi/NVS 动作由调用方按 action 执行。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MUS_NS_AP_WAIT = 0,   // AP 已开,等手机连入提交凭据
    MUS_NS_STA_TESTING,   // 正在用候选/已存凭据连 STA
    MUS_NS_ONLINE,        // 已拿到 IP
} music_ns_t;

typedef enum {
    MUS_NET_EV_BOOT = 0,        // 上电(带不带已存凭据均可)
    MUS_NET_EV_CLIENT_JOINED,   // 手机连上设备热点(UI 提示用)
    MUS_NET_EV_CLIENT_LEFT,
    MUS_NET_EV_PROVISIONED,     // 网页表单已写入 candidate(先经基本校验)
    MUS_NET_EV_STA_GOT_IP,
    MUS_NET_EV_STA_FAIL,        // 连接失败或掉线
    MUS_NET_EV_TICK,            // 周期 tick(超时驱动)
} music_net_ev_t;

// UI 通知位(位掩码,调用方消费后清零自己的镜像)。
#define MUS_NET_NOTIFY_AP_UP (1u << 0)
#define MUS_NET_NOTIFY_CONNECTING (1u << 1)
#define MUS_NET_NOTIFY_ONLINE (1u << 2)
#define MUS_NET_NOTIFY_FAIL (1u << 3)
#define MUS_NET_NOTIFY_BACK_TO_AP (1u << 4)

typedef struct {
    bool ap_enable;          // 期望 AP 接口开关
    bool sta_enable;         // 期望 STA 接口开关
    bool sta_connect;        // 触发一次 wifi_connect,用 sta_ssid/sta_pass
    char sta_ssid[33];
    char sta_pass[65];
    bool commit_candidate;   // 连接测试成功:把候选凭据写入 NVS(覆盖旧的)
    uint32_t notify;         // 本次 step 新产生的 UI 通知位
} music_net_action_t;

// 凭据长度上限与 ESP-IDF Wi-Fi 一致:SSID 32 字节、密码 64 字节。
#define MUSIC_NET_SSID_MAX 33
#define MUSIC_NET_PASS_MAX 65

typedef struct {
    music_ns_t state;
    char committed_ssid[MUSIC_NET_SSID_MAX];
    char committed_pass[MUSIC_NET_PASS_MAX];
    char candidate_ssid[MUSIC_NET_SSID_MAX];
    char candidate_pass[MUSIC_NET_PASS_MAX];
    bool has_committed;
    bool has_candidate;
    bool ap_client;        // 当前是否有手机连着热点
    uint16_t fail_count;   // 连续 STA 失败次数
    uint32_t state_since;  // 当前状态进入时刻(ms)
    uint32_t last_tick;    // 上次 tick 时刻(ms)
} music_net_sm_t;

// 超时参数(ms):STA 连接测试上限;在线重连期间无 IP 宽限。
#define MUSIC_NET_STA_TEST_TIMEOUT_MS (20u * 1000u)
#define MUSIC_NET_ONLINE_GRACE_MS (15u * 1000u)
#define MUSIC_NET_STA_RETRY_MAX 3

void music_net_sm_init(music_net_sm_t *sm);

// NVS 加载/网页表单写入(网页侧已做长度与字符校验)。
void music_net_sm_set_committed(music_net_sm_t *sm, const char *ssid, const char *pass);
void music_net_sm_set_candidate(music_net_sm_t *sm, const char *ssid, const char *pass);

// 推进一个事件,返回新状态;act 可为 NULL。now_ms 单调毫秒。
music_ns_t music_net_sm_step(music_net_sm_t *sm, music_net_ev_t ev, uint32_t now_ms,
                             music_net_action_t *act);

const char *music_ns_name(music_ns_t s);
