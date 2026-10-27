// main/music_player.c —— music_player.h 的实现。
//
// 单任务同步管线:解析(convert_url3,纯 http)→ https→http 降级 →
// 建流(2KB 分块)→ MP3 解码(立体声混缩单声道)→ bsp_audio_write。
// I2S 阻塞写天然按采样时钟节流;网络抖动靠 I2S DMA 深度吸收,v1 不做
// 压缩环形缓冲(真机欠载时再加,见文档"未验证项")。
// 失败语义与 music_player_sm 一致:建流/解码失败先重试当前曲(重新
// 解析),连败 2 次发 skip_notice 跳曲;解析失败回到 IDLE 由外层补试一次。
#include "music_player.h"

#include <string.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "music_decoder.h"
#include "music_http.h"
#include "music_json.h"
#include "music_kuwo.h"

static const char *TAG = "music_player";

// chunk 输出缓冲:两个混缩帧(4608B)一次 I2S 写,粒度约 52ms@44.1kHz。
#define FRAME_PCM_MONO_BYTES 4608

typedef struct {
    uint8_t kind;   // 1 = play
    music_song_t song;
} player_cmd_t;

static music_player_sm_t s_sm;
static QueueHandle_t s_cmdq;
static TaskHandle_t s_task;
static music_player_event_fn s_event_fn;
static void *s_event_user;

static music_player_status_t s_status;      // 快照(临界区内写,事件载荷用本地副本)
static portMUX_TYPE s_status_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_abort;               // 新歌/停止:中断当前流
static volatile bool s_pause_req;           // TOGGLE 请求(播放循环边界处理)
static volatile bool s_stop_req;
static volatile uint8_t s_volume = 70;
static volatile bool s_vol_dirty;
static bool s_audio_inited;
static uint32_t s_audio_rate;               // 0 = 未设置
static uint32_t s_samples;                  // 已解码采样数(单声道)

// 输入进位缓冲:网络分块与 MP3 帧边界不对齐,剩余字节留到下一段。
#define IN_BUF_CAP 4096
static uint8_t s_inbuf[IN_BUF_CAP];
static size_t s_inlen;
static music_decoder_t s_dec;
static bool s_decode_failed;
static bool s_stream_ok_sent;
static uint32_t s_last_notify_s;

// ---- 状态通知 -------------------------------------------------------------

static void publish(bool ended) {
    music_player_status_t st;
    taskENTER_CRITICAL(&s_status_mux);
    st = s_status;
    taskEXIT_CRITICAL(&s_status_mux);
    st.state = s_sm.state;
    st.track_ended = ended;
    st.total_s = st.song.duration_s;
    if (s_audio_rate) {
        st.cur_s = s_samples / s_audio_rate;
        st.progress_pct = st.total_s ? (uint8_t)(st.cur_s * 100 / st.total_s) : 0;
        if (st.progress_pct > 100) st.progress_pct = 100;
    }
    taskENTER_CRITICAL(&s_status_mux);
    s_status = st;
    taskEXIT_CRITICAL(&s_status_mux);
    if (s_event_fn) s_event_fn(&st, s_event_user);
}

static void sm_step(music_pe_t ev, music_player_action_t *act) {
    music_player_action_t local;
    if (!act) act = &local;
    const music_ps_t before = s_sm.state;
    music_player_sm_step(&s_sm, ev, act);
    if (act->stop_stream) {
        music_decoder_close(&s_dec);
        s_inlen = 0;
        s_audio_rate = 0;
        s_stream_ok_sent = false;
    }
    if (s_sm.state != before) publish(false);
}

// ---- 解析播放地址 ---------------------------------------------------------

static bool resolve_chunk(const char *data, size_t len, void *user) {
    music_json_parser_t *p = user;
    (void)music_json_feed(p, data, len);
    return !p->error && !p->done;
}

// convert_url3 返回 https CDN 链接;同路径 http 可用(已实测),降级免 TLS。
static void downgrade_scheme(char *url) {
    if (strncmp(url, "https://", 8) == 0) {
        memmove(url + 4, url + 5, strlen(url) - 5 + 1);
    }
}

