// main/music_ui_nav.h —— 屏幕-按键路由与列表分页(纯逻辑,主机可测)。
//
// 按键语义(与 main.c 头注释一致):
//   列表类屏幕:UP/DOWN 短按移动选中(clamp 不循环,翻页跟随),
//               OK 短按进入/播放,OK 长按返回上级;
//   播放页:    OK 短按暂停/续播,UP/DOWN 短按上一首/下一首,
//               UP/DOWN 长按音量 ±,OK 长按返回列表。
// 本模块只产出意图(action),屏幕构建与绘制在 ui_music.c。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MUS_SCR_HOME = 0,    // 榜单首页(5 个榜单入口)
    MUS_SCR_PROVISION,   // 配网指引页
    MUS_SCR_LIST,        // 歌曲列表(榜单/搜索结果共用)
    MUS_SCR_PLAY,        // 正在播放
    MUS_SCR_SETTINGS,    // 设置(音量/重新配网/关于)
} music_screen_t;

typedef enum {
    MUS_KEY_UP = 0,
    MUS_KEY_DOWN,
    MUS_KEY_OK,
} music_key_t;

typedef enum {
    MUS_KEV_CLICK = 0,   // 单击(含按下即抬起的短按)
    MUS_KEV_DOUBLE,      // 双击
    MUS_KEV_LONG,        // 长按
} music_kev_t;

// 路由产出的意图(一次按键可组合多个标志)。
typedef struct {
    bool change_screen;        // 切到 new_screen
    music_screen_t new_screen;
    bool redraw_list;          // 选中/翻页变化,列表需重绘
    bool play_cursor;          // 播放当前选中曲(进入播放页)
    bool toggle_pause;
    bool next_track;
    bool prev_track;
    int8_t vol_delta;          // 长按音量:+1/-1/0
    bool settings_select;      // 设置页:OK 选中某项
    uint8_t settings_index;    // 被选中的设置项下标(0 音量/1 重新配网/2 关于)
} music_ui_action_t;

#define MUSIC_UI_ROWS_MAX 8  // 240x320 下列表每页行数上限

typedef struct {
    music_screen_t screen;
    uint8_t list_len;      // 当前列表条数(0=空)
    uint8_t cursor;        // 绝对选中下标
    uint8_t page_top;      // 当前页首行绝对下标
    uint8_t rows_per_page; // 固定于构造(1..MUSIC_UI_ROWS_MAX)
} music_ui_nav_t;

void music_ui_nav_init(music_ui_nav_t *nav, uint8_t rows_per_page);

// 列表内容变化(进入新榜单/搜索完成/清空):重置选中与翻页。
void music_ui_nav_set_list(music_ui_nav_t *nav, uint8_t len);

// 直接切屏(播放自动开始/网络状态变化等系统侧驱动)。
void music_ui_nav_goto(music_ui_nav_t *nav, music_screen_t scr);

// 按键路由。返回 true 表示产生了需要响应的意图。
bool music_ui_nav_key(music_ui_nav_t *nav, music_key_t key, music_kev_t kev,
                      music_ui_action_t *act);

// 光标所在页号/总页数(0 列表时页数记 1,避免除零)。
uint8_t music_ui_nav_page(const music_ui_nav_t *nav);
uint8_t music_ui_nav_pages(const music_ui_nav_t *nav);

const char *music_screen_name(music_screen_t s);
