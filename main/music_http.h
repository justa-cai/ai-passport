// main/music_http.h —— esp_http_client 流式 GET 封装(纯 http,全程无 TLS)。
//
// 酷我搜索/榜单/解析三个接口都是明文 http(已实测),设备不需要证书
// bundle,也避免 TLS 吃掉 ~40KB 堆。响应按 2KB 块回流给回调,调用方
// (SAX 解析器)绝不持有整包。net_task 单线程调用,读缓冲复用静态区。
#pragma once

#include <stdbool.h>
#include <stddef.h>

// 分块回调:返回 false = 上层已收满,立即停止读取并断开。
typedef bool (*music_http_chunk_fn)(const char *data, size_t len, void *user);

// 阻塞式 GET。返回 HTTP 状态码(200/206/302/…);连接/传输错误返回 -1。
// cb 为 NULL 时只取回状态码(不读 body)。
int music_http_get(const char *url, music_http_chunk_fn cb, void *user);