static bool resolve_url(uint32_t rid, char *url, size_t cap) {
    char api[MUSIC_KUWO_URL_MAX];
    if (music_kuwo_resolve_url(rid, api, sizeof(api)) == (size_t)-1) return false;
    music_json_parser_t p;
    music_json_url_init(&p, url, cap);
    const int status = music_http_get(api, resolve_chunk, &p);
    if (status != 200 || p.error || !url[0]) {
        ESP_LOGW(TAG, "解析失败:HTTP=%d rid=%lu", status, (unsigned long)rid);
        return false;
    }
    downgrade_scheme(url);
    return true;
}

// ---- 建流 + 解码 + 输出 ---------------------------------------------------

static void handle_volume_request(void) {
    if (s_vol_dirty) {
        bsp_audio_set_volume(s_volume);
        s_vol_dirty = false;
    }
}

static bool ensure_audio(void) {
    if (!s_audio_inited) {
        const esp_err_t err = bsp_audio_init();  // 懒初始化:首次播放才占内存
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "音频初始化失败:%s", esp_err_to_name(err));
            return false;
        }
        s_audio_inited = true;
    }
    handle_volume_request();
    return true;
}

static void handle_pause_request(void) {
    if (!s_pause_req) return;
    s_pause_req = false;
    sm_step(MUS_PE_TOGGLE, NULL);
    handle_volume_request();
}

static bool stream_chunk(const char *data, size_t len, void *user) {
    (void)user;
    if (s_abort) return false;
    handle_pause_request();
    handle_volume_request();

    if (s_inlen + len > IN_BUF_CAP) {  // 防御:进位异常膨胀按解码失败处理
        s_decode_failed = true;
        return false;
    }
    memcpy(s_inbuf + s_inlen, data, len);
    s_inlen += len;

    static int16_t pcm[FRAME_PCM_MONO_BYTES / 2];
    while (s_inlen > 0) {
        size_t consumed = 0;
        const int n = music_decoder_feed(&s_dec, s_inbuf, s_inlen, pcm,
                                         sizeof(pcm), &consumed);
        if (n < 0) {
            ESP_LOGW(TAG, "解码失败,终止本曲");
            s_decode_failed = true;
            return false;
        }
        s_inlen -= consumed;
        memmove(s_inbuf, s_inbuf + consumed, s_inlen);
        if (n == 0) break;             // 凑帧中
        if (consumed == 0) break;      // 防御死循环

        if (s_dec.info_valid && s_dec.sample_rate != s_audio_rate) {
            if (bsp_audio_set_format(s_dec.sample_rate, 16, 1) != ESP_OK) {
                ESP_LOGE(TAG, "采样率设置失败:%luHz",
                         (unsigned long)s_dec.sample_rate);
                s_decode_failed = true;
                return false;
            }
            s_audio_rate = s_dec.sample_rate;
        }
        if (!s_stream_ok_sent) {
            sm_step(MUS_PE_STREAM_OK, NULL);  // 首包音频已到:PLAYING
            s_stream_ok_sent = true;
        }
        if (s_sm.state != MUS_PS_PAUSED) {
            bsp_audio_write(pcm, (size_t)n);  // 阻塞写 = 采样时钟节流
            s_samples += (uint32_t)n / 2;
            const uint32_t sec = s_samples / (s_audio_rate ? s_audio_rate : 1);
            if (sec != s_last_notify_s) {     // 每秒推一次进度
                s_last_notify_s = sec;
                publish(false);
            }
        }
        // 暂停:输出丢弃但流继续拉(保活,恢复零延迟)。
    }
    return !s_abort;
}

typedef enum { SR_END, SR_ABORT, SR_FAIL } stream_rc_t;

static stream_rc_t stream_and_decode(const char *url) {
    if (music_decoder_open_mp3(&s_dec) != ESP_OK) return SR_FAIL;
    if (!ensure_audio()) return SR_FAIL;
    s_inlen = 0;
    s_decode_failed = false;
    s_last_notify_s = 0;
    const int status = music_http_get(url, stream_chunk, NULL);
    music_decoder_close(&s_dec);
    if (s_abort) return SR_ABORT;
    if (status != 200) {
        ESP_LOGW(TAG, "建流失败:HTTP=%d", status);
        return SR_FAIL;
    }
    return s_decode_failed ? SR_FAIL : SR_END;
}

// ---- 单曲播放流程 ---------------------------------------------------------

