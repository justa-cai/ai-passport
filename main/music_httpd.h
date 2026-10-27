// main/music_httpd.h —— 内嵌 HTTP 服务:AP 配网页 + STA 遥控页。
//
// 同一时刻只跑一个服务实例:AP 模式服务配网页(192.168.4.1),STA 上线后
// 换成遥控页(局域网内设备 IP)。两类 handler 通过 start_ap/start_sta
// 整服务切换,互不共存。遥控接口在局域网内无鉴权(文档注明边界)。
#pragma once

#include <stdbool.h>
#include <stddef.h>

// AP 模式:启动配网页服务(192.168.4.1)。已在跑且是配网模式则无操作。
void music_httpd_start_ap(void);

// STA 上线:启动遥控页服务。已在跑且是遥控模式则无操作;配网模式在跑则换页。
void music_httpd_start_sta(void);

// 停止 HTTP 服务(任意模式;未在跑则无操作)。
void music_httpd_stop(void);

// /api/status 正文缓冲上限(分配方与生成方共用):16 首曲名/歌手加转义
// 与状态字段的裕量。
#define MUSIC_HTTPD_STATUS_CAP 6144

// ---- 遥控回调(main 注册;httpd 任务上下文调用,内部自行加 LVGL 锁) ----

// 执行一条遥控命令。cmd: play/pause/next/prev/vol/chart/search;
// play 用 value=队列下标,vol 用 value=0..100,chart 用 value=榜单 id,
// search 用 arg=关键词(UTF-8)。返回 false = 参数不合法或未执行。
typedef bool (*music_remote_cmd_fn)(const char *cmd, int value, const char *arg,
                                    void *user);

// 生成 /api/status 的 JSON 正文(整包,含 net/player/queue 快照)。
// 返回写入字节数(不含 NUL);buf 恒以 NUL 结尾。
typedef size_t (*music_remote_status_fn)(char *buf, size_t cap, void *user);

void music_httpd_set_remote(music_remote_cmd_fn cmd, music_remote_status_fn status,
                            void *user);
