#!/usr/bin/env python3
"""test_font_coverage.py — 中文字形覆盖门禁（纯文本解析，无需字体工具链）。

检查链（对应 docs/development/engineering/lvgl-chinese-fonts.md 验收）：
  1. main/*.[ch] 字符串字面量（NFC）的每个可打印码点 ∈ assets/fonts/pinyin_charset.txt
  2. 清单中每个码点在全部四号字体的产物 .c 中都有字形
  3. 56px 拼音字体必须包含 36 个带调元音与 ü（Montserrat 没有，只能靠子集）
  4. 负例：清单外码点（如 U+9F98 龘）不得混入（防无条件通过）
"""
import re
import sys
import unicodedata
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import gen_app_fonts as G  # noqa: E402  (复用字面量提取逻辑)

TONE_VOWELS = set(G.TONE_VOWELS)
FONT_FILES = [G.FONTS_DIR / spec[0] for spec in G.FONT_SPECS]


def parse_charset_file() -> set[int]:
    text = (G.CHARSET_FILE).read_text(encoding="utf-8")
    out = set()
    for line in text.splitlines():
        m = re.match(r"U\+([0-9A-F]{4}) ", line)
        assert m, f"清单行格式异常: {line!r}"
        out.add(int(m.group(1), 16))
    return out


def font_codepoints(path: Path) -> set[int]:
    text = path.read_text(encoding="utf-8")
    return {int(m[0], 16) for m in G.GLYPH_RE.findall(text)}


def main() -> int:
    # 1. 源码字面量 ⊆ 清单
    sources = sorted(list(G.MAIN.glob("*.c")) + list(G.MAIN.glob("*.h")))
    literal_text = unicodedata.normalize("NFC", G.extract_literals(sources))
    used = {ord(c) for c in literal_text if c.isprintable() and c not in "\n\t\r\0"}
    inventory = parse_charset_file()
    missing = sorted(used - inventory)
    assert not missing, (
        f"字面量字形缺失于清单（需重跑 tools/gen_app_fonts.py）: {[hex(c) for c in missing]}"
    )

    # 2. 清单 ⊆ 各号字体（56px 拼音字体按生成器规则仅含 latin 子集）
    charset = G.collect_charset()
    subsets = G.build_charset_subsets(charset)
    for spec, path in zip(G.FONT_SPECS, FONT_FILES):
        assert path.exists(), f"缺少字体产物 {path}"
        cps = font_codepoints(path)
        need = set(subsets[spec[4]])
        got = sorted(need - cps)
        assert not got, f"{path.name} 缺字形 {[hex(c) for c in got[:10]]}"

    # 3. 56px 必含带调元音与 ü
    big = font_codepoints(G.FONTS_DIR / "pinyin_font_56.c")
    need = {ord(c) for c in TONE_VOWELS}
    lost = sorted(need - big)
    assert not lost, f"56px 拼音字体缺带调元音 {[hex(c) for c in lost]}"

    # 4. 负例：U+9F98（龘）不在任何字体中
    for path in FONT_FILES:
        assert 0x9F98 not in font_codepoints(path), f"{path.name} 不应包含 U+9F98"

    # 5. ASCII 基本段全覆盖（fallback 之外的自足性）
    for path in FONT_FILES:
        cps = font_codepoints(path)
        for c in range(0x30, 0x3A):  # 数字
            assert c in cps, f"{path.name} 缺数字 {chr(c)}"
        for c in range(0x41, 0x5B):  # A-Z
            assert c in cps, f"{path.name} 缺字母 {chr(c)}"

    print(f"test_font_coverage: PASS ({len(inventory)} codepoints, {len(FONT_FILES)} fonts)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
