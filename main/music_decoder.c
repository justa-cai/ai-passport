// main/music_decoder.c —— music_decoder.h 的实现。
#include "music_decoder.h"

#include <string.h>

#include "esp_audio_dec.h"
#include "esp_audio_dec_default.h"
#include "esp_log.h"

static const char *TAG = "music_decoder";

// MP3 一帧最大 PCM:1152 采样 × 2 声道 × 16bit。
#define FRAME_PCM_BYTES 4608

static bool s_registered;

esp_err_t music_decoder_open_mp3(music_decoder_t *d) {
    memset(d, 0, sizeof(*d));
    if (!s_registered) {
        // sdkconfig 只开了 MP3,这里注册的是默认集合中实际编进去的部分。
        const esp_err_t err = esp_audio_dec_register_default();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "解码器注册失败:%s", esp_err_to_name(err));
            return err;
        }
        s_registered = true;
    }
    esp_audio_dec_cfg_t cfg = {
        .type = ESP_AUDIO_TYPE_MP3,
    };
    const esp_err_t err = esp_audio_dec_open(&cfg, &d->dec);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MP3 解码器打开失败:%s", esp_err_to_name(err));
    }
    return err;
}

void music_decoder_close(music_decoder_t *d) {
    if (d->dec) {
        esp_audio_dec_close(d->dec);
        d->dec = NULL;
    }
}

// 立体声交错 16bit → 单声道 (L+R)/2。
static size_t downmix(int16_t *pcm, size_t bytes) {
    const size_t samples = bytes / 4;
    for (size_t i = 0; i < samples; i++) {
        const int32_t l = pcm[2 * i];
        const int32_t r = pcm[2 * i + 1];
        pcm[i] = (int16_t)((l + r) / 2);
    }
    return samples * 2;
}

int music_decoder_feed(music_decoder_t *d, const uint8_t *in, size_t in_len,
                       int16_t *out, size_t out_cap, size_t *consumed) {
    if (!d->dec) return -1;
    // 输出缓冲按最坏一帧给足;调用方栈/静态缓冲不够直接算参数错。
    if (out_cap < FRAME_PCM_BYTES) return -1;

    static int16_t frame[FRAME_PCM_BYTES / 2];  // 2.25KB,player 任务独占
    esp_audio_dec_in_raw_t raw = {
        .buffer = (uint8_t *)in, .len = (uint32_t)in_len,
    };
    esp_audio_dec_out_frame_t fout = {
        .buffer = (uint8_t *)frame, .len = FRAME_PCM_BYTES,
    };
    size_t out_bytes = 0;
    while (raw.len > 0 && out_bytes + FRAME_PCM_BYTES <= out_cap) {
        const esp_audio_err_t ret = esp_audio_dec_process(d->dec, &raw, &fout);
        if (ret == ESP_AUDIO_ERR_OK && fout.decoded_size > 0) {
            if (!d->info_valid) {
                esp_audio_dec_info_t info;
                if (esp_audio_dec_get_info(d->dec, &info) == ESP_AUDIO_ERR_OK &&
                    info.sample_rate) {
                    d->sample_rate = info.sample_rate;
                    d->channels = info.channel ? info.channel : 1;
                    d->info_valid = true;
                    ESP_LOGI(TAG, "MP3 流参数:%luHz %uch",
                             (unsigned long)d->sample_rate, d->channels);
                }
            }
            size_t got = fout.decoded_size;
            if (d->channels == 2) got = downmix(frame, got);
            memcpy((uint8_t *)out + out_bytes, frame, got);
            out_bytes += got;
        }
        if (ret != ESP_AUDIO_ERR_OK && raw.consumed == 0) {
            // 坏帧且零消耗:跳 1 字节重同步,避免死循环。
            raw.buffer += 1;
            raw.len -= 1;
            continue;
        }
        raw.buffer += raw.consumed;
        raw.len -= raw.consumed;
        if (ret == ESP_AUDIO_ERR_INVALID_PARAMETER || ret == ESP_AUDIO_ERR_MEM_LACK) {
            ESP_LOGE(TAG, "解码不可恢复:%d", (int)ret);
            if (consumed) *consumed = in_len;  // 调用方应整段丢弃
            return -1;
        }
    }
    if (consumed) *consumed = in_len - raw.len;
    return (int)out_bytes;
}
