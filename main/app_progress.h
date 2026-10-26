/* 学习进度 NVS 持久化：blob 编解码见 progress_codec（主机可测），
 * 本模块只做 NVS 读写与异步保存 worker（参考番茄钟分支的存储模式）。*/
#ifndef APP_PROGRESS_H
#define APP_PROGRESS_H

#include "progress_codec.h"

/* 启动时加载（NVS 初始化 + 读 + 校验；损坏/缺失 → 默认值）。*/
void pinyin_progress_open(void);

/* 当前 RAM 快照（全局唯一，直接修改后调用 pinyin_progress_commit）。*/
pinyin_progress_t *pinyin_progress(void);

/* 请求异步保存（拷贝快照入队，合并最新；深度 2）。*/
void pinyin_progress_commit(void);

#endif /* APP_PROGRESS_H */
