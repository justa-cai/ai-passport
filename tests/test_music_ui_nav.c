// music_ui_nav 屏幕路由与分页的 host 测试。
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>

#include "music_ui_nav.h"

int main(void) {
    music_ui_nav_t nav;
    music_ui_action_t act;

    // ---- 分页数学:20 项/页 8 行 → 3 页;空列表记 1 页 ----
    music_ui_nav_init(&nav, 8);
    assert(nav.screen == MUS_SCR_HOME);
    music_ui_nav_set_list(&nav, 20);
    assert(music_ui_nav_pages(&nav) == 3);
    assert(music_ui_nav_page(&nav) == 0);
    music_ui_nav_set_list(&nav, 0);
    assert(music_ui_nav_pages(&nav) == 1);

    // ---- 首页:上下移动钳制(不循环),OK 进列表,OK 长按进设置 ----
    music_ui_nav_init(&nav, 8);
    music_ui_nav_set_list(&nav, 5);   // 5 个榜单入口
    assert(!music_ui_nav_key(&nav, MUS_KEY_UP, MUS_KEV_CLICK, &act));   // 顶不上移
    assert(nav.cursor == 0);
    assert(music_ui_nav_key(&nav, MUS_KEY_DOWN, MUS_KEV_CLICK, &act));  // →1
    assert(act.redraw_list && nav.cursor == 1);
    for (int i = 0; i < 10; i++) music_ui_nav_key(&nav, MUS_KEY_DOWN, MUS_KEV_CLICK, &act);
    assert(nav.cursor == 4);   // 底不下移
    assert(!music_ui_nav_key(&nav, MUS_KEY_DOWN, MUS_KEV_CLICK, &act));
    assert(music_ui_nav_key(&nav, MUS_KEY_OK, MUS_KEV_CLICK, &act));
    assert(act.change_screen && act.new_screen == MUS_SCR_LIST);
    assert(nav.screen == MUS_SCR_LIST);

    // ---- 列表:翻页跟随 + OK 播放 + OK 长按返回 ----
    music_ui_nav_init(&nav, 8);
    music_ui_nav_set_list(&nav, 20);
    music_ui_nav_goto(&nav, MUS_SCR_LIST);
    for (int i = 0; i < 9; i++) music_ui_nav_key(&nav, MUS_KEY_DOWN, MUS_KEV_CLICK, &act);
    assert(nav.cursor == 9 && nav.page_top == 2);   // 第 2 页首行 = 下标 2
    assert(music_ui_nav_page(&nav) == 1);
    for (int i = 0; i < 9; i++) music_ui_nav_key(&nav, MUS_KEY_UP, MUS_KEV_CLICK, &act);
    assert(nav.cursor == 0 && nav.page_top == 0);   // 回滚到第 1 页
    for (int i = 0; i < 30; i++) music_ui_nav_key(&nav, MUS_KEY_DOWN, MUS_KEV_CLICK, &act);
    assert(nav.cursor == 19 && nav.page_top == 12); // 末页 12..19
    assert(music_ui_nav_key(&nav, MUS_KEY_OK, MUS_KEV_CLICK, &act));
    assert(act.play_cursor && act.new_screen == MUS_SCR_PLAY);
    assert(nav.screen == MUS_SCR_PLAY);

    // 空列表 OK 无动作。
    music_ui_nav_init(&nav, 8);
    music_ui_nav_set_list(&nav, 0);
    music_ui_nav_goto(&nav, MUS_SCR_LIST);
    assert(!music_ui_nav_key(&nav, MUS_KEY_OK, MUS_KEV_CLICK, &act));

    // ---- 播放页:OK 暂停/续播,UP/DOWN 切歌,长按音量/返回 ----
    music_ui_nav_init(&nav, 8);
    music_ui_nav_goto(&nav, MUS_SCR_PLAY);
    assert(music_ui_nav_key(&nav, MUS_KEY_OK, MUS_KEV_CLICK, &act));
    assert(act.toggle_pause);
    assert(music_ui_nav_key(&nav, MUS_KEY_UP, MUS_KEV_CLICK, &act));
    assert(act.prev_track);
    assert(music_ui_nav_key(&nav, MUS_KEY_DOWN, MUS_KEV_CLICK, &act));
    assert(act.next_track);
    assert(music_ui_nav_key(&nav, MUS_KEY_UP, MUS_KEV_LONG, &act));
    assert(act.vol_delta == +1);
    assert(music_ui_nav_key(&nav, MUS_KEY_DOWN, MUS_KEV_LONG, &act));
    assert(act.vol_delta == -1);
    assert(music_ui_nav_key(&nav, MUS_KEY_OK, MUS_KEV_LONG, &act));
    assert(act.change_screen && act.new_screen == MUS_SCR_LIST);
    assert(nav.screen == MUS_SCR_LIST);

    // 播放页双击:不产生任何意图。
    assert(!music_ui_nav_key(&nav, MUS_KEY_OK, MUS_KEV_DOUBLE, &act));

    // ---- 设置页:OK 选中项,OK 长按回首页 ----
    music_ui_nav_init(&nav, 8);
    music_ui_nav_set_list(&nav, 3);   // 音量/重新配网/关于
    music_ui_nav_goto(&nav, MUS_SCR_SETTINGS);
    music_ui_nav_key(&nav, MUS_KEY_DOWN, MUS_KEV_CLICK, &act);   // → 重新配网
    assert(music_ui_nav_key(&nav, MUS_KEY_OK, MUS_KEV_CLICK, &act));
    assert(act.settings_select && act.settings_index == 1);
    assert(music_ui_nav_key(&nav, MUS_KEY_OK, MUS_KEV_LONG, &act));
    assert(act.change_screen && act.new_screen == MUS_SCR_HOME);
    assert(nav.screen == MUS_SCR_HOME);

    // ---- 配网页:不路由按键 ----
    music_ui_nav_goto(&nav, MUS_SCR_PROVISION);
    assert(!music_ui_nav_key(&nav, MUS_KEY_OK, MUS_KEV_CLICK, &act));
    assert(!music_ui_nav_key(&nav, MUS_KEY_UP, MUS_KEV_LONG, &act));

    // ---- 行数参数钳制 ----
    music_ui_nav_init(&nav, 0);
    assert(nav.rows_per_page == 1);

    puts("music_ui_nav tests: PASS");
    return 0;
}
