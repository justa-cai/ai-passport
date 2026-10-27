// music_kuwo URL 构造的 host 测试:百分号编码与三个端点的精确 URL。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "music_kuwo.h"

int main(void) {
    char buf[MUSIC_KUWO_URL_MAX];

    // ---- 百分号编码:未保留字符直通,其余 %XX 大写 ----
    assert(music_kuwo_percent_encode("jay chou", buf, sizeof(buf)));
    assert(strcmp(buf, "jay%20chou") == 0);
    // 周杰伦(UTF-8 共 9 字节)
    assert(music_kuwo_percent_encode("周杰伦", buf, sizeof(buf)));
    assert(strcmp(buf, "%E5%91%A8%E6%9D%B0%E4%BC%A6") == 0);
    assert(music_kuwo_percent_encode("A-z_9.~-", buf, sizeof(buf)));
    assert(strcmp(buf, "A-z_9.~-") == 0);
    // 缓冲不足:返回 (size_t)-1;空输入编码为空串
    assert(music_kuwo_percent_encode("周", buf, 4) == (size_t)-1);
    assert(music_kuwo_percent_encode("", buf, sizeof(buf)) == 0 && buf[0] == '\0');

    // ---- 搜索 URL(参数集与实测请求一致)----
    assert(music_kuwo_search_url("晴天", 0, 10, buf, sizeof(buf)));
    assert(strcmp(buf,
                  "http://search.kuwo.cn/r.s?all=%E6%99%B4%E5%A4%A9&ft=music&client=kt"
                  "&cluster=0&strategy=2012&rformat=json&encoding=utf8&issubtitle=1"
                  "&vipver=1&mobi=1&pn=0&rn=10&show_copyright_off=1") == 0);
    assert(music_kuwo_search_url("jay", 2, 30, buf, sizeof(buf)));
    assert(strstr(buf, "pn=2&rn=30") != NULL);

    // ---- 榜单 URL ----
    assert(music_kuwo_chart_url(MUSIC_KUWO_BANG_HOT, 0, 30, buf, sizeof(buf)));
    assert(strcmp(buf, "http://kbangserver.kuwo.cn/ksong.s?from=pc&fmt=json&ip=root&id=16&pn=0&rn=30") == 0);
    assert(music_kuwo_chart_url(MUSIC_KUWO_BANG_SHORT_VIDEO, 1, 8, buf, sizeof(buf)));
    assert(strcmp(buf, "http://kbangserver.kuwo.cn/ksong.s?from=pc&fmt=json&ip=root&id=158&pn=1&rn=8") == 0);

    // ---- 播放地址 URL ----
    assert(music_kuwo_resolve_url(661608480u, buf, sizeof(buf)));
    assert(strcmp(buf,
                  "http://antiserver.kuwo.cn/anti.s?type=convert_url3&rid=MUSIC_661608480"
                  "&format=mp3&response=url") == 0);

    // ---- 容量不足返回 0 ----
    char tiny[32];
    assert(music_kuwo_search_url("晴天", 0, 10, tiny, sizeof(tiny)) == 0);
    assert(music_kuwo_chart_url(MUSIC_KUWO_BANG_HOT, 0, 30, tiny, sizeof(tiny)) == 0);
    assert(music_kuwo_resolve_url(1u, tiny, sizeof(tiny)) == 0);

    puts("music_kuwo URL builder tests: PASS");
    return 0;
}
