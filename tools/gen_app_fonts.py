#!/usr/bin/env python3
"""gen_app_fonts.py — 拼音应用 LVGL 9 位图字体生成器（Pillow/FreeType 直出）。

背景：lv_font_conv 1.5.3 在新版 Node.js 下产出的位图经 LVGL 解码为噪点
（docs/reference/shinku-chen/lvgl-cjk-font-subsets.md 记录），故本仓库自研生成器，
逐像素校验后提交产物。

流程：
  1. 扫描 main/*.[ch] 字符串字面量（剥注释）→ NFC 归一 → 并入固定集合
     （ASCII、36 个带调元音、ü、★☆ 与实际使用的中文标点）
  2. 写 charset 清单 assets/fonts/pinyin_charset.txt（覆盖门禁的输入与事实来源）
  3. 用 Pillow 光栅化 Noto Sans CJK SC（ttc 由 fontTools 定位 SC face）
  4. 直接产出 LVGL 9 字体 C：glyph_bitmap(4bpp, 高半字节在前) + glyph_dsc +
     cmap = FORMAT0_TINY(ASCII 连续段) + SPARSE_TINY(其余排序码点)——
     绝不输出 FORMAT0_FULL（LVGL 9.6 实测崩溃）
  5. 回读产物 C，按 LVGL PLAIN 解码规则逐像素比对，任何不一致即失败

用法：
  python3 tools/gen_app_fonts.py            # 生成 + 校验
  python3 tools/gen_app_fonts.py --check    # 仅校验已提交产物（无需字体文件）

设计文档：docs/development/engineering/lvgl-chinese-fonts.md
"""
import argparse
import re
import sys
import unicodedata
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MAIN = REPO / "main"
FONTS_DIR = REPO / "assets" / "fonts"
CHARSET_FILE = FONTS_DIR / "pinyin_charset.txt"

# 字体族：Noto Sans CJK（= 思源黑体，SIL OFL 1.1）。ttc 不入库，仅记录来源。
TTC_MEDIUM = "/usr/share/fonts/opentype/noto/NotoSansCJK-Medium.ttc"
TTC_REGULAR = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"

# 36 个 NFC 带调元音 + ü（拼音显示必备；Montserrat 无这些字形）
TONE_VOWELS = (
    "āáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜ"
    "ü"
)

# 各号字体的角色与字符集（full = 清单全集；latin = ASCII+带调元音+常用符号）
FONT_SPECS = [
    # (输出文件名, C 符号名, 字号, ttc, 字符集, line_height)
    ("pinyin_font_16.c", "pinyin_font_16", 16, TTC_REGULAR, "full", 20),
    ("pinyin_font_22.c", "pinyin_font_22", 22, TTC_MEDIUM, "full", 26),
    ("pinyin_font_44.c", "pinyin_font_44", 44, TTC_MEDIUM, "full", 50),
    ("pinyin_font_56.c", "pinyin_font_56", 56, TTC_MEDIUM, "latin", 62),
]

# ---------- 字符串字面量提取（剥注释；处理常见转义） ----------

C_COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
C_STRING_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
HEX_ESCAPE_RE = re.compile(r"\\x([0-9a-fA-F]{1,2})")


def _decode_escapes(raw: str) -> str:
    out = []
    i = 0
    while i < len(raw):
        c = raw[i]
        if c == "\\" and i + 1 < len(raw):
            n = raw[i + 1]
            simple = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "0": "\0"}
            if n in simple:
                out.append(simple[n])
                i += 2
                continue
            if n == "x":
                m = HEX_ESCAPE_RE.match(raw, i)
                if m:
                    out.append(chr(int(m.group(1), 16)))
                    i = m.end()
                    continue
            out.append(n)
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def extract_literals(sources) -> str:
    blob = []
    for path in sources:
        text = path.read_text(encoding="utf-8")
        text = C_COMMENT_RE.sub(" ", text)
        for m in C_STRING_RE.finditer(text):
            blob.append(_decode_escapes(m.group(1)))
    return "".join(blob)


