#include "screen_dump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "app_fonts.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#if LV_USE_SNAPSHOT
#include "src/draw/snapshot/lv_snapshot.h"
#endif

static const char *TAG = "shot";
#define SHOT_W 240
#define SHOT_H 320
#define SHOT_BYTES ((SHOT_W) * (SHOT_H) * 2)

/* ---- base64（标准字母表，无换行）---- */
static const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t b64_encode(const uint8_t *src, size_t len, char *dst)
{
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)src[i] << 16;
        if (i + 1 < len) {
            v |= (uint32_t)src[i + 1] << 8;
        }
        if (i + 2 < len) {
            v |= src[i + 2];
        }
        dst[o++] = kB64[(v >> 18) & 0x3F];
        dst[o++] = kB64[(v >> 12) & 0x3F];
        dst[o++] = (i + 1 < len) ? kB64[(v >> 6) & 0x3F] : '=';
        dst[o++] = (i + 2 < len) ? kB64[v & 0x3F] : '=';
    }
    dst[o] = '\0';
    return o;
}

/* 快照缓冲用静态分配：运行时堆碎片后 malloc(150KB) 会失败
 * （实测 free 192KB 仍无连续大块），静态区由链接器在链接期保证。*/
static uint8_t s_shot_buf[SHOT_BYTES] __attribute__((aligned(4)));

/* 流式输出：按 RLE 元组（3 字节）分块编码 base64，避免整屏大缓冲。*/
static void do_shot(void)
{
    if (!bsp_lvgl_lock(2000)) {
        printf("SHOT:ERR lock\n");
        return;
    }
    lv_image_dsc_t dsc;
    if (lv_snapshot_take_to_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB565,
                                &dsc, s_shot_buf, SHOT_BYTES) != LV_RESULT_OK) {
        printf("SHOT:ERR snapshot free=%u\n", (unsigned)esp_get_free_heap_size());
        bsp_lvgl_unlock();
        return;
    }

    /* 流式 RLE + base64：每批 300 个元组（900 字节，3 的倍数对齐 base64）。
     * 批缓冲用静态存储——任务栈放不下（实测 4K 栈触发 stack canary 崩溃）。*/
    enum { TUPLES_PER_BATCH = 300 };
    static uint8_t rle_batch[TUPLES_PER_BATCH * 3];
    static char b64_batch[TUPLES_PER_BATCH * 4 + 8];
    size_t total_rle = 0, total_b64 = 0;
    /* 静默其他任务的日志输出，避免与 SHOT 行在 USB CDC 上交错导致丢字节 */
    esp_log_level_t saved = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_NONE);
    printf("SHOT:BEGIN w=%d h=%d\n", SHOT_W, SHOT_H);
    size_t i = 0;
    while (i + 1 < SHOT_BYTES) {
        size_t n = 0;
        while (i + 1 < SHOT_BYTES && n < TUPLES_PER_BATCH) {
            uint8_t b1 = s_shot_buf[i];
            uint8_t b2 = s_shot_buf[i + 1];
            size_t run = 0;
            while (i + run * 2 + 1 < SHOT_BYTES && run < 255 &&
                   s_shot_buf[i + run * 2] == b1 && s_shot_buf[i + run * 2 + 1] == b2) {
                run++;
            }
            rle_batch[n * 3] = (uint8_t)run;
            rle_batch[n * 3 + 1] = b1;
            rle_batch[n * 3 + 2] = b2;
            n++;
            i += run * 2;
        }
        size_t b64_len = b64_encode(rle_batch, n * 3, b64_batch);
        total_rle += n * 3;
        total_b64 += b64_len;
        for (size_t k = 0; k < b64_len; k += 512) {
            size_t len = b64_len - k;
            if (len > 512) {
                len = 512;
            }
            printf("SHOT:%.*s\n", (int)len, b64_batch + k);
        }
    }
    printf("SHOT:END\n");
    fflush(stdout);
    esp_log_level_set("*", saved);
    ESP_LOGI(TAG, "screen dumped: rle=%u b64=%u", (unsigned)total_rle, (unsigned)total_b64);
    bsp_lvgl_unlock();
}

/* glyph 探针：dump LVGL 实际解出的 A8 字形位图（前若干字节 + 非零统计）。
 * 用法：glyph <十进制码点>，如 glyph 25340（拼）。*/
