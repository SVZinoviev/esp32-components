#!/usr/bin/env python3
"""Generate a gfx bitmap font (C source) from a TTF/OTF/OTB font.

Rasterizes a contiguous range of code points at a fixed pixel size into the
1-bit-per-pixel format consumed by `struct gfx_font` (see gfx_font.h), and
writes a <name>.c data file plus a <name>.h with the extern declaration.

Works with scalable outline fonts (TTF/OTF), which render at any --size, and
with embedded-bitmap fonts (OTB, "OpenType Bitmap"; also BDF/PCF), which only
exist at fixed strike sizes. For a bitmap font, pass a --size that matches one
of its strikes; if it does not, the nearest available strike is used (and a
note is printed). Use --list-sizes to see a font's available strike sizes.

Requires Pillow:  pip install pillow

Examples:
    # Scalable outline font:
    python3 tools/otf_to_gfxfont.py \\
        --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \\
        --size 16 --range 32-126 --name font_dejavu16 \\
        --output main/font_dejavu16.c

    # OTB bitmap font (discover strikes, then generate):
    python3 tools/otf_to_gfxfont.py --font Bm437_IBM_VGA_8x16.otb --list-sizes
    python3 tools/otf_to_gfxfont.py --font Bm437_IBM_VGA_8x16.otb \\
        --size 16 --name font_vga --output main/font_vga.c

    # Full CP437 set incl. box-drawing / shade glyphs (indexed by byte 32-255):
    python3 tools/otf_to_gfxfont.py --font Bm437_IBM_VGA_8x16.otb \\
        --size 16 --range 32-255 --codepage cp437 \\
        --name font_vga437 --output main/font_vga437.c
"""
import argparse
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("error: Pillow is required (pip install pillow)")


def parse_range(text):
    """'32-126' -> (32, 126)."""
    lo, hi = text.split("-")
    lo, hi = int(lo, 0), int(hi, 0)
    if lo > hi:
        raise ValueError("range low > high")
    return lo, hi


def available_strikes(path, hi=256):
    """Return the list of pixel sizes a bitmap-strike font supports.

    Embedded-bitmap fonts (OTB/BDF/PCF) only load at fixed sizes; FreeType
    raises OSError("invalid pixel size") for any other. Probe the range and
    collect the sizes that load. Only meaningful for bitmap fonts (a scalable
    font would accept every size).
    """
    sizes = []
    for s in range(1, hi + 1):
        try:
            ImageFont.truetype(path, s)
            sizes.append(s)
        except OSError:
            pass
    return sizes


def load_font(path, size):
    """Load a font at @p size, tolerating bitmap-strike fonts.

    Outline fonts load at any size. For a bitmap font whose strikes do not
    include @p size, fall back to the nearest available strike and warn.
    Returns the loaded font (its real pixel size is font.size).
    """
    try:
        return ImageFont.truetype(path, size)
    except OSError:
        strikes = available_strikes(path)
        if not strikes:
            sys.exit(f"error: cannot load '{path}' at any pixel size")
        nearest = min(strikes, key=lambda s: abs(s - size))
        print(f"note: '{os.path.basename(path)}' is a bitmap font; size {size} "
              f"is not an available strike {strikes}; using {nearest}.",
              file=sys.stderr)
        return ImageFont.truetype(path, nearest)


def codepoint_to_char(cp, codepage):
    """Map a font table index to the Unicode character to rasterize.

    Without a codepage the index is treated as a Unicode code point. With one
    (e.g. 'cp437') the index is a byte value decoded through that codepage, so
    the extended range maps to the box-drawing / accented glyphs the font
    actually carries. Returns None for byte values the codepage leaves
    undefined.
    """
    if codepage is None:
        return chr(cp)
    if cp > 0xFF:
        raise ValueError(f"code point {cp} out of range for codepage {codepage}")
    try:
        return bytes([cp]).decode(codepage)
    except UnicodeDecodeError:
        return None


def rasterize(font, ch, ascent):
    """Return (width, height, x_advance, x_offset, y_offset, bits[]).

    bits is a list of 0/1, row-major, len == width * height.
    """
    advance = round(font.getlength(ch))
    bbox = font.getbbox(ch)  # (left, top, right, bottom), baseline at y=ascent
    left, top, right, bottom = bbox
    width, height = right - left, bottom - top
    if width <= 0 or height <= 0:  # whitespace / empty glyph
        return 0, 0, advance, 0, 0, []

    img = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(img)
    # Shift so the ink bounding box starts at (0, 0).
    draw.text((-left, -top), ch, fill=255, font=font)
    px = img.load()
    bits = [1 if px[x, y] >= 128 else 0
            for y in range(height) for x in range(width)]
    return width, height, advance, left, top - ascent, bits


