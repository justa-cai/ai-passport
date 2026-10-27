#!/usr/bin/env python3
"""gen_music_font.py —— 用 Pillow 光栅化生成 LVGL 9 原生格式的中文子集字体。

产出 main/music_font_16.c:
  - 16px、4bpp、stride=0(跨行连续 nibble 位流,与
    lv_font_fmt_txt.c 的 PLAIN 解码路径逐位一致)
  - cmap:ASCII 32..126 用 FORMAT0_TINY(字形 id 1..95 连续);
    其余字符一个 SPARSE_TINY(unicode_list 存相对码点,二分查找)
  - 内置自校验:按 LVGL 解码方式回读每个字形位图,与 Pillow
    原始光栅逐像素比对,不一致立即失败。

为什么不用 lv_font_conv:仓库文档记录 Node 24 下其位图损坏,
故走本脚本。

用法:
  python3 tools/font_tools/gen_music_font.py \
      --font tmp/fonts/NotoSansSC-Regular.otf \
      --chars assets/fonts/music_chars.txt \
      --out main/music_font_16.c

源字体 Noto Sans SC(SIL OFL 1.1)TTF/OTF 不入库,再获取地址与
许可说明见 assets/README.md。
"""
import argparse
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont

PX = 16          # 字号
BPP = 4          # 每像素位数
ASCII_FIRST = 0x20
ASCII_LAST = 0x7E


def load_inventory(path: pathlib.Path) -> str:
    chars = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        chars.extend(line)
    uniq = sorted(set(chars))
    missing_ascii = [c for c in map(chr, range(ASCII_FIRST, ASCII_LAST + 1)) if c not in uniq]
    if missing_ascii:
        sys.exit(f"清单缺少 ASCII 字符: {missing_ascii!r}")
    return "".join(uniq)


class Glyph:
    __slots__ = ("cp", "adv_px", "box_w", "box_h", "ofs_x", "ofs_y", "pixels")


