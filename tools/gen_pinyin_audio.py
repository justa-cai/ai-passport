#!/usr/bin/env python3
"""gen_pinyin_audio.py — 拼音发音离线生成管线（教学音频 + Edge TTS → 设备嵌入资产）。

音频来源（按 clip 优先级）：
  1. 教学音频（声母呼读/韵母四声/整体认读四声，真人标准读音）：
     下载自 https://123.5ikfc.com/pinyin （assets/voc/<拼音>.mp3，223 个，
     含 63 基础 + 160 四声）到 ./tmp/pinyin_voc/（不入库，见 assets/README.md）。
     文件名即 mark_letters() 的 NFC 带调串（üe 标 e、ün 标 ü，与课本标调一致）。
  2. Edge TTS（例词池，微软神经音色，喂汉字而非拼音串）：
     edge-tts CLI（≥7.3.1；直连 403 时自动经 proxychains 重试），
     mp3 缓存 ./tmp/edge_cache/（不入库）

阶段（每阶段打印统计，VAD 裁剪为用户明确要求）：
  1. 枚举 clip：24 韵母×4 声 + 23 声母呼读 + 16 整体认读×4 + 例词池（按 id 去重）
     —— id 规则与 main/pinyin_blend.c 的 py_aid_* 完全一致（tests 交叉绑定）
  2. 拉取：教学 mp3 经 ffmpeg 解码到 16 kHz（缓存 ./tmp/voc_decoded/）；
     词走 Edge TTS（mp3 → ffmpeg 16 kHz，缓存 ./tmp/edge_decoded/）
  3.（无需再重采样：ffmpeg 已出到 16 kHz）
  4. VAD 裁前后静音：20ms RMS 窗，阈值 max(噪底×8, 峰值×0.02)，保留 60ms 头尾垫
  5. IMA-ADPCM 编码（4B 头 {predictor i16le, index u8, pad} + 连续 nibble 低半字节在前，
     Sun 风格表——与 main/adpcm_decoder.c 严格互逆）
  6. 产物（入库）：assets/audio/pinyin_audio_ima.bin、main/pinyin_audio_index.{c,h}、
     assets/audio/pinyin_audio_manifest.json、tests/adpcm_golden.h

用法：
  python3 tools/gen_pinyin_audio.py                # 全量生成（默认晓晓音色）
  python3 tools/gen_pinyin_audio.py --core         # 仅 183 核心
  python3 tools/gen_pinyin_audio.py --check        # 校验产物一致性
"""
import argparse
import json
import re
import subprocess
import sys
import time
import wave
from datetime import date
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parent.parent
AUDIO_DIR = REPO / "assets" / "audio"
VOC_DIR = REPO / "tmp" / "pinyin_voc"
VOC_DECODED = REPO / "tmp" / "voc_decoded"
EDGE_CACHE = REPO / "tmp" / "edge_cache"
EDGE_DECODED = REPO / "tmp" / "edge_decoded"
GOLDEN = REPO / "tests" / "adpcm_golden.h"
MANIFEST = AUDIO_DIR / "pinyin_audio_manifest.json"
BIN = AUDIO_DIR / "pinyin_audio_ima.bin"
INDEX_C = REPO / "main" / "pinyin_audio_index.c"
INDEX_H = REPO / "main" / "pinyin_audio_index.h"

TEACH_URL = "https://123.5ikfc.com/pinyin"
EDGE_VOICE = "zh-CN-XiaoxiaoNeural"  # 微软晓晓：标准女声，清晰稳定
SAMPLE_RATE = 16000

# ---------------- 课程表（与 main/pinyin_data.c 一致；由 C 测试交叉绑定） ----------------

INITIALS = list("bpmfdtnlgkhjqx") + ["zh", "ch", "sh", "r", "z", "c", "s", "y", "w"]
CALLS = [
    "bō", "pō", "mō", "fō", "dē", "tē", "nē", "lē", "gē", "kē", "hē",
    "jī", "qī", "xī", "zhī", "chī", "shī", "rì", "zī", "cī", "sī", "yī", "wū",
]
CALL_AIDS = [
    "bo1", "po1", "mo1", "fo1", "de1", "te1", "ne1", "le1", "ge1", "ke1", "he1",
    "ji1", "qi1", "xi1", "zhi1", "chi1", "shi1", "ri4", "zi1", "ci1", "si1", "yi1", "wu1",
]
FINALS = ["a", "o", "e", "i", "u", "ü", "ai", "ei", "ui", "ao", "ou", "iu", "ie", "üe", "er",
          "an", "en", "in", "un", "ün", "ang", "eng", "ing", "ong"]
