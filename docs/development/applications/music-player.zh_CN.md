<p align="right">
  <a href="music-player.md">English</a> · <strong>简体中文</strong>
</p>

# musicdl 音乐播放器应用

`feature/music-player` 分支上的在线音乐播放器派生应用。把开源工具
[musicdl](https://github.com/CharlesPikachu/musicdl) 的酷我平台协议移植到
设备端:榜单浏览、关键词搜索、纯 HTTP 的 MP3 流式播放,不依赖 PC 中转。
配网走 SoftAP + 网页(见
[softap-web-provisioning.md](../engineering/softap-web-provisioning.md));
上线后同一 HTTP 服务切换为局域网遥控页 —— 设备本身没有文字输入,搜索
关键词由手机提供。

## 界面与按键

| 屏幕 | 内容 | 按键 |
| --- | --- | --- |
| 启动页 | 应用名,约 1 秒 | 自动进入 |
| 配网页 | 热点名与 `192.168.4.1` 步骤指引 | 确定长按 = 回首页 |
| 榜单首页 | 五个酷我榜单(热歌/新歌/华语/飙升/短视频) | 上/下移动,确定进入 |
| 歌曲列表 | 每页 8 行:序号、曲名、歌手、页码指示 | 上/下移动翻页,确定播放 |
| 正在播放 | 曲名跑马灯、歌手、EQ 动画、进度条、状态行、音量条 | 确定暂停,上/下上一首/下一首(长按=音量),确定长按=返回 |
| 设置 | 音量 / 重新配网 / 关于 | 列表语义 |

列表导航 clamp 不循环。网络状态反映为状态栏图标;抓取失败或结果为空时
列表页显示对应提示行,不留白屏。

## 架构

决策全部放在 `main/` 的纯逻辑模块里,经 `./tools/validate.sh --static`
做主机测试(不需要 ESP-IDF):`music_json.c`(搜索/榜单/解析响应的 SAX
解析)、`music_kuwo.c`(端点 URL 与 scheme 降级)、`music_gbk.c`(搜索
端点的 UTF-8 ↔ GBK)、`music_queue.c`(16 首静态队列)、
`music_player_sm.c`(播放状态机)、`music_net_sm.c`(网络生命周期)、
`music_ui_nav.c`(屏幕-按键路由与分页)。

ESP 绑定模块:`music_wifi.c`(Wi-Fi 生命周期 + 抓取执行)、
`music_http.c`(流式 GET,2KB 分块,可重入)、`music_decoder.c`
(esp_audio_codec MP3 胶水,立体声混缩)、`music_player.c`(单一播放
任务:解析 → 建流 → 解码 → `bsp_audio_write`)、`music_httpd.c`(配网页
与遥控页)、`ui_music.c` / `music_fonts.c`(界面与 16px 中文字体,见
[lvgl-chinese-fonts.md](../engineering/lvgl-chinese-fonts.md))。

失败语义在状态机而不在任务里:解析失败回到空闲并由外层补试一次;
建流/解码失败重解析当前曲,连败两次跳曲;暂停继续拉流但丢弃解码输出,
恢复零延迟(暂停期间照常耗带宽,v1 有意为之)。

## 音频管线与内存

整条管线由一个同步任务驱动;I2S 阻塞写就是节流时钟,DMA 深度吸收网络
抖动。v1 没有做计划中的 20KB 压缩环形缓冲 —— 真机测试出现欠载再加。
音频懒初始化,首次播放才占用解码器与 I2S 内存,配网/浏览阶段零开销。
应用跑在无 PSRAM 的芯片上:Wi-Fi 约 50KB + MP3 解码器约 30KB + LVGL 池
48KB + 任务栈;池不够用时依次砍 EQ/跑马灯动画、环形缓冲、榜单页大小。

`sdkconfig.defaults` 只保留 esp_audio_codec 的 MP3 解码器(其余编码族全
关,应用缩小约 550KB)并关闭蓝牙。注意 ESP-IDF 只在项目根目录没有
`sdkconfig` 时才从 defaults 重新生成;残留的旧根文件会静默覆盖 defaults,
改了解码器选项后要删掉它再构建。

## 验证

- `./tools/validate.sh --static` —— 仓库检查加上述主机测试(含覆盖全部
  字符串字面量的字体覆盖门禁)。
- `./tools/validate.sh` —— 追加隔离固件构建与合并镜像检查。
- 模拟器冒烟:启动、热点配网页、配网 POST、上线榜单抓取、遥控页均可在
  `esp-emu` 里用预置或表单提交的凭据跑通。I2S 未建模:音频播放与
  `bsp_audio_init()` 属真机验证项。

真机未验证项:播放音质与欠载行为、真手机的 captive portal 表现、家庭
网络下酷我接口可达性、esp_audio_codec 在本芯片 160MHz 的 CPU/内存占用、
边流播边 httpd、GBK 边缘歌名显示、耗电。
