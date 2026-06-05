#!/usr/bin/env python3
"""
PNG → C header (RGB565 big-endian) 変換スクリプト

使い方:
    python3 tools/gen_ui_img.py assets/amoled_ui.png \
        src/drivers/display/amoled_ui_img.h \
        amoled_ui_img

依存:
    pip install pillow
"""

import sys
from pathlib import Path
from PIL import Image


def to_rgb565_be(r: int, g: int, b: int) -> int:
    rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return ((rgb565 & 0xFF) << 8) | ((rgb565 >> 8) & 0xFF)


def generate(src: Path, dst: Path, var_name: str) -> None:
    img = Image.open(src).convert("RGB")
    w, h = img.size
    pixels = img.load()

    lines = []
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"// {src.name} -> RGB565 big-endian")
    lines.append(f"// {w}x{h} px")
    lines.append(f"#define {var_name.upper()}_W {w}")
    lines.append(f"#define {var_name.upper()}_H {h}")
    lines.append("")
    lines.append(f"static const uint16_t {var_name}[{h}][{w}] = {{")

    for y in range(h):
        row = ", ".join(f"0x{to_rgb565_be(*pixels[x, y]):04X}" for x in range(w))
        comma = "," if y < h - 1 else ""
        lines.append(f"    {{ {row} }}{comma}")

    lines.append("};")
    lines.append("")

    dst.write_text("\n".join(lines), encoding="utf-8")
    print(f"Generated {dst}  ({w}x{h})")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <src.png> <dst.h> <var_name>")
        sys.exit(1)

    generate(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3])
