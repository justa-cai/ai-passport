/* "儿童彩色大卡"视觉系统脚手架：屏幕、顶栏、电池徽标、面板、标签、
 * 序号徽章与轻动效（滑动/弹跳）。全新设计（与基线 ui_pixel 无关）。
 * 三模式主题色：卡片浏览=琥珀、拼读练习=青绿、闯关测验=天蓝；
 * 朱红保留给错误警示。所有函数须在 LVGL 任务内或持 bsp_lvgl_lock 调用。*/
#ifndef APP_UI_H
#define APP_UI_H

#include "lvgl.h"

/* 配色（暖纸 + 墨 + 三模式主题色 + 朱红警示）*/
#define PY_BG 0xF7F2E7u        /* 纸面 */
#define PY_CARD 0xFFFFFFu      /* 卡面 */
#define PY_INK 0x2B2A33u       /* 正文墨色 */
#define PY_SUB 0x6E7480u       /* 次级文字 */
#define PY_RED 0xC9403Au       /* 朱红：错误警示/全局强调 */
#define PY_GREEN 0x2E8B6Eu     /* 青绿：拼读主题 + 正确 */
#define PY_WASH_GREEN 0xE3F0EAu
#define PY_WASH_RED 0xF6E3E1u
#define PY_WRONG 0xC9403Au
#define PY_AMBER 0xE8A33Du     /* 琥珀：卡片浏览主题 + 星 */
#define PY_WASH_AMBER 0xFAECD2u
#define PY_BLUE 0x3E7BD9u      /* 天蓝：测验主题 */
#define PY_WASH_BLUE 0xDEE9F8u
#define PY_LINE 0xD8D2C4u      /* 发丝线 */
#define PY_SLOT 0xEDE7D9u      /* 槽位底 */

/* 创建应用屏幕：纸底 + 顶栏（标题左、电池徽标右）。返回 screen 对象。
 * 电池徽标自建 5s 刷新 lv_timer；app_screen_destroy 负责清理。*/
lv_obj_t *app_screen_create(const char *title);
/* 删除屏幕（连同其电池徽标 timer）。随后才可创建下一屏。*/
void app_screen_destroy(lv_obj_t *screen);

/* 圆角面板与标签便捷构造。*/
lv_obj_t *app_panel_create(lv_obj_t *parent, int x, int y, int w, int h,
                           uint32_t bg, int radius);
lv_obj_t *app_label_create(lv_obj_t *parent, const lv_font_t *font,
                           uint32_t color, const char *text);

/* 圆形序号徽章（数字/字母居中）。返回徽章容器；内部标签可用
 * app_badge_label() 取到以改字色。d 为直径。*/
lv_obj_t *app_badge_create(lv_obj_t *parent, int x, int y, int d,
                           uint32_t bg, const lv_font_t *font,
                           uint32_t fg, const char *text);
lv_obj_t *app_badge_label(lv_obj_t *badge);

/* 轻动效：x 平滑滑动（选中位移动画）。*/
void app_anim_slide_x(lv_obj_t *obj, int32_t to_x, uint16_t ms);
/* 轻动效：以中心为轴的缩放脉冲（播放/得星反馈，256=100%）。*/
void app_anim_pop(lv_obj_t *obj);

#endif /* APP_UI_H */
