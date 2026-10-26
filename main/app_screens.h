/* 屏幕模块接口：enter/exit 在 LVGL 锁内由路由调用；key 在锁内由 input_task 调用。
 * key 返回 false 表示未处理（路由将 OK 长按解释为返回上一屏）。*/
#ifndef APP_SCREENS_H
#define APP_SCREENS_H

#include "bsp_button.h"

void app_home_enter(void);
void app_home_exit(void);
bool app_home_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void app_lessons_enter(void);
void app_lessons_exit(void);
bool app_lessons_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void app_card_enter(void);
void app_card_exit(void);
bool app_card_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void app_quiz_enter(void);
void app_quiz_exit(void);
bool app_quiz_key(bsp_btn_t btn, bsp_btn_ev_t ev);

#endif /* APP_SCREENS_H */
