// main/music_fonts.c —— musicdl 应用字体接入实现。
#include "music_fonts.h"

#include "lvgl.h"

static bool s_inited;

void music_fonts_init(void) {
    if (s_inited) return;
    s_inited = true;
    // music_font_16 覆盖 ASCII + GB2312 一级 + 中文标点;
    // LV_SYMBOL_* (FontAwesome 区)与生僻字回落 montserrat。
    music_font_16.fallback = &lv_font_montserrat_14;
}
