// main/music_fonts.h —— musicdl 应用字体接入。
#pragma once

#include "lvgl.h"

// 生成的 16px 中文子集字体(4000 字形,tools/font_tools/gen_music_font.py)。
extern lv_font_t music_font_16;

// 挂接 fallback 链(montserrat_14,补齐 LV_SYMBOL_* 等图标码位)。
// 幂等;在任何控件引用字体前调用一次。
void music_fonts_init(void);
