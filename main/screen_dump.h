/* 调试用屏幕转储：USB Serial/JTAG 控制台命令。
 *
 * 命令（串口发送，行结束）：
 *   shot        — 截取当前 LVGL 屏幕（RGB565 快照 → RLE → base64 → 分行输出）
 *   heap        — 空闲/最小堆内存
 *   goto <n>    — 切屏（0首页 1选课 2卡片 3测验），配合 shot 免按键取图
 *   glyph <cp>  — dump 指定码点的 A8 字形（调试字体）
 *   fontdesc    — dump 内置/自生成字体描述符对照
 * 输出格式：
 *   SHOT:BEGIN w=240 h=320
 *   SHOT:<base64 行，每行 512 字符>
 *   SHOT:END
 * 宿主机用 tools/screen_dump.py 解码为 PNG。
 *
 * 快照缓冲为静态区（堆碎片后 malloc(150KB) 会失败），RLE 后纯色屏仅数 KB。
 */
#ifndef SCREEN_DUMP_H
#define SCREEN_DUMP_H

/* 启动控制台监听任务（USB Serial/JTAG stdin，阻塞读，栈 4096）。*/
void screen_dump_start(void);

#endif /* SCREEN_DUMP_H */
