#include "audio_player.h"

#include <stdio.h>
#include <string.h>

#include "adpcm_decoder.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pinyin_audio_index.h"

/* 嵌入的发音 blob（CMake target_add_binary_data 生成符号；强引用防止被 GC 裁剪）*/
extern const uint8_t _binary_pinyin_audio_ima_bin_start[];
extern const uint8_t _binary_pinyin_audio_ima_bin_end[];

static const char *TAG = "py_audio";

#define AUDIO_TASK_STACK 4096
#define AUDIO_TASK_PRIO 4
#define AUDIO_QUEUE_LEN 8
#define CHUNK_SAMPLES 256
#define FX_VOLUME 58
#define SPEECH_VOLUME 75
#define BLEND_GAP_MS 150
#define MAX_SEQ 3

typedef enum { CMD_CLIP = 0, CMD_FX, CMD_BLEND, CMD_REPLAY } cmd_type_t;

typedef struct {
    cmd_type_t type;
    audio_fx_t fx;
    char aids[MAX_SEQ][16];
    uint32_t generation;
} audio_cmd_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static volatile uint32_t s_generation;
static volatile bool s_speech_ok;
static char s_last_aid[16];

#define AUDIO_RATE_HZ 16000

static void play_note(int freq_hz, int ms, uint32_t generation, int amplitude)
{
    /* 方波合成：按 CHUNK_SAMPLES 块写入 */
    int16_t buf[CHUNK_SAMPLES];
    int half_period = AUDIO_RATE_HZ / (2 * freq_hz);
    int total = (AUDIO_RATE_HZ / 1000) * ms;
    int written = 0;
    int phase = 0;
    while (written < total) {
        if (generation != s_generation) {
            return; /* 被新命令取消 */
        }
        int n = CHUNK_SAMPLES;
        if (written + n > total) {
            n = total - written;
        }
        for (int i = 0; i < n; i++) {
            buf[i] = (int16_t)(phase < half_period ? amplitude : -amplitude);
            if (++phase >= half_period * 2) {
                phase = 0;
            }
        }
        if (bsp_audio_write(buf, (size_t)n * sizeof(int16_t)) != ESP_OK) {
            return;
        }
        written += n;
    }
}

static void play_fx_notes(audio_fx_t fx, uint32_t generation)
{
    typedef struct { int freq; int ms; } note_t;
    static const note_t seqs[][4] = {
        [FX_PAGE] = {{784, 40}, {0, 0}, {0, 0}, {0, 0}},
        [FX_CORRECT] = {{1047, 90}, {1319, 90}, {1568, 110}, {0, 0}},
        [FX_WRONG] = {{330, 140}, {262, 160}, {0, 0}, {0, 0}},
        [FX_STAR] = {{1047, 80}, {1319, 80}, {1568, 80}, {2093, 130}},
    };
    bsp_audio_set_volume(FX_VOLUME);
    for (int i = 0; i < 4; i++) {
        if (seqs[fx][i].freq == 0 || generation != s_generation) {
            break;
        }
        play_note(seqs[fx][i].freq, seqs[fx][i].ms, generation, 5200);
    }
    bsp_audio_set_volume(SPEECH_VOLUME);
}

static void play_clip_data(const uint8_t *data, size_t len, uint32_t generation)
{
    adpcm_ima_t dec;
    if (!adpcm_ima_init(&dec, data, len)) {
        return;
    }
    int16_t buf[CHUNK_SAMPLES];
    for (;;) {
        if (generation != s_generation) {
            return;
        }
        size_t n = adpcm_ima_read(&dec, buf, CHUNK_SAMPLES);
        if (n == 0) {
            break;
        }
        if (bsp_audio_write(buf, n * sizeof(int16_t)) != ESP_OK) {
            break;
        }
    }
}

static void play_aid(const char *aid, uint32_t generation)
{
    if (aid[0] == '\0') {
        return;
    }
    const py_audio_entry_t *e = py_audio_lookup(aid);
    if (e == NULL) {
        ESP_LOGW(TAG, "clip 未命中: %s", aid);
        return;
    }
    play_clip_data(_binary_pinyin_audio_ima_bin_start + e->offset, e->length, generation);
}