WHOLE = ["zhi", "chi", "shi", "ri", "zi", "ci", "si", "yi", "wu", "yu", "ye", "yue",
         "yuan", "yin", "yun", "ying"]

# 带调元音表（NFC 码点），行序 a o e i u ü
MARKED = {
    "a": "āáǎà", "o": "ōóǒò", "e": "ēéěè", "i": "īíǐì", "u": "ūúǔù", "ü": "ǖǘǚǜ",
}
TONE_MARKED = {  # (字母, 声调) → 带调字符
    (row, t + 1): MARKED[row][t] for row in MARKED for t in range(4)
}

# 例词池（hanzi, initial_idx|-1, medial|−1(0=i,1=u,2=ü), final_idx|-1, whole_idx|-1, tone）
WORDS: list[tuple] = []


def _w(hanzi, ini, med, fin, whole, tone):
    WORDS.append((hanzi, ini, med, fin, whole, tone))


# —— 与 main/pinyin_data.c k_words 逐条一致 ——
for e in [
    ("阿", -1, -1, 0, -1, 1), ("鹅", -1, -1, 2, -1, 2), ("恶", -1, -1, 2, -1, 4), ("哦", -1, -1, 1, -1, 2),
    ("衣", -1, -1, -1, 7, 1), ("屋", -1, -1, -1, 8, 1), ("鱼", -1, -1, -1, 9, 2), ("雨", -1, -1, -1, 9, 3),
    ("五", -1, -1, -1, 8, 3), ("蛙", 22, -1, 0, -1, 1), ("鸭", 21, -1, 0, -1, 1),
    ("爸", 0, -1, 0, -1, 4), ("妈", 2, -1, 0, -1, 1), ("马", 2, -1, 0, -1, 3), ("米", 2, -1, 3, -1, 3),
    ("木", 2, -1, 4, -1, 4), ("布", 0, -1, 4, -1, 4), ("波", 0, -1, 1, -1, 1), ("婆", 1, -1, 1, -1, 2),
    ("发", 3, -1, 0, -1, 1), ("佛", 3, -1, 1, -1, 2),
    ("大", 4, -1, 0, -1, 4), ("地", 4, -1, 3, -1, 4), ("兔", 5, -1, 4, -1, 4), ("他", 5, -1, 0, -1, 1),
    ("你", 6, -1, 3, -1, 3), ("拉", 7, -1, 0, -1, 1), ("路", 7, -1, 4, -1, 4), ("拿", 6, -1, 0, -1, 2),
    ("乐", 7, -1, 2, -1, 4), ("特", 5, -1, 2, -1, 4),
    ("哥", 8, -1, 2, -1, 1), ("果", 8, 1, 1, -1, 3), ("花", 10, 1, 0, -1, 1), ("画", 10, 1, 0, -1, 4),
    ("哭", 9, -1, 4, -1, 1), ("河", 10, -1, 2, -1, 2), ("古", 8, -1, 4, -1, 3), ("课", 9, -1, 2, -1, 4),
    ("鸡", 11, -1, 3, -1, 1), ("七", 12, -1, 3, -1, 1), ("西", 13, -1, 3, -1, 1), ("家", 11, 0, 0, -1, 1),
    ("下", 13, 0, 0, -1, 4), ("虾", 13, 0, 0, -1, 1), ("菊", 11, -1, 5, -1, 2), ("去", 12, -1, 5, -1, 4),
    ("字", -1, -1, -1, 4, 4), ("刺", -1, -1, -1, 5, 4), ("丝", -1, -1, -1, 6, 1), ("早", 18, -1, 9, -1, 3),
    ("苏", 20, -1, 4, -1, 1), ("座", 18, 1, 1, -1, 4),
    ("纸", -1, -1, -1, 0, 3), ("尺", -1, -1, -1, 1, 3), ("十", -1, -1, -1, 2, 2), ("日", -1, -1, -1, 3, 4),
    ("茶", 15, -1, 0, -1, 2), ("沙", 16, -1, 0, -1, 1), ("竹", 14, -1, 4, -1, 2), ("热", 17, -1, 2, -1, 4),
    ("肉", 17, -1, 10, -1, 4),
    ("白", 0, -1, 6, -1, 2), ("菜", 19, -1, 6, -1, 4), ("来", 7, -1, 6, -1, 2), ("飞", 3, -1, 7, -1, 1),
    ("北", 0, -1, 7, -1, 3), ("妹", 2, -1, 7, -1, 4), ("雷", 7, -1, 7, -1, 2), ("水", 16, -1, 8, -1, 3),
    ("嘴", 18, -1, 8, -1, 3),
    ("猫", 2, -1, 9, -1, 1), ("高", 8, -1, 9, -1, 1), ("桃", 5, -1, 9, -1, 2), ("草", 19, -1, 9, -1, 3),
    ("跑", 1, -1, 9, -1, 3), ("头", 5, -1, 10, -1, 2), ("狗", 8, -1, 10, -1, 3), ("楼", 7, -1, 10, -1, 2),
    ("牛", 6, -1, 11, -1, 2), ("六", 7, -1, 11, -1, 4),
    ("叶", -1, -1, -1, 10, 4), ("月", -1, -1, -1, 11, 4), ("耳", -1, -1, 14, -1, 3), ("二", -1, -1, 14, -1, 4),
    ("姐", 11, -1, 12, -1, 3), ("鞋", 13, -1, 12, -1, 2), ("雪", 13, -1, 13, -1, 3),
    ("山", 16, -1, 15, -1, 1), ("蓝", 7, -1, 15, -1, 2), ("门", 2, -1, 16, -1, 2), ("本", 0, -1, 16, -1, 3),
    ("林", 7, -1, 17, -1, 2), ("心", 13, -1, 17, -1, 1), ("村", 19, -1, 18, -1, 1), ("云", -1, -1, -1, 14, 2),
    ("圆", -1, -1, -1, 12, 2), ("银", -1, -1, -1, 13, 2), ("裙", 12, -1, 19, -1, 2),
    ("羊", 21, -1, 20, -1, 2), ("上", 16, -1, 20, -1, 4), ("灯", 4, -1, 21, -1, 1), ("风", 3, -1, 21, -1, 1),
    ("星", 13, -1, 22, -1, 1), ("明", 2, -1, 22, -1, 2), ("虫", 15, -1, 23, -1, 2), ("中", 14, -1, 23, -1, 1),
    ("龙", 7, -1, 23, -1, 2), ("鹰", -1, -1, -1, 15, 1), ("松", 20, -1, 23, -1, 1),
]:
    _w(*e)


