#!/usr/bin/env python3
"""test_pinyin_audio_blob.py — 音频产物静态校验（离线，无需 TTS/字体环境）。

检查 manifest ↔ index.c ↔ .bin 三方一致：
  id 排序唯一、偏移 4 字节对齐、在 blob 内且不重叠、golden 文件存在，
  以及 manifest 记录的 clip 数与索引一致。
"""
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
AUDIO = REPO / "assets" / "audio"
INDEX_C = REPO / "main" / "pinyin_audio_index.c"
GOLDEN = REPO / "tests" / "adpcm_golden.h"


def main() -> int:
    manifest = json.loads((AUDIO / "pinyin_audio_manifest.json").read_text(encoding="utf-8"))
    blob_size = (AUDIO / "pinyin_audio_ima.bin").stat().st_size
    text = INDEX_C.read_text(encoding="utf-8")

    rows = re.findall(r'\{"([a-z0-9]+)", (\d+), (\d+), (\d+)\},', text)
    assert rows, "index.c 无条目"

    ids = [r[0] for r in rows]
    assert ids == sorted(ids), "id 未排序（破坏二分查找）"
    assert len(ids) == len(set(ids)), "id 重复"
    assert len(ids) == manifest["clip_count"], "manifest clip 数与索引不符"

    spans = []
    for aid, off_s, len_s, _samples in rows:
        off, length = int(off_s), int(len_s)
        assert off % 4 == 0, f"{aid} 偏移未 4 字节对齐"
        assert length >= 5, f"{aid} 长度异常"
        assert off + length <= blob_size, f"{aid} 越界 blob"
        spans.append((off, off + length, aid))
    spans.sort()
    for (a0, a1, id0), (b0, b1, id1) in zip(spans, spans[1:]):
        assert a1 <= b0, f"clip 重叠: {id0} 与 {id1}"

    # 核心 183 个 id 必须存在（24 韵母×4 + 23 呼读 + 16 整体×4 去重后）
    ids_set = set(ids)
    finals = ["a", "o", "e", "i", "u", "v", "ai", "ei", "ui", "ao", "ou", "iu", "ie", "ve", "er",
              "an", "en", "in", "un", "vn", "ang", "eng", "ing", "ong"]
    for f in finals:
        for t in range(1, 5):
            assert f"{f}{t}" in ids_set, f"缺韵母 clip {f}{t}"
    calls = ["bo1", "po1", "mo1", "fo1", "de1", "te1", "ne1", "le1", "ge1", "ke1", "he1",
             "ji1", "qi1", "xi1", "zhi1", "chi1", "shi1", "ri4", "zi1", "ci1", "si1", "yi1", "wu1"]
    for c in calls:
        assert c in ids_set, f"缺声母呼读 clip {c}"
    whole = ["zhi", "chi", "shi", "ri", "zi", "ci", "si", "yi", "wu", "yu", "ye", "yue",
             "yuan", "yin", "yun", "ying"]
    for w in whole:
        for t in range(1, 5):
            assert f"{w}{t}" in ids_set, f"缺整体认读 clip {w}{t}"

    assert GOLDEN.exists(), "缺 tests/adpcm_golden.h"
    golden = GOLDEN.read_text(encoding="utf-8")
    assert "k_golden_adpcm" in golden and "k_golden_pcm" in golden

    blob_kib = blob_size / 1024
    assert blob_kib < 3072, f"blob 超预算: {blob_kib:.0f} KiB"
    print(f"test_pinyin_audio_blob: PASS ({len(ids)} clips, {blob_kib:.0f} KiB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
