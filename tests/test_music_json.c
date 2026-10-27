// music_json 流式解析的 host 测试:真实酷我响应回放 + 逐字节流式
// 等价 + 截断/畸形/转义边界。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "music_json.h"
#include "music_fixtures.h"

typedef struct {
    music_song_t songs[8];
    uint8_t count;
} collector_t;

static void on_song(void *user, const music_song_t *song, uint8_t index) {
    collector_t *c = (collector_t *)user;
    assert(index == c->count);
    assert(c->count < 8);
    c->songs[c->count++] = *song;
}

static void parse_whole(const char *fixture, music_json_list_t kind, uint8_t max_songs,
                        collector_t *out) {
    memset(out, 0, sizeof(*out));
    music_json_parser_t p;
    music_json_songs_init(&p, kind, max_songs, on_song, out);
    assert(music_json_feed(&p, fixture, strlen(fixture)));
    assert(!p.error);
}

// 逐字节喂:分块边界任意是硬性要求,结果必须与整包一致。
static void parse_bytewise(const char *fixture, music_json_list_t kind, uint8_t max_songs,
                           collector_t *out) {
    memset(out, 0, sizeof(*out));
    music_json_parser_t p;
    music_json_songs_init(&p, kind, max_songs, on_song, out);
    const size_t n = strlen(fixture);
    for (size_t i = 0; i < n; i++) {
        assert(music_json_feed(&p, fixture + i, 1));
    }
    assert(!p.error);
}

static void assert_song(const music_song_t *s, uint32_t rid, const char *title,
                        const char *artist, uint16_t dur) {
    assert(s->rid == rid);
    assert(strcmp(s->title, title) == 0);
    assert(strcmp(s->artist, artist) == 0);
    assert(s->duration_s == dur);
}