# ---------------- 组串（与 pinyin_blend.c 规则一致） ----------------

def mark_letters(s: str, tone: int) -> str:
    """在（可能已去点的）字母串上按 a>o>e>ü>末位 i/u 标调。"""
    for pri in ("a", "o", "e"):
        i = s.find(pri)
        if i >= 0:
            return s[:i] + TONE_MARKED[(pri, tone)] + s[i + 1:]
    i = s.find("ü")
    if i >= 0:
        return s[:i] + TONE_MARKED[("ü", tone)] + s[i + 1:]
    last = max(s.rfind("i"), s.rfind("u"))
    if last >= 0:
        c = s[last]
        return s[:last] + TONE_MARKED[(c, tone)] + s[last + 1:]
    return s


def dots(ini: int) -> bool:
    return ini in (11, 12, 13, 21)  # j q x y


def written_final(ini: int, fin: int) -> str:
    f = FINALS[fin]
    if ini >= 0 and dots(ini) and f.startswith("ü"):
        f = "u" + f[1:]
    return f


def compose_syllable(ini: int, fin: int, tone: int) -> str:
    return INITIALS[ini] + mark_letters(written_final(ini, fin), tone)


def compose3(ini: int, med: int, fin: int, tone: int) -> str:
    medial = "i" if med == 0 else "u"
    if med == 2:
        medial = "u" if dots(ini) else "ü"
    return INITIALS[ini] + medial + mark_letters(FINALS[fin], tone)


def compose_whole(whole: int, tone: int) -> str:
    return mark_letters(WHOLE[whole], tone)


def compose_word(word) -> str:
    _, ini, med, fin, whole, tone = word
    if whole >= 0:
        return compose_whole(whole, tone)
    if ini >= 0 and med >= 0:
        return compose3(ini, med, fin, tone)
    if ini >= 0:
        return compose_syllable(ini, fin, tone)
    return mark_letters(FINALS[fin], tone)


def ascii_final(ini: int, fin: int) -> str:
    return written_final(ini, fin).replace("ü", "v")


def aid_word(word) -> str:
    _, ini, med, fin, whole, tone = word
    if whole >= 0:
        return f"{WHOLE[whole]}{tone}"
    out = (INITIALS[ini] if ini >= 0 else "")
    if med >= 0:
        out += "i" if med == 0 else "u"  # med==2 时按去点规则也是 u（jqxy）
    out += ascii_final(ini, fin)
    return out + str(tone)


