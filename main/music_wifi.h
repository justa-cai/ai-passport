// main/music_wifi.h —— 配网生命周期绑定(Wi-Fi/NVS/HTTPD ← music_net_sm 决策)。
//
// 网络任务(net_task)单线程驱动状态机:IDF 事件回调与网页表单只往队列
// 投事件,真正的 Wi-Fi 模式切换/连接/NVS 写入都在 net_task 里按 action
// 执行。UI 通过 notify 回调拿状态变化(回调在 net_task 上下文,自行加锁)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "music_net_sm.h"

// 开放热点(配网入口)与 NVS 命名空间;配网页指引文字与实际 SSID 保持一致。
#define MUSIC_WIFI_AP_SSID "AI-Passport"
#define MUSIC_WIFI_NVS_NS "musicdl"
#define MUSIC_WIFI_NVS_KEY_SSID "ssid"
#define MUSIC_WIFI_NVS_KEY_PASS "pass"

// UI 通知回调:bits 为本次产生的 MUS_NET_NOTIFY_* 位,state 为最新状态。
typedef void (*music_wifi_notify_fn)(uint32_t bits, music_ns_t state, void *user);

// 初始化 NVS/Wi-Fi/事件循环并创建 net 任务(幂等:重复调用直接返回)。
void music_wifi_start(void);

void music_wifi_set_notify(music_wifi_notify_fn fn, void *user);

// 网页表单提交入口(任意线程安全):校验后入队,由 net_task 设为候选凭据
// 并进入 STA 连接测试。返回 false = 参数不合法(未入队)。
bool music_wifi_provision(const char *ssid, const char *pass);

// 当前网络状态(net_task 之外的只读快照;只在 UI 提示里用)。
music_ns_t music_wifi_state(void);
