// main/music_kuwo.h —— 酷我接口 URL 构造(纯函数,主机可测)。
//
// 全部端点均为纯 http(已实测):播放地址接口返回 https CDN 链接,
// 同路径 http 可用,设备端统一降级 scheme,全程免 TLS。
// 关键词为 UTF-8,经百分号编码发送(接口实测 UTF-8 请求/响应正常)。
#pragma once

#include <stddef.h>
#include <stdint.h>

// 榜单 id(实测可用;新增榜单在 kuwo 文档侧核对 id 后扩展)。
typedef enum {
    MUSIC_KUWO_BANG_HOT = 16,         // 热歌榜
    MUSIC_KUWO_BANG_NEW = 17,         // 新歌榜
    MUSIC_KUWO_BANG_CN = 62,          // 华语榜
    MUSIC_KUWO_BANG_RISING = 93,      // 飙升榜
    MUSIC_KUWO_BANG_SHORT_VIDEO = 158,  // 短视频热歌
} music_kuwo_bang_t;

#define MUSIC_KUWO_URL_MAX 512

// 搜索:r.s 接口,pn 页码从 0 起,rn 每页条数(设备端建议 ≤ 30)。
size_t music_kuwo_search_url(const char *keyword_utf8, unsigned page, unsigned rn,
                             char *out, size_t cap);

// 榜单:ksong.s 接口。
size_t music_kuwo_chart_url(music_kuwo_bang_t bang, unsigned page, unsigned rn,
                            char *out, size_t cap);

// 播放地址:convert_url3,rid 为曲目数字 id。
size_t music_kuwo_resolve_url(uint32_t rid, char *out, size_t cap);

// 把 UTF-8 关键词转成百分号编码(未保留字符原样,其余 %XX 大写十六进制)。
// 返回写入长度(空输入为 0);cap 不足返回 (size_t)-1,不写越界。
size_t music_kuwo_percent_encode(const char *in, char *out, size_t cap);
