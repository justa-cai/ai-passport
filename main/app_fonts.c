#include "app_fonts.h"

/* 生成的字体子集（assets/fonts/，由 tools/gen_app_fonts.py 产出）。*/
LV_FONT_DECLARE(pinyin_font_16);
LV_FONT_DECLARE(pinyin_font_22);
LV_FONT_DECLARE(pinyin_font_44);
LV_FONT_DECLARE(pinyin_font_56);

lv_font_t g_font_small;
lv_font_t g_font_ui;
lv_font_t g_font_hanzi;
lv_font_t g_font_big;

void app_fonts_init(void)
{
    g_font_small = pinyin_font_16;
    g_font_small.fallback = &lv_font_montserrat_14;

    g_font_ui = pinyin_font_22;
    g_font_ui.fallback = &lv_font_montserrat_14;

    g_font_hanzi = pinyin_font_44;
    g_font_hanzi.fallback = &lv_font_montserrat_14;

    /* 56px 为拼音专用封闭集（ASCII+带调元音+符号），保持无兜底，
     * 避免 Montserrat 同号基线差异；其覆盖由 test_font_coverage.py 保证。*/
    g_font_big = pinyin_font_56;
}
