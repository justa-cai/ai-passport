// music_queue 定长队列的 host 测试:容量、移位、游标钳制。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "music_queue.h"

static music_song_t make(uint32_t rid, const char *title) {
    music_song_t s = {0};
    s.rid = rid;
    snprintf(s.title, sizeof(s.title), "%s", title);
    return s;
}

int main(void) {
    music_queue_t q;
    music_queue_clear(&q);
    assert(music_queue_count(&q) == 0);
    assert(music_queue_at(&q, 0) == NULL);
    assert(music_queue_current(&q) == NULL);
    assert(music_queue_next_index(&q) == -1);
    assert(music_queue_prev_index(&q) == -1);

    // 填满 16 首,第 17 首被拒。
    for (uint32_t i = 1; i <= MUSIC_QUEUE_CAP; i++) {
        char t[16];
        music_song_t s;
        snprintf(t, sizeof(t), "t%lu", (unsigned long)i);
        s = make(i, t);
        assert(music_queue_push(&q, &s));
    }
    assert(music_queue_count(&q) == MUSIC_QUEUE_CAP);
    {
        music_song_t extra = make(99, "x");
        assert(!music_queue_push(&q, &extra));
    }
    assert(music_queue_count(&q) == MUSIC_QUEUE_CAP);
    assert(music_queue_at(&q, MUSIC_QUEUE_CAP) == NULL);

    // 游标:钳制在 [0, count)。
    music_queue_set_current(&q, 5);
    assert(music_queue_current(&q)->rid == 6);  // 下标 5 → rid 6
    assert(music_queue_next_index(&q) == 6);
    assert(music_queue_prev_index(&q) == 4);
    music_queue_set_current(&q, 15);
    assert(music_queue_next_index(&q) == -1);   // 末尾无下一首(不循环)
    assert(music_queue_prev_index(&q) == 14);
    music_queue_set_current(&q, 0);
    assert(music_queue_prev_index(&q) == -1);   // 头部无上一首
    music_queue_set_current(&q, 200);           // 越界 → 清空游标
    assert(music_queue_current(&q) == NULL);
    assert(music_queue_next_index(&q) == 0);    // 空游标时下一首 = 第 0 首

    // 移除:整体前移,游标跟随调整。
    music_queue_set_current(&q, 10);
    assert(music_queue_remove(&q, 3));          // 移除下标 3(在游标前)
    assert(music_queue_count(&q) == 15);
    assert(music_queue_current(&q)->rid == 11); // 原 rid 11 从下标 10 顶到 9,游标 10→9
    assert(!music_queue_remove(&q, 200));
    assert(music_queue_remove(&q, 9));          // 移除当前曲本身 → 游标失效
    assert(music_queue_current(&q) == NULL);

    // 清空。
    music_queue_clear(&q);
    assert(music_queue_count(&q) == 0 && q.current == -1);

    puts("music_queue tests: PASS");
    return 0;
}