def pack_bits(bits):
    """Pack a list of 0/1 into bytes, MSB first."""
    out = bytearray()
    acc = 0
    n = 0
    for bit in bits:
        acc = (acc << 1) | bit
        n += 1
        if n == 8:
            out.append(acc)
            acc, n = 0, 0
    if n:
        out.append(acc << (8 - n))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--font", required=True, help="path to .ttf/.otf/.otb")
    ap.add_argument("--size", type=int, help="pixel size (strike size for OTB)")
    ap.add_argument("--range", default="32-126",
                    help="inclusive code point range, e.g. 32-126")
    ap.add_argument("--name", help="C identifier for the font")
    ap.add_argument("--output", help="output .c path")
    ap.add_argument("--codepage", default=None,
                    help="treat table indices as bytes in this codepage "
                         "(e.g. cp437) so the extended range 128-255 maps to "
                         "the font's box-drawing/accented glyphs; omit to use "
                         "Unicode code points directly")
    ap.add_argument("--list-sizes", action="store_true",
                    help="print the font's available bitmap strike sizes and exit")
    args = ap.parse_args()

    if args.list_sizes:
        strikes = available_strikes(args.font)
        if strikes:
            print("bitmap strike sizes:", ", ".join(map(str, strikes)))
        else:
            print("scalable outline font: any size works")
        return

    if args.size is None or not args.name or not args.output:
        ap.error("--size, --name and --output are required to generate a font")

    lo, hi = parse_range(args.range)
    if args.codepage and hi > 0xFF:
        ap.error(f"--range {lo}-{hi} exceeds 255; codepage indices are bytes")
    # Validate the codepage early with a clear error.
    if args.codepage:
        try:
            bytes([32]).decode(args.codepage)
        except LookupError:
            ap.error(f"unknown codepage '{args.codepage}'")

    font = load_font(args.font, args.size)
    actual_size = getattr(font, "size", args.size)
    ascent, descent = font.getmetrics()
    y_advance = ascent + descent

    bitmap = bytearray()
    glyphs = []  # (offset, w, h, adv, xoff, yoff)
    for cp in range(lo, hi + 1):
        ch = codepoint_to_char(cp, args.codepage)
        if ch is None:  # byte undefined in this codepage -> empty glyph
            glyphs.append((len(bitmap), 0, 0, 0, 0, 0))
            continue
        w, h, adv, xoff, yoff, bits = rasterize(font, ch, ascent)
        offset = len(bitmap)
        bitmap += pack_bits(bits)
        glyphs.append((offset, w, h, adv, xoff, yoff))

    c_path = args.output
    base = os.path.splitext(os.path.basename(c_path))[0]
    h_path = os.path.splitext(c_path)[0] + ".h"
    name = args.name

    with open(c_path, "w") as f:
        f.write(f'/* Generated by otf_to_gfxfont.py from '
                f'{os.path.basename(args.font)} @ {actual_size}px. Do not edit. */\n')
        f.write(f'#include "{base}.h"\n\n')
        f.write(f"static const uint8_t {name}_bitmap[] = {{\n")
        for i in range(0, len(bitmap), 12):
            row = ", ".join(f"0x{b:02X}" for b in bitmap[i:i + 12])
            f.write(f"    {row},\n")
        f.write("};\n\n")

        f.write(f"static const struct gfx_glyph {name}_glyphs[] = {{\n")
        for cp, (off, w, h, adv, xoff, yoff) in zip(range(lo, hi + 1), glyphs):
            mapped = codepoint_to_char(cp, args.codepage)
            ch = mapped if (mapped and mapped.isprintable()) else ""
            f.write(f"    {{{off}, {w}, {h}, {adv}, {xoff}, {yoff}}},"
                    f"  /* 0x{cp:02X} {ch} */\n")
        f.write("};\n\n")

        f.write(f"const struct gfx_font {name} = {{\n")
        f.write(f"    .bitmap = {name}_bitmap,\n")
        f.write(f"    .glyphs = {name}_glyphs,\n")
        f.write(f"    .first_char = {lo},\n")
        f.write(f"    .last_char = {hi},\n")
        f.write(f"    .y_advance = {y_advance},\n")
        f.write(f"    .ascent = {ascent},\n")
        f.write("};\n")

    with open(h_path, "w") as f:
        f.write("/* Generated by otf_to_gfxfont.py. Do not edit. */\n")
        f.write("#pragma once\n\n")
        f.write('#include "gfx_font.h"\n\n')
        f.write(f"extern const struct gfx_font {name};\n")

    print(f"wrote {c_path} and {h_path}: {hi - lo + 1} glyphs, "
          f"{len(bitmap)} bitmap bytes, {y_advance}px line height")


if __name__ == "__main__":
    main()
