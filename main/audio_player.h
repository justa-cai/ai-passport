/* 统一音频播放器：发音 clip（ADPCM 解码）+ 合成音效 + 拼读序列。
 * 单 FreeRTOS worker（阻塞 PCM 写入只在此任务），命令队列 + 世代计数取消。
 * 模式参考 tetris 分支 rtttl_player 的生命周期语义，本实现为独立编写。*/
#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    FX_NONE = 0,
    FX_PAGE,    /* 翻页：G5 短音 */
    FX_CORRECT, /* 答对：C6-E6-G6 上行 */
    FX_WRONG,   /* 答错：E4-C4 下行 */
    FX_STAR,    /* 得星：C6 E6 G6 C7 */
} audio_fx_t;

/* 初始化（bsp_audio_init + 16k/16/1 + worker）。失败后 speech_ok() 为 false，
 * UI 据此隐藏播放提示（音频是软依赖）。*/
esp_err_t audio_player_start(void);
void audio_player_stop(void);

/* 播放发音 clip（ASCII aid，索引未命中则记日志忽略；替换当前语音）。*/
void audio_player_play_clip(const char *aid);
/* 音效（不取消进行中的语音；排队跟随）。*/
void audio_player_play_fx(audio_fx_t fx);
/* 拼读序列：声母呼读 → 韵母 → 整体音节（任一为 NULL 则跳过该段）。*/
void audio_player_play_blend(const char *initial_aid, const char *final_aid,
                             const char *syllable_aid);
/* 重听当前语音（测验用）。*/
void audio_player_replay(void);

bool audio_player_speech_ok(void);

#endif /* AUDIO_PLAYER_H */