static void audio_task(void *arg)
{
    (void)arg;
    audio_cmd_t cmd;
    bsp_audio_set_volume(SPEECH_VOLUME);
    for (;;) {
        if (xQueueReceive(s_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (cmd.generation != s_generation) {
            continue; /* 已被更新命令取代 */
        }
        switch (cmd.type) {
        case CMD_CLIP:
            play_aid(cmd.aids[0], cmd.generation);
            break;
        case CMD_FX:
            play_fx_notes(cmd.fx, cmd.generation);
            break;
        case CMD_BLEND:
            for (int i = 0; i < MAX_SEQ; i++) {
                if (cmd.generation != s_generation) {
                    break;
                }
                if (cmd.aids[i][0] != '\0') {
                    play_aid(cmd.aids[i], cmd.generation);
                    if (i < MAX_SEQ - 1 && cmd.aids[i + 1][0] != '\0') {
                        vTaskDelay(pdMS_TO_TICKS(BLEND_GAP_MS));
                    }
                }
            }
            break;
        case CMD_REPLAY:
            if (s_last_aid[0] != '\0') {
                play_aid(s_last_aid, cmd.generation);
            }
            break;
        default:
            break;
        }
    }
}

esp_err_t audio_player_start(void)
{
    if (s_task != NULL) {
        return ESP_OK;
    }
    esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "音频初始化失败(%d)，发音不可用", err);
        return err;
    }
    err = bsp_audio_set_format(16000, 16, 1);
    if (err != ESP_OK) {
        return err;
    }
    s_queue = xQueueCreate(AUDIO_QUEUE_LEN, sizeof(audio_cmd_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(audio_task, "py_audio", AUDIO_TASK_STACK, NULL,
                    AUDIO_TASK_PRIO, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_speech_ok = true;
    return ESP_OK;
}

void audio_player_stop(void)
{
    if (s_task != NULL) {
        s_generation++;
        vTaskDelete(s_task);
        s_task = NULL;
    }
    if (s_queue != NULL) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
    s_speech_ok = false;
}

static void enqueue(audio_cmd_t *cmd, bool replace_speech)
{
    if (s_queue == NULL) {
        return;
    }
    if (replace_speech) {
        s_generation++;
        cmd->generation = s_generation;
        xQueueReset(s_queue);
    } else {
        cmd->generation = s_generation;
    }
    if (xQueueSend(s_queue, cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "音频队列满，忽略命令 %d", (int)cmd->type);
    }
}

void audio_player_play_clip(const char *aid)
{
    audio_cmd_t cmd = {0};
    cmd.type = CMD_CLIP;
    snprintf(cmd.aids[0], sizeof(cmd.aids[0]), "%s", aid ? aid : "");
    snprintf(s_last_aid, sizeof(s_last_aid), "%s", aid ? aid : "");
    enqueue(&cmd, true);
}

void audio_player_play_fx(audio_fx_t fx)
{
    audio_cmd_t cmd = {0};
    cmd.type = CMD_FX;
    cmd.fx = fx;
    enqueue(&cmd, false);
}

void audio_player_play_blend(const char *initial_aid, const char *final_aid,
                             const char *syllable_aid)
{
    audio_cmd_t cmd = {0};
    cmd.type = CMD_BLEND;
    snprintf(cmd.aids[0], sizeof(cmd.aids[0]), "%s", initial_aid ? initial_aid : "");
    snprintf(cmd.aids[1], sizeof(cmd.aids[1]), "%s", final_aid ? final_aid : "");
    snprintf(cmd.aids[2], sizeof(cmd.aids[2]), "%s", syllable_aid ? syllable_aid : "");
    snprintf(s_last_aid, sizeof(s_last_aid), "%s", syllable_aid ? syllable_aid : "");
    enqueue(&cmd, true);
}

void audio_player_replay(void)
{
    audio_cmd_t cmd = {0};
    cmd.type = CMD_REPLAY;
    enqueue(&cmd, true);
}

bool audio_player_speech_ok(void)
{
    return s_speech_ok;
}
