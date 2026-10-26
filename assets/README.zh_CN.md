<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐、电池参数等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

### 拼音应用字体子集（分支 `feature/pinyin-cards`）

| 文件 | 内容与来源 |
| --- | --- |
| [`fonts/pinyin_charset.txt`](fonts/pinyin_charset.txt) | 字符清单（403 码点）：`main/*.[ch]` 全部字符串字面量（NFC）∪ ASCII ∪ 36 个 NFC 带调元音与 `ü` ∪ 界面符号。既是生成器输出，也是覆盖门禁输入。 |
| `fonts/pinyin_font_16.c` / `pinyin_font_22.c` / `pinyin_font_44.c` | 全清单子集，16px（Regular）/ 22px（Medium）/ 44px（Medium），用于提示行、正文、测验汉字与星标。 |
| `fonts/pinyin_font_56.c` | 133 码点拉丁子集（ASCII + 带调元音 + 符号），拼音大字专用；刻意不接兜底字体以避免基线错位。 |

来源字体：**Noto Sans CJK SC**（即思源黑体，SIL OFL 1.1），取自构建机
`/usr/share/fonts/opentype/noto/NotoSansCJK-{Regular,Medium}.ttc`；`.ttc`
不入库。由仓库内生成器 `tools/gen_app_fonts.py` 产出（Pillow/FreeType
直出 LVGL 格式并逐像素回读校验；依据
`docs/reference/shinku-chen/lvgl-cjk-font-subsets.md` 的事故记录，新版
Node.js 下的 `lv_font_conv` 不予信任）。重新生成：`python3
tools/gen_app_fonts.py`；门禁以 `tests/test_font_coverage.py` 对照已提交
清单校验。cmap 仅使用 `FORMAT0_TINY` + `SPARSE_TINY`。三号字体位图合计
约 455 KB Flash；兜底链的运行时副本占用静态 RAM。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

### 拼音发音音频（分支 `feature/pinyin-cards`，位于 `audio/`）

| 文件 | 内容与来源 |
| --- | --- |
| [`audio/pinyin_audio_ima.bin`](audio/pinyin_audio_ima.bin) | 255 条 clip：24 韵母×4 声 + 23 声母呼读 + 16 整体认读×4 声（去重后 174 个核心 id）+ 81 条例词池；IMA-ADPCM 4bit @16kHz 单声道（每条 4 字节头 + 连续 nibble 低半字节在前），偏移 4 字节对齐。 |
| [`audio/pinyin_audio_manifest.json`](audio/pinyin_audio_manifest.json) | 每条 clip 的 id、来源（`voc:*` / `tts`）、TTS 文本、裁剪前后时长、音色与生成日期。 |

`tools/gen_pinyin_audio.py` 按 clip 选用两类音源：

1. **教学音频（核心音节，`voc:*`）**——23 声母呼读、96 韵母四声、64 整体
   认读四声采用真人标准读音，抓取自拼音学习站
   `https://123.5ikfc.com/pinyin`（63 基础 + 160 四声共 223 个 MP3，缓存在
   `./tmp/pinyin_voc/`，经 ffmpeg 解码到 16kHz）。ü 行（ǖǘǚǜ、ǖn…、üē…）
   该站有原生文件，此前的同音整体认读合成仅保留为 TTS 回退路径。
   **许可说明**：抓取的教学音频自用可以，再分发或商用需获得来源许可。
2. **Edge TTS（例词池，`edge:*`）**——读真实词语不是 TTS 短板，81 条例词
   仍为合成，但音源从 VoxCPM 换到微软 Edge TTS（神经音色
   `zh-CN-XiaoxiaoNeural`，**喂汉字**而非拼音串；`edge-tts` CLI ≥7.3.1，
   CN 出口直连 WSS 为 403，生成器自动经 `proxychains` 重试）。原始 MP3
   缓存 `./tmp/edge_cache/` 不入库。音色使用受微软神经声音条款约束，
   商用应改用授权的 Azure Speech 资源。

共用后处理：能量 VAD 静音裁剪（60ms 垫）→ 0.7 满刻度峰值归一化 →
5/4/3.2kHz 自适应低通 → IMA-ADPCM。固件解码器（`main/adpcm_decoder.c`）
与编码器由 `tests/adpcm_golden.h` 基准绑定；成品语料 SNR：最低 14.6 /
均值 24.0 dB。

## 电池参数（battery）

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
保存万锂新能源 500 mAh 电芯的 CW2017 参数（profile2、XIL40X、2026-10-20）。
该文件是项目提供的 `喜梨_智能工卡_万锂新能源500mAh_profile2_XIL40X_20261020.txt`
的原样副本，按维护者要求保留用于本次固件更新；原文件没有附带单独的许可说明。

格式为 80 行两位十六进制数字，每行一个字节，按寄存器顺序排列。
BSP 在 `components/bsp/src/bsp_battery.c` 中嵌入这 80 字节，主机检查会验证
源文件及数组内容完全一致。文本文件 SHA-256：
`7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`。
解码后 80 字节 SHA-256：
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`。
这是电量计参数，不是充电配置，也不能通用于所有 500 mAh 电芯；详见
[硬件指南](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md#9-cw2017-电池计)。