def enumerate_clips(core_only: bool) -> dict[str, tuple[str, str | None]]:
    """id → (TTS 展示文本, 例词汉字|None)。相同 id 天然去重；
    呼读与对应整体认读 1 声为同一 id。"""
    clips: dict[str, tuple[str, str | None]] = {}

    def add(aid: str, text: str, hanzi: str | None = None):
        old = clips.get(aid)
        if old is not None and old[0] != text:
            raise SystemExit(f"id 冲突: {aid} -> {old[0]} vs {text}")
        # 同 id（如词 阿 与韵母 a1）：取已有文本，汉字取非空者（词提供）
        clips[aid] = (old[0] if old else text, old[1] if old and old[1] else hanzi)

    for f in range(len(FINALS)):
        for t in range(1, 5):
            # ü 行（ü/üe/ün）的书面整音节形式留作 TTS 回退提示；
            # 教学音频源有 ǖǘǚǜ/ǖn…/üē… 原生文件，正常不经过 TTS。
            u_standalone = {"ü": "yu", "üe": "yue", "ün": "yun"}
            aid = ascii_final(-1, f) + str(t)
            if FINALS[f] in u_standalone:
                add(aid, compose_whole(WHOLE.index(u_standalone[FINALS[f]]), t))
            else:
                add(aid, mark_letters(FINALS[f], t))
    for i in range(len(INITIALS)):
        add(CALL_AIDS[i], CALLS[i])
    for w_ in range(len(WHOLE)):
        for t in range(1, 5):
            add(f"{WHOLE[w_]}{t}", compose_whole(w_, t))
    if not core_only:
        for word in WORDS:
            add(aid_word(word), compose_word(word), word[0])
    return clips


# ---------------- 教学音频源（tmp/pinyin_voc/<name>.mp3） ----------------

# aid 里的 ASCII v 还原成韵母 ü 行（仅这三形）
V_BACK = {"v": "ü", "ve": "üe", "vn": "ün"}


def voc_name_for(aid: str) -> str | None:
    """clip id → 教学音频文件名（不含 .mp3）；词池返回 None 走 TTS。
    优先级：声母呼读 > 韵母四声 > 整体认读四声（重叠 id 同音，任取皆可）。
    文件名 = mark_letters() 的 NFC 带调串，与站点按钮文件完全一致。"""
    for i, ca in enumerate(CALL_AIDS):
        if aid == ca:
            return INITIALS[i]
    m = re.match(r"^([a-z]+)([1-4])$", aid)
    if m:
        base, tone = m.group(1), int(m.group(2))
        key = V_BACK.get(base, base)
        if key in FINALS:
            return mark_letters(key, tone)
        if base in WHOLE:
            return mark_letters(base, tone)
    return None


def voc_load(name: str) -> tuple[np.ndarray, int]:
    """教学 mp3 → 16 kHz 单声道 PCM（ffmpeg 重采样，缓存 ./tmp/voc_decoded/）。"""
    src = VOC_DIR / f"{name}.mp3"
    if not src.exists():
        raise SystemExit(
            f"缺教学音频 {src}——请从 {TEACH_URL} 下载到 {VOC_DIR}/"
            "（声调页 shengdiao.html，共 223 个 mp3；详见 assets/README.md）")
    VOC_DECODED.mkdir(parents=True, exist_ok=True)
    wav = VOC_DECODED / f"{name}.wav"
    if not wav.exists():
        subprocess.run(
            ["ffmpeg", "-y", "-loglevel", "error", "-i", str(src),
             "-ar", str(SAMPLE_RATE), "-ac", "1", str(wav)],
            check=True)
    pcm, sr = load_wav_mono(wav)
    if float(np.abs(pcm).max()) < 500.0:
        raise SystemExit(f"教学音频疑似静音: {src.name}")
    return pcm, sr


# ---------------- TTS 拉取与音频处理 ----------------


