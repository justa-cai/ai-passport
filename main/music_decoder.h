// main/music_decoder.h —— MP3 解码器胶水(esp_audio_codec 按需注册)。
//
// 只包 MP3:sdkconfig.defaults 关掉了其它编码,注册函数也只挂 MP3,
// 省掉用不到的解码库(flash/RAM)。输出统一为 16bit 单声道:立体声
// 在本层 (L+R)/2 混缩,BSP 音频口固定单声道。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    void *dec;             // esp_audio_dec_handle_t(不透明)
    uint32_t sample_rate;  // 首帧解码后有效
    uint8_t channels;      // 源流声道数(1/2)
    bool info_valid;
} music_decoder_t;

// 打开一个 MP3 解码器实例(每首新曲重新打开,内部状态干净)。
esp_err_t music_decoder_open_mp3(music_decoder_t *d);

void music_decoder_close(music_decoder_t *d);

// 输入一段 MP3 字节流(任意切分,内部按帧消费),输出混缩后的 16bit
// 单声道 PCM 到 out。返回:
//   >0  写入 out 的字节数(可能为 0:输入还没凑齐完整帧,属正常)
//   -1  不可恢复错误(调用方按解码失败处理)
// *consumed = 本次实际吃掉的输入字节数(≤ in_len):输出缓冲装满时
// 输入会有剩余,调用方把剩余字节留在缓冲区与下一段拼接。
// 首次成功出帧后 d->sample_rate/channels 可用。
int music_decoder_feed(music_decoder_t *d, const uint8_t *in, size_t in_len,
                       int16_t *out, size_t out_cap, size_t *consumed);
