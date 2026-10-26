/* 拼音拼读训练 应用层共享接口（屏幕路由与全局状态访问）。*/
#ifndef APP_H
#define APP_H

#include "bsp_button.h"
#include "pinyin_data.h"
#include "progress_codec.h"

typedef enum {
    APP_SCR_HOME = 0,
    APP_SCR_LESSONS,
    APP_SCR_CARD,
    APP_SCR_QUIZ,
} app_screen_id_t;

/* 屏幕切换（内部持 LVGL 锁：exit 旧屏 → enter 新屏）。
 * 仅可从 input_task 上下文调用。*/
void app_nav_goto(app_screen_id_t id);

/* 调试用：控制台直接切屏（screen_dump 的 goto 命令；自持 LVGL 锁）。*/
void app_debug_nav(int screen_id);
/* 调试用：控制台注入按键（screen_dump 的 key 命令；0UP 1DOWN 2OK ×
 * 0PRESS 1CLICK 2DOUBLE 3LONG），复现按键序列用。*/
void app_debug_key(int btn, int ev);

/* 全局进度快照（app_progress 持有；直接改后调 app_progress_commit）*/
pinyin_progress_t *app_progress(void);
void app_progress_commit(void);

#endif /* APP_H */