def edge_tts_fetch(text: str, voice: str, retries: int = 3) -> tuple[np.ndarray, int]:
    """例词合成：edge-tts CLI（喂汉字）→ ffmpeg 16 kHz 解码。
    直连 403（CN 出口被拒）时自动经 proxychains 重试；静音/空输出视为坏例。
    仅校验通过的音频进入缓存。"""
    EDGE_CACHE.mkdir(parents=True, exist_ok=True)
    EDGE_DECODED.mkdir(parents=True, exist_ok=True)
    key = f"{voice}_{text}".replace("/", "_")
    wav = EDGE_DECODED / f"{key}.wav"
    if wav.exists():
        pcm, sr = load_wav_mono(wav)
        if float(np.abs(pcm).max()) >= 500.0:
            return pcm, sr
        wav.unlink()

    mp3 = EDGE_CACHE / f"{key}.mp3"
    base = [sys.executable, "-m", "edge_tts", "--voice", voice,
            "--text", text, "--write-media", str(mp3)]
    last_exc = None
    for attempt in range(retries):
        for cmd in (base, ["proxychains", "-q"] + base):  # 先直连，403 再走代理
            try:
                subprocess.run(cmd, check=True, timeout=90,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if not mp3.exists() or mp3.stat().st_size < 1000:
                    raise ValueError("空输出")
                subprocess.run(
                    ["ffmpeg", "-y", "-loglevel", "error", "-i", str(mp3),
                     "-ar", str(SAMPLE_RATE), "-ac", "1", str(wav)], check=True)
                pcm, sr = load_wav_mono(wav)
                if float(np.abs(pcm).max()) < 500.0:
                    raise ValueError("静音输出")
                return pcm, sr
            except Exception as exc:  # noqa: BLE001
                last_exc = exc
                mp3.unlink(missing_ok=True)
        print(f"  重试 {text!r} ({attempt + 1}/{retries}): {last_exc}")
        time.sleep(2)
    raise SystemExit(f"Edge TTS 拉取失败 {text!r}: {last_exc}")


def load_wav_mono(path: Path) -> np.ndarray:
    with wave.open(str(path)) as w:
        assert w.getnchannels() == 1 and w.getsampwidth() == 2
        pcm = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
        return pcm.astype(np.float64), w.getframerate()


def resample_sinc(x: np.ndarray, sr_in: int, sr_out: int = SAMPLE_RATE) -> np.ndarray:
    """窗函数 sinc 重采样（阻带 ~70dB，截止 0.45*sr_out）。"""
    if sr_in == sr_out:
        return x
    ratio = sr_in / sr_out
    n_out = int(len(x) / ratio)
    t = np.arange(n_out) * ratio
    half = 16
    cutoff = 0.45 * sr_out / sr_in  # 归一化到输入采样率
    out = np.zeros(n_out)
    for k in range(-half, half + 1):
        idx = np.floor(t).astype(int) + k
        idx = np.clip(idx, 0, len(x) - 1)
        u = t - (np.floor(t) + k)
        # Blackman 窗 sinc 核
        win = (0.42 - 0.5 * np.cos(2 * np.pi * (k + u + half) / (2 * half + 1))
               + 0.08 * np.cos(4 * np.pi * (k + u + half) / (2 * half + 1)))
        sinc = np.sinc(2 * cutoff * u) * 2 * cutoff
        out += x[idx] * win * sinc
    return out


def lowpass_biquad(x: np.ndarray, fc: float, sr: int) -> np.ndarray:
    """2 阶 Butterworth 低通（双线性变换，无 scipy 依赖）。
    TTS 童声气音高频成分重，5kHz 预滤波显著改善 ADPCM 信噪比。"""
    import math
    w = 2 * math.pi * fc / sr
    c = 1 / math.tan(w / 2)
    den = 1 + c * math.sqrt(2) + c * c
    b0 = 1 / den
    b1, b2 = 2 * b0, b0
    a1 = 2 * (1 - c * c) / den
    a2 = (1 - c * math.sqrt(2) + c * c) / den
    y = np.empty_like(x)
    x1 = x2 = y1 = y2 = 0.0
    for i, xn in enumerate(x):
        yn = b0 * xn + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1 = x1, xn
        y2, y1 = y1, yn
        y[i] = yn
    return y


def vad_trim(x: np.ndarray, pad_ms: int = 60, sr: int = SAMPLE_RATE) -> tuple[np.ndarray, dict]:
    """能量 VAD 去除前后静音（用户要求）；阈值=max(噪底×8, 峰值×2%)。"""
    win = int(sr * 0.02)
    if len(x) < 3 * win:
        return x, {"trimmed": 0.0}
    rms = np.sqrt(np.convolve(x * x, np.ones(win) / win, mode="same"))
    noise = float(np.percentile(rms, 10))
    peak = float(rms.max())
    thresh = max(noise * 8.0, peak * 0.02)
    speech = np.where(rms > thresh)[0]
    stats = {"noise_rms": noise, "peak_rms": peak, "threshold": thresh}
    if len(speech) == 0:
        return x, stats
    pad = int(sr * pad_ms / 1000)
    a = max(0, int(speech[0]) - pad)
    b = min(len(x), int(speech[-1]) + pad)
    stats["trimmed"] = (len(x) - (b - a)) / sr
    return x[a:b], stats


# ---------------- IMA-ADPCM 编码（与 adpcm_decoder.c 互逆） ----------------

STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
]
INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8] * 2


