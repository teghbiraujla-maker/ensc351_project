#!/usr/bin/env python3
"""Turn GUI drawing commands (from `gui_test --dump DIR`) into PNG previews.

    cmake -S . -B build-host -DBUILD_HOST_TESTS=ON && cmake --build build-host
    ./build-host/tests/gui_test --dump previews
    python3 tools/render_preview.py previews        # writes previews/*.png

The real screen uses the RA8875's built-in 8x16 font; here a monospace
TrueType font stands in for it, so letter shapes differ slightly but sizes
and positions are exact. Needs Pillow (pip install pillow).
"""
import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

WIDTH, HEIGHT = 800, 480
FONT_CANDIDATES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/Library/Fonts/Menlo.ttc",
    "C:/Windows/Fonts/consola.ttf",
]


def rgb565(value):
    value = int(value)
    r = (value >> 11) & 0x1F
    g = (value >> 5) & 0x3F
    b = value & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def load_font():
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            return ImageFont.truetype(path, 13)
    return ImageFont.load_default()


FONT = load_font()
_glyphs = {}


def glyph(ch, fg, bg):
    """One 8x16 character cell, like the RA8875's built-in font."""
    key = (ch, fg, bg)
    if key not in _glyphs:
        cell = Image.new("RGB", (8, 16), bg)
        ImageDraw.Draw(cell).text((0, 1), ch, font=FONT, fill=fg)
        _glyphs[key] = cell
    return _glyphs[key]


def render(ops_path, png_path):
    image = Image.new("RGB", (WIDTH, HEIGHT), (0, 0, 0))
    draw = ImageDraw.Draw(image)
    with open(ops_path, encoding="latin-1") as f:
        for line in f:
            line = line.rstrip("\n")
            kind = line[:1]
            if kind == "R":
                x, y, w, h, c = map(int, line[2:].split())
                if w > 0 and h > 0:
                    draw.rectangle([x, y, x + w - 1, y + h - 1], fill=rgb565(c))
            elif kind == "T":
                x0, y0, x1, y1, x2, y2, c = map(int, line[2:].split())
                draw.polygon([(x0, y0), (x1, y1), (x2, y2)], fill=rgb565(c))
            elif kind == "S":
                parts = line[2:].split(" ", 5)
                x, y, scale, fg, bg = map(int, parts[:5])
                text = parts[5] if len(parts) > 5 else ""
                size = scale + 1
                for i, ch in enumerate(text):
                    cell = glyph(ch, rgb565(fg), rgb565(bg)).resize((8 * size, 16 * size), Image.NEAREST)
                    image.paste(cell, (x + i * 8 * size, y))
    image.save(png_path)


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "previews"
    for ops_path in sorted(glob.glob(os.path.join(folder, "*.ops"))):
        png_path = ops_path[:-4] + ".png"
        render(ops_path, png_path)
        print("wrote", png_path)


if __name__ == "__main__":
    main()
