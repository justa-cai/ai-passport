// main/ui_music.h —— musicdl 播放器应用的视觉外壳(深色主题)。
//
// 派生应用必须自带界面:本模块是该应用自己的屏幕/控件工厂,不复用基线 demo
// 的测试菜单与 ui_pixel 视觉壳。所有屏幕共享:纯深背景 + 顶部状态栏
// (左:网络状态,右:电池百分比 —— 仓库编码规范强制项,电量计不可用时隐藏)。
#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#include "music_song.h"

// 配色。全部经 lv_color_hex 使用(RGB888)。
#define UI_MUSIC_BG         0x0E1116  // 页面背景
#define UI_MUSIC_PANEL      0x161B22  // 一级面板
#define UI_MUSIC_ELEVATED   0x1F2733  // 二级面板/选中行
#define UI_MUSIC_ACCENT     0x34D399  // 强调色(薄荷绿)
#define UI_MUSIC_ACCENT_DIM 0x1D6E54  // 强调色暗态
#define UI_MUSIC_TEXT       0xE8EDF2  // 主文本
#define UI_MUSIC_MUTED      0x8B98A5  // 次要文本
#define UI_MUSIC_DANGER     0xF87171  // 错误
#define UI_MUSIC_WARN       0xFBBF24  // 缓冲/提醒

// 屏幕几何:240x320,顶部 20px 状态栏 + 28px 标题行,内容区自 y=50 起。
#define UI_MUSIC_CONTENT_TOP 50

// 全局缺省字体切换点:接入中文点阵字体(music_fonts_init)后一处生效,
// 避免散落在每个 label 创建点上。
void ui_music_set_default_font(const lv_font_t *font);

// 创建一张全屏深色页面,顶部 20px 状态栏 + 标题行,返回页面对象。
// 状态栏由本模块内部维护(网络/电量定时刷新),应用层只管往页面里加内容。
lv_obj_t *ui_music_screen_create(const char *title);

// 网络状态图标:-1 = 未联网(隐藏图标),0 = 配网热点中,1 = 已联网。
void ui_music_status_set_net(int mode);

// 手动触发一次状态栏刷新(电量/网络内部定时器之外的即时更新入口)。
void ui_music_status_refresh(void);

// 面板/行控件:音乐应用的列表行外观(选中 = 左侧 4px 强调色竖条 + 抬亮底色)。
lv_obj_t *ui_music_row_create(lv_obj_t *parent, int y, int height);
void ui_music_row_set_selected(lv_obj_t *row, bool selected);

// 文本 label 快捷创建:缺省字体、主文本色、可选换行模式。
lv_obj_t *ui_music_label_create(lv_obj_t *parent, int x, int y, int w,
                                const char *text, bool wrap);

// ---- S0 启动页:应用名 + 一句副标题,居中 ----
lv_obj_t *ui_music_boot_screen_create(const char *app, const char *sub);

// ---- S1 配网页:热点名 + 网页地址 + 三步指引 ----
lv_obj_t *ui_music_provision_screen_create(const char *ap_name);

// ---- 通用 8 行列表页(S2 榜单首页 / S3 歌曲列表 / S5 设置共用) ----
#define UI_MUSIC_LIST_ROWS 8

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *rows[UI_MUSIC_LIST_ROWS];   // 行容器
    lv_obj_t *num[UI_MUSIC_LIST_ROWS];    // 行首序号
    lv_obj_t *main[UI_MUSIC_LIST_ROWS];   // 主文本(曲名/设置项)
    lv_obj_t *sub[UI_MUSIC_LIST_ROWS];    // 次文本(歌手/取值,右对齐)
    lv_obj_t *page;                       // 右上角页码 "2/3"
    lv_obj_t *hint;                       // 空列表提示(居中)
} ui_music_list_t;

void ui_music_list_screen_build(ui_music_list_t *ui, const char *title);
void ui_music_list_row_set(ui_music_list_t *ui, uint8_t row, const char *num_text,
                           const char *main_text, const char *sub_text, bool selected);
void ui_music_list_page_set(ui_music_list_t *ui, uint8_t page, uint8_t pages);
void ui_music_list_hint_set(ui_music_list_t *ui, const char *text);  // NULL/"" = 隐藏

// ---- S4 播放页:曲名/歌手/状态行/进度条/时长/EQ/音量条 ----
typedef struct {
    lv_obj_t *screen;
    lv_obj_t *title;
    lv_obj_t *artist;
    lv_obj_t *state;      // 状态行:解析中/缓冲中/播放中/已暂停…
    lv_obj_t *bar;        // 播放进度
    lv_obj_t *time_cur;
    lv_obj_t *time_total;
    lv_obj_t *eq[5];      // 五柱均衡器动画
    lv_obj_t *vol;        // 音量条(瞬时显示)
} ui_music_play_t;

void ui_music_play_screen_build(ui_music_play_t *ui);
void ui_music_play_song_set(ui_music_play_t *ui, const music_song_t *song);
void ui_music_play_state_set(ui_music_play_t *ui, const char *text);
void ui_music_play_progress_set(ui_music_play_t *ui, uint8_t pct,
                                uint32_t cur_s, uint32_t total_s);
void ui_music_play_volume_set(ui_music_play_t *ui, uint8_t pct);
void ui_music_play_eq_set(ui_music_play_t *ui, bool playing);  // EQ 动画开关