static void play_song(const music_song_t *song) {
    s_samples = 0;
    s_abort = false;
    taskENTER_CRITICAL(&s_status_mux);
    s_status.song = *song;
    s_status.cur_s = 0;
    s_status.progress_pct = 0;
    taskEXIT_CRITICAL(&s_status_mux);

    music_player_action_t act;
    sm_step(MUS_PE_SELECT, &act);   // → RESOLVING + begin_resolve

    char url[MUSIC_KUWO_URL_MAX] = "";
    uint8_t resolve_attempts = 0;
    while (!s_abort) {
        if (act.begin_resolve) {
            resolve_attempts++;
            if (!resolve_url(song->rid, url, sizeof(url))) {
                // 状态机:解析失败 → IDLE;外层最多补试一次(接口偶发抖动)。
                sm_step(MUS_PE_RESOLVE_FAIL, &act);
                if (s_sm.state == MUS_PS_IDLE && resolve_attempts < 2) {
                    sm_step(MUS_PE_SELECT, &act);
                }
                continue;
            }
            sm_step(MUS_PE_RESOLVE_OK, &act);   // → BUFFERING + begin_stream
        }
        if (act.begin_stream) {
            const stream_rc_t rc = stream_and_decode(url);
            if (rc == SR_ABORT) {
                sm_step(MUS_PE_STOP, &act);
                return;
            }
            if (rc == SR_END) {
                sm_step(MUS_PE_STOP, &act);
                publish(true);   // 自然播完:主应用进下一首
                return;
            }
            sm_step(s_decode_failed ? MUS_PE_DECODE_FAIL : MUS_PE_STREAM_FAIL, &act);
            // stop_stream 已在 sm_step 兑现;begin_resolve 决定是否重试当前曲。
            if (act.skip_notice) {   // 连败 2 次:收工并让主应用跳曲
                sm_step(MUS_PE_STOP, &act);
                publish(true);
                return;
            }
            continue;
        }
        break;   // 既不解析也不建流:异常兜底退出
    }
}

// ---- 任务与公开接口 -------------------------------------------------------

static void player_task(void *arg) {
    (void)arg;
    player_cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_cmdq, &cmd, portMAX_DELAY) == pdTRUE && cmd.kind == 1) {
            play_song(&cmd.song);
        }
        // 空闲期也消化暂停/音量请求(SM 对无效事件无操作)。
        handle_pause_request();
        handle_volume_request();
        if (s_stop_req) {
            s_stop_req = false;
            s_abort = true;
            sm_step(MUS_PE_STOP, NULL);
        }
    }
}

void music_player_start(void) {
    if (s_task) return;
    music_player_sm_init(&s_sm);
    s_cmdq = xQueueCreate(4, sizeof(player_cmd_t));
    if (!s_cmdq || xTaskCreate(player_task, "music_player", 10240, NULL, 5,
                               &s_task) != pdPASS) {
        ESP_LOGE(TAG, "player 任务创建失败");
        s_task = NULL;
        if (s_cmdq) {
            vQueueDelete(s_cmdq);
            s_cmdq = NULL;
        }
    }
}

void music_player_set_event_cb(music_player_event_fn fn, void *user) {
    s_event_fn = fn;
    s_event_user = user;
}

bool music_player_play(const music_song_t *song) {
    if (!s_cmdq || !song || !song->rid) return false;
    s_abort = true;   // 中断当前流;新命令在任务循环里接着执行
    s_stop_req = false;
    const player_cmd_t cmd = { .kind = 1, .song = *song };
    return xQueueSend(s_cmdq, &cmd, 0) == pdTRUE;
}

void music_player_toggle_pause(void) {
    s_pause_req = true;
}

void music_player_stop(void) {
    s_abort = true;
    s_stop_req = true;
}

uint8_t music_player_volume(void) {
    return s_volume;
}

void music_player_set_volume(uint8_t pct) {
    if (pct > 100) pct = 100;
    s_volume = pct;
    s_vol_dirty = true;
}

void music_player_get_status(music_player_status_t *out) {
    if (!out) return;
    taskENTER_CRITICAL(&s_status_mux);
    *out = s_status;
    taskEXIT_CRITICAL(&s_status_mux);
    out->state = s_sm.state;  // 状态读松散一拍,UI 可接受
}
