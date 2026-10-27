// main/music_queue.c —— music_queue.h 的实现。
#include "music_queue.h"

#include <string.h>

void music_queue_clear(music_queue_t *q) {
    q->count = 0;
    q->current = -1;
}

bool music_queue_push(music_queue_t *q, const music_song_t *song) {
    if (q->count >= MUSIC_QUEUE_CAP) return false;
    q->items[q->count++] = *song;
    return true;
}

uint8_t music_queue_count(const music_queue_t *q) {
    return q->count;
}

const music_song_t *music_queue_at(const music_queue_t *q, uint8_t index) {
    return index < q->count ? &q->items[index] : NULL;
}

bool music_queue_remove(music_queue_t *q, uint8_t index) {
    if (index >= q->count) return false;
    memmove(&q->items[index], &q->items[index + 1],
            (size_t)(q->count - index - 1) * sizeof(q->items[0]));
    q->count--;
    if (q->current >= 0) {
        if ((uint8_t)q->current == index)
            q->current = -1;
        else if ((uint8_t)q->current > index)
            q->current--;
    }
    return true;
}

void music_queue_set_current(music_queue_t *q, int8_t index) {
    q->current = (index >= 0 && index < (int8_t)q->count) ? index : -1;
}

const music_song_t *music_queue_current(const music_queue_t *q) {
    return q->current >= 0 ? &q->items[q->current] : NULL;
}

int8_t music_queue_next_index(const music_queue_t *q) {
    if (q->current < 0) return q->count ? 0 : -1;
    return q->current + 1 < (int8_t)q->count ? (int8_t)(q->current + 1) : -1;
}

int8_t music_queue_prev_index(const music_queue_t *q) {
    if (q->current < 0) return q->count ? 0 : -1;
    return q->current > 0 ? (int8_t)(q->current - 1) : -1;
}
