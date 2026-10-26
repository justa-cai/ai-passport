/* 应用字体注册：生成的 Noto Sans CJK SC 子集 + Montserrat 兜底链。*/
#ifndef APP_FONTS_H
#define APP_FONTS_H

#include "lvgl.h"

/* 初始化（LVGL 就绪后、创建任何控件前调用一次）：把 const 字体描述符
 * 拷贝到可写副本并接 fallback。此后全局使用下列可写句柄。*/
void app_fonts_init(void);

extern lv_font_t g_font_small;  /* 16px 全集：提示行 */
extern lv_font_t g_font_ui;     /* 22px 全集：正文 */
extern lv_font_t g_font_hanzi;  /* 44px 全集：测验汉字/星 */
extern lv_font_t g_font_big;    /* 56px latin：拼音大字（封闭集，无兜底）*/

#endif /* APP_FONTS_H */