static void do_glyph(uint32_t cp)
{
    if (!bsp_lvgl_lock(2000)) {
        printf("GLYPH:ERR lock\n");
        return;
    }
    lv_font_glyph_dsc_t g = {0};
    extern lv_font_t g_font_ui;
    bool ok = lv_font_get_glyph_dsc(&g_font_ui, &g, cp, 0);
    if (!ok) {
        printf("GLYPH:ERR dsc not found\n");
        bsp_lvgl_unlock();
        return;
    }
    printf("GLYPH:dsc box=%dx%d ofs=%d,%d adv=%u stride=%u gid=%u\n",
           g.box_w, g.box_h, g.ofs_x, g.ofs_y, (unsigned)g.adv_w,
           (unsigned)g.stride, (unsigned)g.gid.index);
    lv_draw_buf_t *dbuf = lv_draw_buf_create((uint32_t)(g.box_w ? g.box_w : 1),
                                              (uint32_t)(g.box_h ? g.box_h : 1),
                                              LV_COLOR_FORMAT_A8, 0);
    if (dbuf == NULL) {
        printf("GLYPH:ERR drawbuf\n");
        bsp_lvgl_unlock();
        return;
    }
    const uint8_t *bm = (const uint8_t *)lv_font_get_glyph_bitmap(&g, dbuf);
    if (bm == NULL) {
        printf("GLYPH:ERR bitmap null\n");
        lv_draw_buf_destroy(dbuf);
        bsp_lvgl_unlock();
        return;
    }
    size_t n = (size_t)g.box_w * g.box_h;
    size_t nz = 0, sum = 0;
    for (size_t i = 0; i < n; i++) {
        if (bm[i]) {
            nz++;
        }
        sum += bm[i];
    }
    printf("GLYPH:a8 pixels=%u nonzero=%u sum=%u head=", (unsigned)n, (unsigned)nz,
           (unsigned)sum);
    for (size_t i = 0; i < n && i < 40; i++) {
        printf("%02x", bm[i]);
    }
    printf("\n");
    fflush(stdout);
    lv_draw_buf_destroy(dbuf);
    bsp_lvgl_unlock();
}

/* fontdesc 探针：并排 dump 内置 Montserrat 与自生成字体的描述符字段。*/
static void dump_one_font(const lv_font_t *f, const char *tag)
{
    const lv_font_fmt_txt_dsc_t *d = (const lv_font_fmt_txt_dsc_t *)f->dsc;
    printf("FONT:%s line_h=%d base=%d bpp=%u cmaps=%u fmt=%u stride=%u kern=%u\n",
           tag, (int)f->line_height, (int)f->base_line, d->bpp, d->cmap_num,
           d->bitmap_format, d->stride, d->kern_scale);
    for (int i = 0; i < d->cmap_num; i++) {
        const lv_font_fmt_txt_cmap_t *c = &d->cmaps[i];
        printf("FONT:%s cmap[%d] start=0x%lX len=%u gid_start=%u uni=%p ofs=%p list=%u type=%u\n",
               tag, i, (unsigned long)c->range_start, c->range_length,
               c->glyph_id_start, (const void *)c->unicode_list,
               c->glyph_id_ofs_list, c->list_length, (unsigned)c->type);
    }
    /* 首个非空字形的原始 4bpp 位图字节（对照位图数据通路）*/
    printf("FONT:%s bitmap[0..15]=", tag);
    const uint8_t *bm = d->glyph_bitmap;
    for (int i = 0; i < 16; i++) {
        printf("%02x", bm[i]);
    }
    printf("\n");
}

static void do_fontdesc(void)
{
    if (!bsp_lvgl_lock(2000)) {
        return;
    }
    extern const lv_font_t pinyin_font_22;
    dump_one_font(&lv_font_montserrat_20, "ref");
    dump_one_font(&pinyin_font_22, "mine");
    dump_one_font(&g_font_ui, "copy");
    fflush(stdout);
    bsp_lvgl_unlock();
}

static void console_task(void *arg)
{
    (void)arg;
    char line[64];
    char *end;
    ESP_LOGI(TAG, "调试控制台就绪：发送 shot 截屏 / heap 内存");
    for (;;) {
        /* fgets 阻塞于 USB Serial/JTAG stdin */
        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "shot") == 0) {
            do_shot();
        } else if (strncmp(line, "glyph ", 6) == 0) {
            do_glyph((uint32_t)strtoul(line + 6, NULL, 0));
        } else if (strcmp(line, "fontdesc") == 0) {
            do_fontdesc();
        } else if (strcmp(line, "heap") == 0) {
            printf("HEAP: free=%u min=%u\n",
                   (unsigned)esp_get_free_heap_size(),
                   (unsigned)esp_get_minimum_free_heap_size());
            fflush(stdout);
        } else if (strncmp(line, "goto ", 5) == 0) {
            /* 调试切屏：goto 0首页 1选课 2卡片 3测验 */
            app_debug_nav((int)strtol(line + 5, NULL, 0));
            printf("NAV:ok\n");
            fflush(stdout);
        } else if (strncmp(line, "key ", 4) == 0) {
            /* 调试注入按键：key <btn 0UP/1DOWN/2OK> <ev 1CLICK/2DOUBLE/3LONG> */
            int btn = (int)strtol(line + 4, &end, 0);
            int ev = (end != NULL && *end != '\0') ? (int)strtol(end, NULL, 0) : 1;
            app_debug_key(btn, ev);
            printf("KEY:ok %d %d\n", btn, ev);
            fflush(stdout);
        } else if (line[0] != '\0') {
            printf("ERR:未知命令 %s（可用：shot heap goto key）\n", line);
            fflush(stdout);
        }
    }
}

void screen_dump_start(void)
{
    xTaskCreate(console_task, "py_console", 6144, NULL, 3, NULL);
}
