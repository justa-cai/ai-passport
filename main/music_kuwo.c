// main/music_kuwo.c —— music_kuwo.h 的实现。
#include "music_kuwo.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

size_t music_kuwo_percent_encode(const char *in, char *out, size_t cap) {
    size_t o = 0;
    for (const unsigned char *s = (const unsigned char *)in; *s; s++) {
        const unsigned char c = *s;
        const bool unreserved =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            if (o + 1 >= cap) return (size_t)-1;
            out[o++] = (char)c;
        } else {
            static const char hex[] = "0123456789ABCDEF";
            if (o + 3 >= cap) return (size_t)-1;
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0x0F];
        }
    }
    if (o >= cap) return (size_t)-1;
    out[o] = '\0';
    return o;
}

size_t music_kuwo_search_url(const char *keyword_utf8, unsigned page, unsigned rn,
                             char *out, size_t cap) {
    char kw[256];
    if (music_kuwo_percent_encode(keyword_utf8, kw, sizeof(kw)) == (size_t)-1) return 0;
    const int n = snprintf(out, cap,
                           "http://search.kuwo.cn/r.s"
                           "?all=%s&ft=music&client=kt&cluster=0&strategy=2012"
                           "&rformat=json&encoding=utf8&issubtitle=1&vipver=1"
                           "&mobi=1&pn=%u&rn=%u&show_copyright_off=1",
                           kw, page, rn);
    if (n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

size_t music_kuwo_chart_url(music_kuwo_bang_t bang, unsigned page, unsigned rn,
                            char *out, size_t cap) {
    const int n = snprintf(out, cap,
                           "http://kbangserver.kuwo.cn/ksong.s"
                           "?from=pc&fmt=json&ip=root&id=%d&pn=%u&rn=%u",
                           (int)bang, page, rn);
    if (n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

size_t music_kuwo_resolve_url(uint32_t rid, char *out, size_t cap) {
    const int n = snprintf(out, cap,
                           "http://antiserver.kuwo.cn/anti.s"
                           "?type=convert_url3&rid=MUSIC_%lu&format=mp3&response=url",
                           (unsigned long)rid);
    if (n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}