def collect_charset() -> list[int]:
    """清单 = 源码字面量（NFC）∪ ASCII ∪ 带调元音 ∪ 固定符号。"""
    sources = sorted(list(MAIN.glob("*.c")) + list(MAIN.glob("*.h")))
    literal_text = unicodedata.normalize("NFC", extract_literals(sources))
    cps: set[int] = set()
    for ch in literal_text:
        if ch.isprintable() and ch not in "\n\t\r\0":
            cps.add(ord(ch))
    cps |= set(range(0x20, 0x7F))
    cps |= {ord(c) for c in TONE_VOWELS}
    cps |= {ord(c) for c in "★☆·，。、？！：…—（）"}
    return sorted(cps)


# ---------- Pillow 光栅化 ----------


def load_face(ttc_path: str, px: int, charset: list[int] | None = None):
    from PIL import Image, ImageDraw, ImageFont
    from fontTools.ttLib import TTCollection

    ttc = TTCollection(ttc_path, lazy=True)
    face_index = None
    for i, font in enumerate(ttc.fonts):
        name = font["name"].getDebugName(4) or ""
        # Regular ttc 名为 'Noto Sans CJK SC'，Medium ttc 为 'Noto Sans CJK SC Medium'
        if name.startswith("Noto Sans CJK SC") and "Mono" not in name:
            face_index = i
            cmap = font.getBestCmap()
            break
    if face_index is None:
        raise SystemExit(f"未在 {ttc_path} 中找到 Noto Sans CJK SC face")
    if charset is not None:
        # 防线：清单码点必须真实存在于字体 cmap。缺失时 PIL 会渲染 notdef
        # 空壳（形似圆环的垃圾字形），逐像素校验对此免疫（自证清白）。
        missing = [hex(c) for c in charset if c not in cmap]
        if missing:
            raise SystemExit(f"{ttc_path}: 字体缺字形 {missing[:8]}——请从字符清单移除或换字体")
    font = ImageFont.truetype(ttc_path, px, index=face_index)
    return Image, ImageDraw, font