int main(void) {
    // ---- 真实搜索响应(ASCII 关键词样本;SONGNAME 原始 UTF-8 中文,
    //      ARTIST 为原始中文 + \u0026 转义混排)----
    collector_t a, b;
    parse_whole(FIXTURE_SEARCH, MUSIC_JSON_LIST_SEARCH, 8, &a);
    assert(a.count == 2);
    assert_song(&a.songs[0], 661608480u, "恒星不忘 Forever Forever",
                "周杰伦&言承旭&吴建豪&周渝民&五月天 阿信", 285);
    assert_song(&a.songs[1], 228908u, "晴天", "周杰伦", 269);

    // 逐字节流式等价。
    parse_bytewise(FIXTURE_SEARCH, MUSIC_JSON_LIST_SEARCH, 8, &b);
    assert(b.count == a.count);
    for (uint8_t i = 0; i < a.count; i++) {
        assert(a.songs[i].rid == b.songs[i].rid);
        assert(strcmp(a.songs[i].title, b.songs[i].title) == 0);
        assert(strcmp(a.songs[i].artist, b.songs[i].artist) == 0);
        assert(a.songs[i].duration_s == b.songs[i].duration_s);
    }

    // max_songs 截停:只收第一首。
    parse_whole(FIXTURE_SEARCH, MUSIC_JSON_LIST_SEARCH, 1, &a);
    assert(a.count == 1);
    assert_song(&a.songs[0], 661608480u, "恒星不忘 Forever Forever",
                "周杰伦&言承旭&吴建豪&周渝民&五月天 阿信", 285);

    // ---- 真实榜单响应 ----
    parse_whole(FIXTURE_CHART, MUSIC_JSON_LIST_CHART, 8, &a);
    assert(a.count == 2);
    assert_song(&a.songs[0], 624683929u, "山风山风等等我", "万海东", 89);
    assert_song(&a.songs[1], 567247828u, "你有没有真的爱过我", "阿图表妹", 140);

    parse_bytewise(FIXTURE_CHART, MUSIC_JSON_LIST_CHART, 8, &b);
    assert(b.count == 2 && b.songs[1].rid == 567247828u);

    // ---- convert_url3 响应:顶层 "url" 提取 ----
    {
        char url[256];
        music_json_parser_t p;
        music_json_url_init(&p, url, sizeof(url));
        assert(music_json_feed(&p, FIXTURE_RESOLVE, strlen(FIXTURE_RESOLVE)));
        assert(strcmp(url,
                      "https://kw-lv.kuwo.cn/3d7c81c19462cdfb99a0250bebbc75f0/"
                      "6ae04827/resource/30106/trackmedia/M500001EoL4s3wji21.mp3") == 0);
        // 输出缓冲过小:截断必须 NUL 终止且是前缀。
        char tiny[16];
        music_json_url_init(&p, tiny, sizeof(tiny));
        assert(music_json_feed(&p, FIXTURE_RESOLVE, strlen(FIXTURE_RESOLVE)));
        assert(strlen(tiny) == sizeof(tiny) - 1);
        assert(strncmp(tiny, "https://kw-lv", 13) == 0);
    }

    // ---- 响应截断:不算错误,已完整的歌保留 ----
    {
        collector_t c = {0};
        music_json_parser_t p;
        music_json_songs_init(&p, MUSIC_JSON_LIST_SEARCH, 8, on_song, &c);
        assert(music_json_feed(&p, FIXTURE_SEARCH, strlen(FIXTURE_SEARCH) / 2));
        assert(!p.error);  // 截断 ≠ 畸形
        assert(c.count <= 2);
    }

    // ---- 畸形输入:置错后拒绝继续 ----
    {
        music_json_parser_t p;
        music_json_songs_init(&p, MUSIC_JSON_LIST_SEARCH, 8, on_song, NULL);
        assert(!music_json_feed(&p, "not-json", 8));
        assert(p.error);
        assert(!music_json_feed(&p, "{}", 2));  // 持错后恒 false

        music_json_songs_init(&p, MUSIC_JSON_LIST_SEARCH, 8, on_song, NULL);
        assert(!music_json_feed(&p, "{\"a\":}", 6));  // 缺值
        assert(p.error);

        music_json_songs_init(&p, MUSIC_JSON_LIST_SEARCH, 8, on_song, NULL);
        assert(!music_json_feed(&p, "{\"a\" 1}", 7));  // 缺冒号
        assert(p.error);
    }

    // ---- 手造样本:转义与边界 ----
    {
        // \uD83D\uDE00 代理对 → U+1F600(4 字节 UTF-8);孤立代理 → U+FFFD。
        static const char esc[] =
            "{\"musiclist\":[{\"id\":\"7\",\"name\":\"\\uD83D\\uDE00ok\","
            "\"artist\":\"\\uD800abc\",\"duration\":\"61\"}]}";
        collector_t c = {0};
        music_json_parser_t p;
        music_json_songs_init(&p, MUSIC_JSON_LIST_CHART, 8, on_song, &c);
        assert(music_json_feed(&p, esc, strlen(esc)));
        assert(c.count == 1);
        assert(c.songs[0].rid == 7);
        // U+1F600 = F0 9F 98 80,随后 "ok"
        static const char want_name[] = "\xF0\x9F\x98\x80ok";
        assert(strcmp(c.songs[0].title, want_name) == 0);
        // 孤立高代理 → U+FFFD(EF BF BD),后续 ASCII 原样
        static const char want_artist[] = "\xEF\xBF\xBD" "abc";
        assert(strcmp(c.songs[0].artist, want_artist) == 0);
        assert(c.songs[0].duration_s == 61);
    }
    {
        // 歌曲对象内部嵌套数组:内层字段不得污染外层歌曲。
        static const char nested[] =
            "{\"abslist\":[{\"SONGNAME\":\"outer\",\"bad\":[{\"SONGNAME\":\"inner\"}],"
            "\"MUSICRID\":\"MUSIC_9\",\"ARTIST\":\"a\",\"DURATION\":\"5\"}]}";
        collector_t c = {0};
        music_json_parser_t p;
        music_json_songs_init(&p, MUSIC_JSON_LIST_SEARCH, 8, on_song, &c);
        assert(music_json_feed(&p, nested, strlen(nested)));
        assert(c.count == 1);
        assert(strcmp(c.songs[0].title, "outer") == 0);
        assert(c.songs[0].rid == 9);
    }
    {
        // rid 缺失的对象整首丢弃;空数组/空对象不崩溃。
        static const char norid[] =
            "{\"abslist\":[{\"SONGNAME\":\"x\",\"ARTIST\":\"y\",\"DURATION\":\"1\"},"
            "{\"MUSICRID\":\"MUSIC_3\",\"SONGNAME\":\"z\",\"ARTIST\":\"w\",\"DURATION\":\"2\"}]}";
        collector_t c = {0};
        music_json_parser_t p;
        music_json_songs_init(&p, MUSIC_JSON_LIST_SEARCH, 8, on_song, &c);
        assert(music_json_feed(&p, norid, strlen(norid)));
        assert(c.count == 1);
        assert(c.songs[0].rid == 3);

        // 追加第二个顶层文档(流式接口可能分多次到达)
        static const char empty_arr[] = "{\"abslist\":[]}";
        static const char empty_obj[] = "{\"x\":{}}";
        assert(music_json_feed(&p, empty_arr, strlen(empty_arr)));
        assert(music_json_feed(&p, empty_obj, strlen(empty_obj)));
    }

    puts("music_json streaming parser tests: PASS");
    return 0;
}