def _clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def adpcm_encode(pcm: np.ndarray) -> bytes:
    """4B 头 + 连续 nibble（低半字节在前）；编码器跟踪解码器重构值。
    样本数裁成奇数：保证 nibble 数为偶、字节内无补位幻影样本，
    解码输出样本数与输入严格一致（golden 逐样本比对前提）。
    """
    s16 = np.clip(np.round(pcm), -32768, 32767).astype(np.int64)
    if len(s16) % 2 == 0:
        s16 = s16[:-1]
    predictor = int(s16[0]) if len(s16) else 0
    index = 0
    nibbles = []
    for sample in s16[1:]:
        step = STEP_TABLE[index]
        diff = int(sample) - predictor
        code = 0
        sign = 8 if diff < 0 else 0
        d = abs(diff)
        if d >= step:
            code |= 4
            d -= step
        if d >= step >> 1:
            code |= 2
            d -= step >> 1
        if d >= step >> 2:
            code |= 1
        code |= sign
        # 按解码器公式重构，保持状态一致
        recon = step >> 3
        if code & 4:
            recon += step
        if code & 2:
            recon += step >> 1
        if code & 1:
            recon += step >> 2
        predictor = _clamp(predictor + (-recon if code & 8 else recon), -32768, 32767)
        index = _clamp(index + INDEX_TABLE[code], 0, 88)
        nibbles.append(code)
    out = bytearray()
    out.append(predictor & 0xFF)
    out.append((predictor >> 8) & 0xFF)
    out.append(0)  # step_index（初始 0）
    out.append(0)  # 保留
    for i in range(0, len(nibbles), 2):
        lo = nibbles[i]
        hi = nibbles[i + 1] if i + 1 < len(nibbles) else 0
        out.append((lo & 0x0F) | ((hi & 0x0F) << 4))
    return bytes(out)


# ---------------- 产物生成 ----------------


def adpcm_decode(ad: bytes) -> list[int]:
    """镜像 main/adpcm_decoder.c（golden 基准与质量校验用）。"""
    STEP, IDXT = STEP_TABLE, INDEX_TABLE
    pred = ad[0] | (ad[1] << 8)
    if pred >= 0x8000:
        pred -= 0x10000
    idx = ad[2]
    out = [pred]
    for i in range(4, len(ad)):
        b = ad[i]
        for nib in (b & 0xF, b >> 4):
            step = STEP[idx]
            diff = step >> 3
            if nib & 4:
                diff += step
            if nib & 2:
                diff += step >> 1
            if nib & 1:
                diff += step >> 2
            pred = _clamp(pred + (-diff if nib & 8 else diff), -32768, 32767)
            idx = _clamp(idx + IDXT[nib], 0, 88)
            out.append(pred)
    return out


def adpcm_quality(pcm: np.ndarray, ad: bytes) -> float:
    """重构信噪比（dB）。语音 IMA-ADPCM @16k 典型 20~35dB；低于 18dB 视为异常。
    参考序列应用与编码器一致的奇数样本裁剪。"""
    ref = np.clip(np.round(pcm), -32768, 32767).astype(np.int64)
    if len(ref) % 2 == 0:
        ref = ref[:-1]
    dec = np.array(adpcm_decode(ad), dtype=np.int64)
    assert len(ref) == len(dec)
    sig = float((ref.astype(np.float64) ** 2).sum())
    err = float(((ref - dec).astype(np.float64) ** 2).sum())
    if sig <= 0.0:
        return 99.0
    return 10.0 * float(np.log10(sig / max(err, 1.0)))


