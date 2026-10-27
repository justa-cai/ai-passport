// main/music_httpd.h —— 配网页 HTTP 服务(AP 模式)与 STA 遥控页(后续提交)。
//
// v1 范围:AP 模式下的配网页。GET / 表单,POST /api/wifi 提交凭据,
// GET /api/status 供页面轮询连接结果。STA 模式遥控页在后续提交接入,
// 两类 handler 通过启动/停止整服务切换,互不共存。
#pragma once

#include <stdbool.h>

// AP 模式:启动配网页服务(192.168.4.1)。已在跑则无操作。
void music_httpd_start_ap(void);

// 停止 HTTP 服务(AP/STA 通用;未在跑则无操作)。
void music_httpd_stop(void);