def rasterize_glyph(font, draw_cls, image_cls, ch: str, px: int):
    """返回 (bitmap_bytes(每像素 0..15), adv_w(8.4 定点), box_w, box_h, ofs_x, ofs_y)。
    adv_w 为 LVGL 8.4 定点（真实像素 ×16，见 lv_font_fmt_txt.h）。
    ofs_y 语义（lv_draw_label.c:626 实测）：y1 = 基线 − box_h − ofs_y，
    即 ofs_y = (基线 − 字形盒顶部) − box_h——正值上抬（如引号 +8）、
    负值下沉（如降部 −3），Montserrat 20 同约定。"""
    margin = px  # 四周留白，避免裁切
    canvas = image_cls.new("L", (px + 2 * margin, px * 2 + 2 * margin), 0)
    draw = draw_cls.Draw(canvas)
    ox, oy = margin, margin + px  # 基线位置
    draw.text((ox, oy), ch, font=font, fill=255, anchor="ls")
    adv = round(font.getlength(ch) * 16)
    bbox = canvas.getbbox()
    if bbox is None:
        return b"", adv, 0, 0, 0, 0
    x0, y0, x1, y1 = bbox
    box_w, box_h = x1 - x0, y1 - y0
    ofs_x = x0 - ox
    ofs_y = (oy - y0) - box_h  # 见 docstring；坐基线的 CJK 字形 ≈ 0 ~ -1
    pixels = canvas.crop((x0, y0, x1, y1)).tobytes()
    nib = bytes((p * 15 + 127) // 255 for p in pixels)
    return nib, adv, box_w, box_h, ofs_x, ofs_y


def pack_4bpp(nib: bytes, box_w: int, box_h: int) -> bytes:
    """LVGL PLAIN stride=0 语义：跨行连续 nibble 流（高半字节在前），
    整个字形起始按字节对齐（bitmap_index 为字节偏移），末尾不足一字节补 0。
    注意：不逐行补齐——LVGL 的 lv_font_get_bitmap_fmt_txt 按 x 奇偶交替
    读高低半字节，奇数宽行会在下一行继续消费当前字节（实测源码行为）。"""
    total = box_w * box_h
    out = bytearray((total + 1) // 2)
    for i in range(total):
        v = nib[i]
        if i % 2 == 0:
            out[i // 2] |= (v << 4) & 0xF0
        else:
            out[i // 2] |= v & 0x0F
    return bytes(out)


# ---------- LVGL 字体 C 生成 ----------

HEADER = """/* 由 tools/gen_app_fonts.py 生成（Noto Sans CJK SC, SIL OFL 1.1）——勿手改。
 * 生成命令与字符清单见 assets/fonts/pinyin_charset.txt 与 assets/README.md。
 * cmap: FORMAT0_TINY(ASCII) + SPARSE_TINY(其余)；4bpp PLAIN 未压缩。
 */

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef {guard}
#define {guard} 1

#if {guard}
"""


def c_literal(cp: int) -> str:
    ch = chr(cp)
    if 0x20 <= cp < 0x7F and ch not in '\\"':
        return f"'{ch}'"
    return f"\\x{cp:04x}"


def emit_font(spec, glyphs: dict[int, tuple], line_height: int) -> str:
    """glyphs: cp -> (nib, adv, w, h, ofs_x, ofs_y)（已按 cp 排序）。"""
    name = spec[1]
    guard = name.upper()
    cps = sorted(glyphs.keys())
    ascii_run = [c for c in cps if 0x20 <= c <= 0x7E]
    others = [c for c in cps if not (0x20 <= c <= 0x7E)]
    # glyph 顺序：ASCII 段在前（FORMAT0_TINY 连续），其余按码点排序
    order = ascii_run + others
    index_of = {c: i for i, c in enumerate(order)}

    bitmap = bytearray()
    dsc_lines = []
    for c in order:
        nib, adv, w, h, ofs_x, ofs_y = glyphs[c]
        packed = pack_4bpp(nib, w, h)
        off = len(bitmap)
        bitmap += packed
        # 对齐到 4 字节（与 LVGL 内置字体一致的保守对齐）
        while len(bitmap) % 4:
            bitmap.append(0)
        ch_repr = c_literal(c)
        dsc_lines.append(
            f"    /* U+{c:04X} {ch_repr} */ "
            f"{{.bitmap_index = {off}, .adv_w = {adv}, .box_w = {w}, .box_h = {h}, "
            f".ofs_x = {ofs_x}, .ofs_y = {ofs_y}}},"
        )

    lines = [HEADER.format(guard=guard)]
    lines.append(f"/* 共 {len(order)} 字形，清单来源 assets/fonts/pinyin_charset.txt */\n")
    lines.append(f"static LV_ATTRIBUTE_LARGE_CONST const uint8_t {name}_bitmap[] = {{\n")
    hex_rows = [", ".join(f"0x{b:02x}" for b in bitmap[i:i + 16]) for i in range(0, len(bitmap), 16)]
    lines.append(",\n".join(hex_rows))
    lines.append("\n};\n\n")
    lines.append(f"static const lv_font_fmt_txt_glyph_dsc_t {name}_glyph_dsc[] = {{\n")
    lines.append("\n".join(dsc_lines))
    lines.append("\n};\n\n")

    # cmap：ASCII 连续段 + 稀疏段
    cmap_lines = []
    cmap_lines.append(
        f"    {{.range_start = 0x20, .range_length = {len(ascii_run)}, "
        f".glyph_id_start = 0, .unicode_list = NULL, .glyph_id_ofs_list = NULL, "
        f".list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY}},"
    )
    if others:
        base = others[0]
        uni_list = ", ".join(str(c - base) for c in others)
        cmap_lines.append(
            f"    {{.range_start = 0x{base:X}, .range_length = {others[-1] - base + 1}, "
            f".glyph_id_start = {len(ascii_run)}, .unicode_list = {name}_unicode_list, "
            f".glyph_id_ofs_list = NULL, .list_length = {len(others)}, "
            f".type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}},"
        )
        lines.append(
            f"static const uint16_t {name}_unicode_list[] = {{\n    {uni_list},\n}};\n\n"
        )
    lines.append(
        f"static const lv_font_fmt_txt_cmap_t {name}_cmaps[] = {{\n"
        + "\n".join(cmap_lines)
        + "\n};\n\n"
    )
    lines.append(
        f"static const lv_font_fmt_txt_dsc_t {name}_dsc = {{\n"
        f"    .glyph_bitmap = {name}_bitmap,\n"
        f"    .glyph_dsc = {name}_glyph_dsc,\n"
        f"    .cmaps = {name}_cmaps,\n"
        f"    .cmap_num = {len(cmap_lines)},\n"
        f"    .bpp = 4,\n"
        f"    .bitmap_format = LV_FONT_FMT_TXT_PLAIN,\n"
        f"}};\n\n"
    )
    lines.append(
        f"const lv_font_t {name} = {{\n"
        f"    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n"
        f"    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n"
        f"    .line_height = {line_height},\n"
        f"    .base_line = {line_height - spec[2]},\n"
        f"    .dsc = &{name}_dsc,\n"
        f"    .fallback = NULL,\n"
        f"    .user_data = NULL,\n"
        f"}};\n\n"
    )
    lines.append(f"#endif /* {guard} */\n#endif /* ndef {guard} */\n")
    return "".join(lines)


# ---------- 产物回读校验（按 LVGL PLAIN 规则解码，逐像素比对） ----------

GLYPH_RE = re.compile(
    r"/\* U\+([0-9A-F]{4}) .*?\*/ \{\.bitmap_index = (\d+), \.adv_w = (\d+), "
    r"\.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\},"
)


def parse_font_c(path: Path):
    """返回 (bitmap bytes, [(cp, off, adv, w, h, ox, oy)])。"""
    text = path.read_text(encoding="utf-8")
    bm = re.search(
        r"_bitmap\[\] = \{\n(.*?)\n\};", text, re.S
    )
    if not bm:
        raise SystemExit(f"{path}: 未找到位图数组")
    bitmap = bytes(
        int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", bm.group(1))
    )
    glyphs = [
        (int(cp, 16), int(off), int(adv), int(w), int(h), int(ox), int(oy))
        for cp, off, adv, w, h, ox, oy in GLYPH_RE.findall(text)
    ]
    return bitmap, glyphs


def decode_plain_4bpp(bitmap: bytes, off: int, w: int, h: int):
    """与 LVGL lv_font_get_bitmap_fmt_txt（bpp=4, stride=0）一致的连续解码。"""
    out = bytearray(w * h)
    byte = off
    for i in range(w * h):
        b = bitmap[byte]
        out[i] = (b >> 4) if i % 2 == 0 else (b & 0x0F)
        if i % 2 == 1:
            byte += 1
    return out


def verify_font(path: Path, glyphs_ref: dict[int, tuple], px: int):
    bitmap, glyphs = parse_font_c(path)
    got = {cp: (off, adv, w, h, ox, oy) for cp, off, adv, w, h, ox, oy in glyphs}
    if len(got) != len(glyphs):
        raise SystemExit(f"{path.name}: 字形重复")
    missing = [hex(c) for c in glyphs_ref if c not in got]
    if missing:
        raise SystemExit(f"{path.name}: 缺字形 {missing[:8]}")
    for cp, (off, adv, w, h, ox, oy) in got.items():
        nib_ref, adv_ref, w_ref, h_ref, ox_ref, oy_ref = glyphs_ref[cp]
        if (adv, w, h, ox, oy) != (adv_ref, w_ref, h_ref, ox_ref, oy_ref):
            raise SystemExit(
                f"{path.name}: U+{cp:04X} 度量不符 "
                f"got adv={adv},w={w},h={h},ox={ox},oy={oy} "
                f"want adv={adv_ref},w={w_ref},h={h_ref},ox={ox_ref},oy={oy_ref}"
            )
        # 回归防线：adv_w 为 8.4 定点（真实值 ×16）。若误发整数像素，
        # 此处范围检查必然失败（半角 ≈ 0.5em、全角 = 1em、带调字符 < 2.5em）。
        real_adv = adv / 16.0
        if not (px * 0.15 <= real_adv <= px * 2.5):
            raise SystemExit(
                f"{path.name}: U+{cp:04X} adv_w={adv}（{real_adv:.1f}px）超出 "
                f"{px}px 字号的 8.4 定点合理范围——疑似漏乘 16"
            )
        decoded = decode_plain_4bpp(bitmap, off, w, h)
        if decoded != nib_ref:
            first = next(
                i for i in range(len(decoded)) if decoded[i] != nib_ref[i]
            )
            raise SystemExit(
                f"{path.name}: U+{cp:04X} 位图不符 @px{first} "
                f"got {decoded[first]} want {nib_ref[first]}"
            )
    return len(got), len(bitmap)


# ---------- 主流程 ----------


def build_charset_subsets(full: list[int]):
    latin = [c for c in full if c < 0x80 or chr(c) in TONE_VOWELS or c in (0xB7,)]
    latin += [ord(c) for c in "★☆·，。、？！：…—（）"]
    return {"full": sorted(set(full)), "latin": sorted(set(latin))}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="仅校验已提交产物")
    args = ap.parse_args()

    charset = collect_charset()
    subsets = build_charset_subsets(charset)

    # 写清单（--check 时清单必须已存在且为输入）
    if not args.check:
        FONTS_DIR.mkdir(parents=True, exist_ok=True)
        CHARSET_FILE.write_text(
            "".join(f"U+{c:04X} {chr(c)}\n" for c in charset), encoding="utf-8"
        )

    if args.check:
        # 清单门禁：源码字面量 ⊆ 已提交清单
        inv = {}
        for line in CHARSET_FILE.read_text(encoding="utf-8").splitlines():
            m = re.match(r"U\+([0-9A-F]{4}) (.*)$", line)
            if m:
                inv[int(m.group(1), 16)] = m.group(2)
        lost = [hex(c) for c in charset if c not in inv]
        if lost:
            raise SystemExit(
                f"源码新增字面量字形缺失于 {CHARSET_FILE.name}: {lost[:8]}；"
                "请运行 python3 tools/gen_app_fonts.py 重新生成"
            )
        # 逐字体校验（需要重新光栅化做逐像素比对）
        for spec in FONT_SPECS:
            path = FONTS_DIR / spec[0]
            if not path.exists():
                raise SystemExit(f"缺少字体产物 {path}")
            _, _, font = load_face(spec[3], spec[2], charset=charset)
            from PIL import Image as _I, ImageDraw as _D  # noqa: F401
            glyphs = {}
            for cp in subsets[spec[4]]:
                glyphs[cp] = rasterize_glyph(font, _D, _I, chr(cp), spec[2])
            n, nbytes = verify_font(path, glyphs, spec[2])
            print(f"  {path.name}: {n} glyphs, bitmap {nbytes} B — verify OK")
        print("gen_app_fonts --check: PASS")
        return 0

    # 生成模式
    for spec in FONT_SPECS:
        image_cls, draw_cls, font = load_face(spec[3], spec[2], charset=charset)
        glyphs = {}
        for cp in subsets[spec[4]]:
            glyphs[cp] = rasterize_glyph(font, draw_cls, image_cls, chr(cp), spec[2])
        src = emit_font(spec, glyphs, spec[5])
        path = FONTS_DIR / spec[0]
        path.write_text(src, encoding="utf-8")
        n, nbytes = verify_font(path, glyphs, spec[2])
        print(f"  {path.name}: {n} glyphs, bitmap {nbytes} B — generated + verified")
    print(f"charset: {len(charset)} codepoints -> {CHARSET_FILE.relative_to(REPO)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
