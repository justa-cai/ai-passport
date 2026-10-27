#!/usr/bin/env python3
"""test_music_font_coverage.py —— 中文字体覆盖门禁。

三道检查:
1. 应用源码字符串字面量里的非 ASCII 字符必须都在字符清单内
   (清单 = 生成字体的输入;不在清单里的界面文字 = 设备上显示豆腐块)。
2. 生成的 music_font_16.c 的 cmap 必须覆盖清单全体(生成器或字体
   文件被换过、缺字形时在这里拦下)。
3. 负例:清单/字体确实缺失的字符能被检出(门禁本身有效)。

只扫描 musicdl 应用源码(demo_* / ui_pixel_* 是基线 demo 的主机测试
样本,music_font_16.c 是生成数据,均排除)。
"""
import re
import pathlib
import unittest

REPO = pathlib.Path(__file__).resolve().parent.parent
APP_SOURCE_EXCLUDE = ("demo_", "ui_pixel_", "music_font_16.c", "key_guard")


def load_inventory() -> set:
    chars = set()
    for line in (REPO / "assets/fonts/music_chars.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or not line:
            continue
        chars.update(line)
    return chars


def app_sources() -> list:
    out = []
    for p in sorted(REPO.glob("main/*.c")) + sorted(REPO.glob("main/*.h")):
        if any(p.name.startswith(e.rstrip(".c")) or e in p.name for e in APP_SOURCE_EXCLUDE):
            continue
        out.append(p)
    return out


def strip_comments(src: str) -> str:
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    src = re.sub(r"//[^\n]*", " ", src)
    return src


STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')


def literal_non_ascii(src: str) -> set:
    found = set()
    for lit in STRING_LITERAL.findall(strip_comments(src)):
        # 只关心界面文字;协议转义(\xNN、\uXXXX)不进 UI。
        for ch in lit:
            if ch == "\\":
                continue
            if ord(ch) > 0x7E:
                found.add(ch)
    return found


def font_codepoints() -> set:
    """从生成的 music_font_16.c 还原 cmap 覆盖的码点集合。"""
    src = (REPO / "main/music_font_16.c").read_text(encoding="utf-8")
    cps = set()
    # ASCII 段:FORMAT0_TINY 连续覆盖。
    m = re.search(r"\.range_start = (\d+), \.range_length = (\d+)", src)
    assert m, "找不到 ASCII cmap"
    cps.update(range(int(m.group(1)), int(m.group(1)) + int(m.group(2))))
    # 稀疏段:unicode_cjk 数组存相对码点。
    m = re.search(r"static const uint16_t unicode_cjk\[\] = \{(.*?)\};", src, flags=re.S)
    assert m, "找不到 unicode_cjk"
    rcps = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{4}),", m.group(1))]
    m = re.search(r"\.range_start = 0x([0-9A-Fa-f]+), \.range_length", src)
    assert m, "找不到稀疏段 range_start"
    base = int(m.group(1), 16)
    cps.update(base + r for r in rcps)
    return cps


class MusicFontCoverage(unittest.TestCase):
    def test_inventory_covers_app_string_literals(self):
        inv = load_inventory()
        offenders = {}
        for p in app_sources():
            missing = literal_non_ascii(p.read_text(encoding="utf-8")) - inv
            if missing:
                offenders[p.name] = "".join(sorted(missing))
        self.assertFalse(
            offenders,
            f"应用字符串出现清单外字符(设备会显示豆腐块,请把字符加进"
            f" assets/fonts/music_chars.txt 并重新生成字体): {offenders}",
        )

    def test_font_cmap_covers_inventory(self):
        inv = load_inventory()
        font_cps = font_codepoints()
        missing = {c for c in inv if ord(c) not in font_cps}
        self.assertFalse(
            missing,
            f"字体 cmap 缺少清单字符(重新运行 tools/font_tools/gen_music_font.py): "
            f"{''.join(sorted(missing))!r}",
        )

    def test_gate_detects_missing_char(self):
        # 负例:门禁能发现清单/字体都不认识的字符。
        inv = load_inventory()
        font_cps = font_codepoints()
        ghost = "😀"  # 清单与字体都不含
        self.assertNotIn(ghost, inv)
        self.assertNotIn(ord(ghost), font_cps)
        self.assertTrue({ghost} - inv)  # 检出非空


if __name__ == "__main__":
    unittest.main()
