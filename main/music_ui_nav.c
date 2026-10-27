// main/music_ui_nav.c —— music_ui_nav.h 的实现。
#include "music_ui_nav.h"

#include <string.h>

static void act_reset(music_ui_action_t *act) {
    if (act) memset(act, 0, sizeof(*act));
}

void music_ui_nav_init(music_ui_nav_t *nav, uint8_t rows_per_page) {
    memset(nav, 0, sizeof(*nav));
    nav->screen = MUS_SCR_HOME;
    nav->rows_per_page = rows_per_page < 1 ? 1 : rows_per_page;
}

void music_ui_nav_set_list(music_ui_nav_t *nav, uint8_t len) {
    nav->list_len = len;
    nav->cursor = 0;
    nav->page_top = 0;
}

void music_ui_nav_goto(music_ui_nav_t *nav, music_screen_t scr) {
    nav->screen = scr;
}

const char *music_screen_name(music_screen_t s) {
    switch (s) {
        case MUS_SCR_HOME: return "HOME";
        case MUS_SCR_PROVISION: return "PROVISION";
        case MUS_SCR_LIST: return "LIST";
        case MUS_SCR_PLAY: return "PLAY";
        case MUS_SCR_SETTINGS: return "SETTINGS";
        default: return "?";
    }
}

uint8_t music_ui_nav_page(const music_ui_nav_t *nav) {
    return nav->cursor / nav->rows_per_page;
}

uint8_t music_ui_nav_pages(const music_ui_nav_t *nav) {
    const uint8_t n = nav->list_len ? nav->list_len : 1;
    return (uint8_t)((n + nav->rows_per_page - 1) / nav->rows_per_page);
}

// 列表内移动:clamp 不循环;越出当前页时滚动 page_top。
static void move_cursor(music_ui_nav_t *nav, int dir, music_ui_action_t *act) {
    if (nav->list_len == 0) return;
    int c = (int)nav->cursor + dir;
    if (c < 0) c = 0;
    if (c > (int)nav->list_len - 1) c = nav->list_len - 1;
    if (c == (int)nav->cursor) return;
    nav->cursor = (uint8_t)c;
    if (nav->cursor < nav->page_top)
        nav->page_top = nav->cursor;
    else if (nav->cursor >= (uint8_t)(nav->page_top + nav->rows_per_page))
        nav->page_top = (uint8_t)(nav->cursor - nav->rows_per_page + 1);
    if (act) act->redraw_list = true;
}

bool music_ui_nav_key(music_ui_nav_t *nav, music_key_t key, music_kev_t kev,
                      music_ui_action_t *act) {
    act_reset(act);
    switch (nav->screen) {
        case MUS_SCR_HOME:
            // 榜单首页:上下移动榜单选中,OK 进入榜单,OK 长按进设置。
            if (kev == MUS_KEV_CLICK) {
                if (key == MUS_KEY_OK) {
                    if (act) {
                        act->change_screen = true;
                        act->new_screen = MUS_SCR_LIST;
                        act->redraw_list = true;
                    }
                    nav->screen = MUS_SCR_LIST;
                    return true;
                }
                if (key == MUS_KEY_UP) {
                    move_cursor(nav, -1, act);
                    return act && act->redraw_list;
                }
                if (key == MUS_KEY_DOWN) {
                    move_cursor(nav, 1, act);
                    return act && act->redraw_list;
                }
            }
            if (kev == MUS_KEV_LONG && key == MUS_KEY_OK) {
                if (act) {
                    act->change_screen = true;
                    act->new_screen = MUS_SCR_SETTINGS;
                }
                nav->screen = MUS_SCR_SETTINGS;
                return true;
            }
            return false;

        case MUS_SCR_LIST:
            if (key == MUS_KEY_UP && kev == MUS_KEV_CLICK) {
                move_cursor(nav, -1, act);
                return act && act->redraw_list;
            }
            if (key == MUS_KEY_DOWN && kev == MUS_KEV_CLICK) {
                move_cursor(nav, 1, act);
                return act && act->redraw_list;
            }
            if (key == MUS_KEY_OK && kev == MUS_KEV_CLICK) {
                if (nav->list_len == 0) return false;
                if (act) {
                    act->play_cursor = true;
                    act->change_screen = true;
                    act->new_screen = MUS_SCR_PLAY;
                }
                nav->screen = MUS_SCR_PLAY;
                return true;
            }
            if (key == MUS_KEY_OK && kev == MUS_KEV_LONG) {
                if (act) {
                    act->change_screen = true;
                    act->new_screen = MUS_SCR_HOME;
                }
                nav->screen = MUS_SCR_HOME;
                return true;
            }
            return false;

        case MUS_SCR_PLAY:
            if (kev == MUS_KEV_LONG) {
                if (key == MUS_KEY_OK) {
                    if (act) {
                        act->change_screen = true;
                        act->new_screen = MUS_SCR_LIST;
                    }
                    nav->screen = MUS_SCR_LIST;
                    return true;
                }
                if (key == MUS_KEY_UP && act) {
                    act->vol_delta = +1;
                    return true;
                }
                if (key == MUS_KEY_DOWN && act) {
                    act->vol_delta = -1;
                    return true;
                }
                return false;
            }
            if (kev == MUS_KEV_CLICK) {
                if (key == MUS_KEY_OK) {
                    if (act) act->toggle_pause = true;
                    return true;
                }
                if (key == MUS_KEY_UP) {
                    if (act) act->prev_track = true;
                    return true;
                }
                if (key == MUS_KEY_DOWN) {
                    if (act) act->next_track = true;
                    return true;
                }
            }
            return false;

        case MUS_SCR_SETTINGS:
            // 复用列表光标:list_len = 设置项数(进入时由 UI 设置)。
            if (kev == MUS_KEV_CLICK && key == MUS_KEY_UP) {
                move_cursor(nav, -1, act);
                return act && act->redraw_list;
            }
            if (kev == MUS_KEV_CLICK && key == MUS_KEY_DOWN) {
                move_cursor(nav, 1, act);
                return act && act->redraw_list;
            }
            if (kev == MUS_KEV_CLICK && key == MUS_KEY_OK) {
                if (nav->list_len == 0) return false;
                if (act) {
                    act->settings_select = true;
                    act->settings_index = nav->cursor;
                }
                return true;
            }
            if (kev == MUS_KEV_LONG && key == MUS_KEY_OK) {
                if (act) {
                    act->change_screen = true;
                    act->new_screen = MUS_SCR_HOME;
                }
                nav->screen = MUS_SCR_HOME;
                return true;
            }
            return false;

        case MUS_SCR_PROVISION:
            // 配网页进度由网络事件驱动;OK 长按可返回首页(如配网暂不可用)。
            if (kev == MUS_KEV_LONG && key == MUS_KEY_OK) {
                if (act) {
                    act->change_screen = true;
                    act->new_screen = MUS_SCR_HOME;
                }
                nav->screen = MUS_SCR_HOME;
                return true;
            }
            return false;
    }
    return false;  // 未识别的屏幕值(防御,正常到不了这里)
}