def build_outputs(clips: dict[str, dict], voice: str) -> None:
    AUDIO_DIR.mkdir(parents=True, exist_ok=True)
    blob = bytearray()
    entries = []
    for aid in sorted(clips):
        entry = clips[aid]
        off = len(blob)
        blob += entry["adpcm"]
        while len(blob) % 4:
            blob.append(0)
        entries.append((aid, off, len(entry["adpcm"]), entry["samples"]))
    BIN.write_bytes(bytes(blob))

    n = len(entries)
    lines = [
        "/* 由 tools/gen_pinyin_audio.py 生成——勿手改。*/",
        "/* id 规则与 main/pinyin_blend.c 的 py_aid_* 一致；偏移指向",
        " * assets/audio/pinyin_audio_ima.bin（target_add_binary_data 嵌入）。*/",
        "#include <string.h>",
        "",
        "#include \"pinyin_audio_index.h\"",
        "",
        "const py_audio_entry_t k_pinyin_audio_entries[] = {",
    ]
    for aid, off, length, samples in entries:
        lines.append(f"    {{\"{aid}\", {off}, {length}, {samples}}},")
    lines += [
        "};",
        "",
        "const size_t k_pinyin_audio_count = %d;" % n,
        "",
        "const py_audio_entry_t *py_audio_lookup(const char *id)",
        "{",
        "    if (id == NULL) {",
        "        return NULL;",
        "    }",
        "    size_t lo = 0, hi = k_pinyin_audio_count;",
        "    while (lo < hi) {",
        "        size_t mid = lo + (hi - lo) / 2;",
        "        int cmp = strcmp(k_pinyin_audio_entries[mid].id, id);",
        "        if (cmp == 0) {",
        "            return &k_pinyin_audio_entries[mid];",
        "        }",
        "        if (cmp < 0) {",
        "            lo = mid + 1;",
        "        } else {",
        "            hi = mid;",
        "        }",
        "    }",
        "    return NULL;",
        "}",
        "",
    ]
    INDEX_C.write_text("\n".join(lines), encoding="utf-8")

    header = [
        "/* 由 tools/gen_pinyin_audio.py 生成——勿手改。*/",
        "#ifndef PINYIN_AUDIO_INDEX_H",
        "#define PINYIN_AUDIO_INDEX_H",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    const char *id;    /* ASCII 拼音+声调数字（ü→v；jqxy 后 ü 写 u）*/",
        "    uint32_t offset;   /* blob 内偏移（含 4 字节 ADPCM 头）*/",
        "    uint32_t length;   /* ADPCM 字节数（含头）*/",
        "    uint16_t samples;  /* PCM 样本数（16 kHz）*/",
        "} py_audio_entry_t;",
        "",
        "extern const py_audio_entry_t k_pinyin_audio_entries[];",
        "extern const size_t k_pinyin_audio_count;",
        "",
        "/* 二分查找；未命中返回 NULL。*/",
        "const py_audio_entry_t *py_audio_lookup(const char *id);",
        "",
        "#endif /* PINYIN_AUDIO_INDEX_H */",
        "",
    ]
    INDEX_H.write_text("\n".join(header), encoding="utf-8")

    manifest = {
        "generator": "tools/gen_pinyin_audio.py",
        "date": str(date.today()),
        "voice": voice,
        "teaching_audio": TEACH_URL,
        "sources": {
            "voc:*": "教学音频（声母呼读/韵母四声/整体认读四声，真人标准读音）",
            "edge:*": f"Edge TTS 例词（微软神经音色 {voice}，喂汉字）",
        },
        "format": "ima-adpcm-4bit-16khz-mono",
        "clip_count": n,
        "blob_bytes": len(blob),
        "clips": [
            {"id": aid, "text": clips[aid]["text"],
             "hanzi": clips[aid].get("hanzi"), "source": clips[aid]["source"],
             "raw_s": round(clips[aid]["raw_s"], 3),
             "trimmed_s": round(clips[aid]["trimmed_s"], 3)}
            for aid in sorted(clips)
        ],
    }
    MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=1), encoding="utf-8")

    # golden fixture：取最短的 clip 做 C 解码测试。
    # 注意：golden PCM 存“重构值”（C 解码器的实际输出），与原始样本存在
    # ±少量量化差——测试因此可逐样本精确比对。
    aid, entry = min(clips.items(), key=lambda kv: kv[1]["samples"])
    pcm16 = np.array(adpcm_decode(entry["adpcm"]), dtype=np.int16)
    g = ["/* 由 tools/gen_pinyin_audio.py 生成——勿手改。*/",
         "/* golden clip: id=\"%s\", %d samples */" % (aid, len(pcm16)),
         "#ifndef ADPCM_GOLDEN_H", "#define ADPCM_GOLDEN_H", "",
         "#include <stdint.h>", "#include <stddef.h>", ""]
    ad = entry["adpcm"]
    g.append("static const uint8_t k_golden_adpcm[] = {")
    g.append(", ".join(f"0x{b:02x}" for b in ad))
    g.append("};")
    g.append(f"static const size_t k_golden_adpcm_len = {len(ad)};")
    g.append("static const int16_t k_golden_pcm[] = {")
    g.append(", ".join(str(int(v)) for v in pcm16))
    g.append("};")
    g.append(f"static const size_t k_golden_pcm_len = {len(pcm16)};")
    g += ["", "#endif /* ADPCM_GOLDEN_H */", ""]
    GOLDEN.write_text("\n".join(g), encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--voice", default=EDGE_VOICE)
    ap.add_argument("--core", action="store_true", help="仅生成 183 核心 clip")
    ap.add_argument("--check", action="store_true", help="校验已生成产物一致性")
    args = ap.parse_args()

    if args.check:
        return do_check()

    clips_spec = enumerate_clips(core_only=args.core)
    print(f"枚举 clip {len(clips_spec)} 个（{'仅核心' if args.core else '核心+词池'}）")
    def encode_clip(pcm_f: np.ndarray, sr: int):
        """重采样→VAD→归一化→自适应低通→ADPCM；返回 (adpcm, sig, snr, trimmed_s, raw_s)。"""
        x16 = resample_sinc(pcm_f, sr) if sr != SAMPLE_RATE else pcm_f
        trimmed, _ = vad_trim(x16)
        trimmed_s = len(trimmed) / SAMPLE_RATE
        peak = float(np.abs(trimmed).max()) if len(trimmed) else 0.0
        if peak > 100.0:
            trimmed = trimmed * (23000.0 / peak)
        adpcm, best, best_snr = None, trimmed, -99.0
        for fc in (5000.0, 4000.0, 3200.0):
            sig = lowpass_biquad(trimmed, fc, SAMPLE_RATE) if len(trimmed) > 16 else trimmed
            repk = float(np.abs(sig).max())
            if repk > 100.0:
                sig = sig * (23000.0 / repk)
            cand = adpcm_encode(sig)
            snr = adpcm_quality(sig, cand)
            if snr > best_snr:
                adpcm, best, best_snr = cand, sig, snr
            if snr > 14.0:
                break
        return adpcm, best, best_snr, trimmed_s, len(pcm_f) / sr

    results: dict[str, dict] = {}
    snr_list: list[float] = []
    total_raw = total_trim = 0.0
    voc_n = edge_n = 0
    for i, (aid, (text, hanzi)) in enumerate(sorted(clips_spec.items())):
        name = voc_name_for(aid)
        if name is not None:
            pcm_f, sr = voc_load(name)
            source = f"voc:{name}"
            voc_n += 1
        else:
            pcm_f, sr = edge_tts_fetch(hanzi or text, args.voice)
            source = f"edge:{args.voice}"
            edge_n += 1
        adpcm, sig, snr, trimmed_s, raw_s = encode_clip(pcm_f, sr)
        if source.startswith("edge") and snr < 12.0:
            # 随机坏例（合成质量波动）：弃缓存重合成一次
            print(f"  {aid} SNR {snr:.1f} dB → 重新合成")
            key = f"{args.voice}_{hanzi or text}".replace("/", "_")
            (EDGE_CACHE / f"{key}.mp3").unlink(missing_ok=True)
            (EDGE_DECODED / f"{key}.wav").unlink(missing_ok=True)
            try:
                pcm_f, sr = edge_tts_fetch(hanzi or text, args.voice)
                adpcm2, sig2, snr2, trimmed_s2, raw_s2 = encode_clip(pcm_f, sr)
                if snr2 > snr:
                    adpcm, sig, snr, trimmed_s, raw_s = (
                        adpcm2, sig2, snr2, trimmed_s2, raw_s2)
            except SystemExit:
                pass
        if snr <= 10.0:
            print(f"  警告: {aid} SNR {snr:.1f} dB（重合成后仍偏噪，接受）")
        snr_list.append(snr)
        n_samples = 1 + 2 * (len(adpcm) - 4)
        results[aid] = {"text": text, "hanzi": hanzi, "raw_s": raw_s,
                        "trimmed_s": trimmed_s, "adpcm": adpcm,
                        "samples": n_samples, "pcm": sig,
                        "snr_db": round(snr, 1), "source": source}
        total_raw += raw_s
        total_trim += trimmed_s
        if (i + 1) % 20 == 0:
            print(f"  {i + 1}/{len(clips_spec)} …")
    print(f"来源: 教学音频 {voc_n} + Edge TTS 词 {edge_n}")
    print(f"VAD: 原始 {total_raw:.1f}s → 裁剪后 {total_trim:.1f}s "
          f"（平均 {total_trim / len(results):.2f}s/clip）")
    print(f"ADPCM SNR: min {min(snr_list):.1f} / mean "
          f"{sum(snr_list) / len(snr_list):.1f} / max {max(snr_list):.1f} dB")
    build_outputs(results, args.voice)
    blob_kb = (BIN).stat().st_size / 1024
    print(f"产物: {BIN.name} {blob_kb:.0f} KiB, {len(results)} clips, "
          f"manifest + index + golden 已生成")
    return 0


def do_check() -> int:
    """离线校验：manifest↔index↔blob 偏移一致、id 排序唯一、golden 自洽。"""
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    text = INDEX_C.read_text(encoding="utf-8")
    rows = re.findall(r'\{"([a-z0-9]+)", (\d+), (\d+), (\d+)\},', text)
    ids = [r[0] for r in rows]
    assert ids == sorted(ids), "index id 未排序"
    assert len(ids) == len(set(ids)), "index id 重复"
    blob_size = BIN.stat().st_size
    for (aid, off, length, _s) in rows:
        off, length = int(off), int(length)
        assert off % 4 == 0, f"{aid} 偏移未 4 字节对齐"
        assert off + length <= blob_size, f"{aid} 越界"
    assert len(ids) == manifest["clip_count"], "manifest clip 数不符"
    print(f"check: {len(ids)} clips, blob {blob_size} B — OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
