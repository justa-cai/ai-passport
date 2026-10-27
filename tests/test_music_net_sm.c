// music_net_sm 网络生命周期状态机的 host 测试:
// 候选凭据仅在连接测试成功后覆盖已存凭据,失败回落 AP。
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "music_net_sm.h"

int main(void) {
    music_net_sm_t sm;
    music_net_action_t act;

    // ---- 上电无凭据:开 AP 等配网 ----
    music_net_sm_init(&sm);
    assert(music_net_sm_step(&sm, MUS_NET_EV_BOOT, 0, &act) == MUS_NS_AP_WAIT);
    assert(act.ap_enable && !act.sta_enable);
    assert(act.notify & MUS_NET_NOTIFY_AP_UP);

    // ---- 手机连入/离开:只影响提示状态 ----
    music_net_sm_step(&sm, MUS_NET_EV_CLIENT_JOINED, 100, &act);
    assert(sm.ap_client);
    music_net_sm_step(&sm, MUS_NET_EV_CLIENT_LEFT, 200, &act);
    assert(!sm.ap_client);

    // ---- 网页提交候选凭据:转 STA 测试(用候选)----
    music_net_sm_set_candidate(&sm, "HomeWiFi", "secret123");
    assert(music_net_sm_step(&sm, MUS_NET_EV_PROVISIONED, 1000, &act) == MUS_NS_STA_TESTING);
    assert(act.sta_connect && !act.ap_enable);
    assert(strcmp(act.sta_ssid, "HomeWiFi") == 0);
    assert(strcmp(act.sta_pass, "secret123") == 0);

    // ---- 成功拿到 IP:此刻才覆盖已存凭据 ----
    assert(music_net_sm_step(&sm, MUS_NET_EV_STA_GOT_IP, 5000, &act) == MUS_NS_ONLINE);
    assert(act.commit_candidate);
    assert(sm.has_committed && !sm.has_candidate);
    assert(strcmp(sm.committed_ssid, "HomeWiFi") == 0);
    assert(strcmp(sm.committed_pass, "secret123") == 0);
    assert(act.notify & MUS_NET_NOTIFY_ONLINE);

    // ---- 候选失败 × 重试上限:回落 AP,已存凭据不被污染 ----
    music_net_sm_init(&sm);
    music_net_sm_set_committed(&sm, "OldNet", "oldpass");   // 设备里已有的旧凭据
    assert(music_net_sm_step(&sm, MUS_NET_EV_BOOT, 0, &act) == MUS_NS_STA_TESTING);
    assert(strcmp(act.sta_ssid, "OldNet") == 0);            // 有旧凭据直接连
    assert(music_net_sm_step(&sm, MUS_NET_EV_STA_GOT_IP, 1000, &act) == MUS_NS_ONLINE);
    // 在线重新配网:新候选进入测试
    music_net_sm_set_candidate(&sm, "NewNet", "newpass");
    assert(music_net_sm_step(&sm, MUS_NET_EV_PROVISIONED, 2000, &act) == MUS_NS_STA_TESTING);
    assert(strcmp(act.sta_ssid, "NewNet") == 0);
    // 三连败:两次重试后第三次回落 AP
    assert(music_net_sm_step(&sm, MUS_NET_EV_STA_FAIL, 3000, &act) == MUS_NS_STA_TESTING);
    assert(strcmp(act.sta_ssid, "NewNet") == 0);            // 重试仍用候选
    assert(music_net_sm_step(&sm, MUS_NET_EV_STA_FAIL, 4000, &act) == MUS_NS_STA_TESTING);
    assert(music_net_sm_step(&sm, MUS_NET_EV_STA_FAIL, 5000, &act) == MUS_NS_AP_WAIT);
    assert(act.ap_enable && !act.sta_enable);
    assert(act.notify & MUS_NET_NOTIFY_BACK_TO_AP);
    assert(strcmp(sm.committed_ssid, "OldNet") == 0);       // 旧凭据完好
    assert(strcmp(sm.committed_pass, "oldpass") == 0);
    assert(!sm.has_candidate);                              // 失败候选弃用

    // ---- 测试超时(TICK 驱动)与事件失败等价 ----
    music_net_sm_init(&sm);
    music_net_sm_set_candidate(&sm, "A", "b");
    music_net_sm_step(&sm, MUS_NET_EV_BOOT, 0, &act);              // AP
    music_net_sm_step(&sm, MUS_NET_EV_PROVISIONED, 0, &act);       // 测试起点 t=0
    assert(music_net_sm_step(&sm, MUS_NET_EV_TICK, 10000, &act) == MUS_NS_STA_TESTING);
    assert(music_net_sm_step(&sm, MUS_NET_EV_TICK, 19000, &act) == MUS_NS_STA_TESTING);
    assert(music_net_sm_step(&sm, MUS_NET_EV_TICK, 21000, &act) == MUS_NS_STA_TESTING);  // 超时→重试1
    assert(music_net_sm_step(&sm, MUS_NET_EV_TICK, 42000, &act) == MUS_NS_STA_TESTING);  // 重试超时→重试2
    assert(music_net_sm_step(&sm, MUS_NET_EV_TICK, 64000, &act) == MUS_NS_AP_WAIT);      // 回落 AP

    // ---- 在线掉线:重连成功恢复 ONLINE ----
    music_net_sm_init(&sm);
    music_net_sm_set_committed(&sm, "Net", "pw");
    music_net_sm_step(&sm, MUS_NET_EV_BOOT, 0, &act);
    music_net_sm_step(&sm, MUS_NET_EV_STA_GOT_IP, 1000, &act);
    assert(music_net_sm_step(&sm, MUS_NET_EV_STA_FAIL, 2000, &act) == MUS_NS_STA_TESTING);
    assert(act.sta_connect && strcmp(act.sta_ssid, "Net") == 0);   // 用已存凭据重连
    assert(music_net_sm_step(&sm, MUS_NET_EV_STA_GOT_IP, 9000, &act) == MUS_NS_ONLINE);
    assert(!act.commit_candidate);                                 // 无候选不提交

    puts("music_net_sm tests: PASS");
    return 0;
}