def rasterize(font: ImageFont.FreeTypeFont, cp: int) -> Glyph:
    """渲染单个字符:返回基线坐标系下的推进量/包围盒与 4bpp 像素矩阵。"""
    g = Glyph()
    g.cp = cp
    ch = chr(cp)
    g.adv_px = round(font.getlength(ch))
    bbox = font.getbbox(ch, anchor="ls")  # 基线左原点,y 向下为正
    x0, y0, x1, y1 = bbox
    g.box_w = max(0, x1 - x0)
    g.box_h = max(0, y1 - y0)
    g.ofs_x = x0
    g.ofs_y = -y0  # LVGL ofs_y 以基线为原点、向上为正
    if g.box_w == 0 or g.box_h == 0:
        g.pixels = []
        return g
    img = Image.new("L", (g.box_w + 8, g.box_h + 8), 0)
    draw = ImageDraw.Draw(img)
    # 在画布上以基线锚点落笔,使包围盒左上角位于 (4, 4)
    draw.text((4 - x0, 4 - y0), ch, font=font, fill=255, anchor="ls")
    g.pixels = []
    for y in range(g.box_h):
        for x in range(g.box_w):
            g.pixels.append(img.getpixel((4 + x, 4 + y)) * 15 // 255)
    return g


def pack_bitmap(glyphs: list) -> tuple:
    """把所有字形打包成连续字节流;每个字形起始字节对齐,返回 (bytes, 索引表)。

    stride=0 语义:单字形内部像素跨行连续编码,高 nibble 在前
    (与 lv_font_fmt_txt.c bpp==4 解码循环一致);字形之间按字节对齐。
    """
    data = bytearray()
    indexes = []
    for g in glyphs:
        indexes.append(len(data))
        n = len(g.pixels)
        for i in range(0, n, 2):
            hi = g.pixels[i]
            lo = g.pixels[i + 1] if i + 1 < n else 0
            data.append((hi << 4) | lo)
    return bytes(data), indexes


def verify(glyphs, data: bytes, indexes) -> None:
    """按 LVGL 解码方式回读位图,与原始像素逐位比对。"""
    for g, bi in zip(glyphs, indexes):
        if not g.pixels:
            continue
        n = len(g.pixels)
        for i in range(n):
            byte = data[bi + i // 2]
            nib = byte >> 4 if i % 2 == 0 else byte & 0x0F
            if nib != g.pixels[i]:
                sys.exit(
                    f"自校验失败 U+{g.cp:04X} 像素 {i}(行{i // g.box_w} 列{i % g.box_w}):"
                    f"打包 {nib} != 原始 {g.pixels[i]}"
                )
        if (n + 1) // 2 > len(data) - bi:
            sys.exit(f"自校验失败 U+{g.cp:04X}: 位图越界")


def c_bytes(data: bytes) -> str:
    lines = []
    for i in range(0, len(data), 16):
        lines.append("    " + " ".join(f"0x{b:02X}," for b in data[i:i + 16]))
    return "\n".join(lines)


def c_u16(values) -> str:
    lines = []
    for i in range(0, len(values), 12):
        lines.append("    " + " ".join(f"0x{v:04X}," for v in values[i:i + 12]))
    return "\n".join(lines)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", required=True, help="Noto Sans SC TTF/OTF 路径")
    ap.add_argument("--chars", required=True, help="字符清单文件")
    ap.add_argument("--out", required=True, help="输出 C 文件路径")
    args = ap.parse_args()

    inventory = load_inventory(pathlib.Path(args.chars))
    font = ImageFont.truetype(args.font, PX)
    # 行高不取字体名义度量(CJK 字体 ascender ~1.16em,16px 会得到 24px
    # 行距),改为全清单实际墨迹范围 + 1px 呼吸:中文 16px ≈ 20px 行高。
    ascent, descent = font.getmetrics()

    # 字形顺序:0 号保留,ASCII 连续段在前(供 FORMAT0_TINY 直达),
    # 其余按码点升序(供 SPARSE_TINY 二分)。
    ascii_glyphs = [rasterize(font, cp) for cp in range(ASCII_FIRST, ASCII_LAST + 1)]
    other_cps = sorted({ord(c) for c in inventory} - set(range(ASCII_FIRST, ASCII_LAST + 1)))
    other_glyphs = [rasterize(font, cp) for cp in other_cps]
    for g in ascii_glyphs + other_glyphs:
        if g.adv_px * 16 >= 4096:
            sys.exit(f"U+{g.cp:04X} adv_w 超出 12 位")

    glyphs = ascii_glyphs + other_glyphs
    # ofs_y = 基线上方墨迹顶;墨迹底伸到基线下 (box_h - ofs_y)。
    ink_ascent = max(g.ofs_y for g in glyphs)
    ink_descent = max(g.box_h - g.ofs_y for g in glyphs)
    line_height = ink_ascent + ink_descent + 1  # 底部留 1px 呼吸
    base_line = line_height - ink_ascent
    data, indexes = pack_bitmap(glyphs)
    verify(glyphs, data, indexes)

    # 断言清单字符全部有非空字形(空白字符允许零尺寸)
    for g in glyphs:
        if g.cp not in (0x20, 0x3000) and g.box_w == 0:
            sys.exit(f"U+{g.cp:04X} 光栅化为空,清单或字体有误")

    # SPARSE_TINY:unicode_list 存 相对码点,升序。
    range_start = other_cps[0]
    range_length = other_cps[-1] - other_cps[0] + 1
    if range_length > 0xFFFF:
        sys.exit("稀疏段跨度超 16 位,需拆分多个 cmap")
    unicode_list = [cp - range_start for cp in other_cps]
    glyph_id_start_cjk = 1 + len(ascii_glyphs)

    out = []
    w = out.append
    w("// main/music_font_16.c —— 由 tools/font_tools/gen_music_font.py 生成,勿手改。")
    w("// 源字体:Noto Sans SC Regular(SIL OFL 1.1);16px / 4bpp / stride=0 连续位流。")
    w(f"// 字符数:ASCII {len(ascii_glyphs)} + 其他 {len(other_glyphs)};"
      f"位图 {len(data)} 字节;line_height={line_height} base_line={base_line}。")
    w("#include \"lvgl.h\"")
    w("")
    w(f"static const uint8_t glyph_bitmap[] = {{\n{c_bytes(data)}\n}};")
    w("")
    w("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
    w("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, "
      ".ofs_x = 0, .ofs_y = 0} /* id 0 保留(LVGL 以 0 表示未命中) */,")
    for g, bi in zip(glyphs, indexes):
        w(f"    {{.bitmap_index = {bi}, .adv_w = {g.adv_px * 16}, .box_w = {g.box_w}, "
          f".box_h = {g.box_h}, .ofs_x = {g.ofs_x}, .ofs_y = {g.ofs_y}}} "
          f"/* U+{g.cp:04X} '{chr(g.cp) if g.cp >= 0x20 else ' '}' */,")
    w("};")
    w("")
    w(f"static const uint16_t unicode_cjk[] = {{\n{c_u16(unicode_list)}\n}};")
    w("")
    w("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    w("    {")
    w(f"        .range_start = {ASCII_FIRST}, .range_length = {ASCII_LAST - ASCII_FIRST + 1},")
    w("        .glyph_id_start = 1,")
    w("        .unicode_list = NULL, .glyph_id_ofs_list = NULL,")
    w(f"        .list_length = {ASCII_LAST - ASCII_FIRST + 1},")
    w("        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY")
    w("    },")
    w("    {")
    w(f"        .range_start = 0x{range_start:04X}, .range_length = {range_length},")
    w(f"        .glyph_id_start = {glyph_id_start_cjk},")
    w("        .unicode_list = unicode_cjk, .glyph_id_ofs_list = NULL,")
    w(f"        .list_length = {len(unicode_list)},")
    w("        .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY")
    w("    }")
    w("};")
    w("")
    w("static const lv_font_fmt_txt_dsc_t font_dsc = {")
    w("    .glyph_bitmap = glyph_bitmap,")
    w("    .glyph_dsc = glyph_dsc,")
    w("    .cmaps = cmaps,")
    w("    .kern_dsc = NULL,")
    w("    .kern_scale = 0,")
    w("    .cmap_num = 2,")
    w(f"    .bpp = {BPP},")
    w("    .kern_classes = 0,")
    w("    .bitmap_format = LV_FONT_FMT_TXT_PLAIN,")
    w("    .stride = 0,")
    w("};")
    w("")
    w("// 非 const:运行时由 music_fonts.c 挂 fallback(montserrat,补 LV_SYMBOL_*)。")
    w("lv_font_t music_font_16 = {")
    w("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    w("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    w(f"    .line_height = {line_height},")
    w(f"    .base_line = {base_line},")
    w("    .subpx = LV_FONT_SUBPX_NONE,")
    w("    .kerning = LV_FONT_KERNING_NONE,")
    w("    .underline_position = -2,")
    w("    .underline_thickness = 1,")
    w("    .dsc = &font_dsc,")
    w("};")

    pathlib.Path(args.out).write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"生成 {args.out}:{len(data)} 字节位图,{len(glyphs)} 字形,"
          f"line_height={line_height} base_line={base_line}")


if __name__ == "__main__":
    main()
