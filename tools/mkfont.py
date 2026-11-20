#!/usr/bin/env python3
"""Schrift -> 4-Bit-Alpha-Bitmaps als C-Header (gleiches Format wie core/font.h).

  python3 tools/mkfont.py <font.ttf> <px> <name> <zeichen|ascii> <out.h> [verstaerkung]

Braucht Pillow (pip install pillow). Läuft auf Linux und macOS.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

EXTRA = "ÄÖÜäöüß×·–„“”…°€éèáàâñçÉ’"


def main():
    if len(sys.argv) < 6:
        print(__doc__)
        sys.exit(1)
    path, px, name, chars, out = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5]
    stroke = int(sys.argv[6]) if len(sys.argv) > 6 else 0
    if chars == "ascii":
        chars = "".join(chr(c) for c in range(32, 127)) + EXTRA
    font = ImageFont.truetype(path, px)
    ascent, descent = font.getmetrics()
    glyphs = []
    for ch in sorted(set(chars), key=ord):
        if ch != " " and font.getmask(ch).getbbox() is None:
            print("fehlt:", ch, file=sys.stderr)
            continue
        cw, chh = px * 3 + 16, ascent + descent + 16
        img = Image.new("L", (cw, chh), 0)
        d = ImageDraw.Draw(img)
        bx, by = 8, ascent + 8   # Stift auf der Grundlinie
        d.text((bx, by), ch, font=font, fill=255, anchor="ls", stroke_width=stroke, stroke_fill=255)
        adv = round(font.getlength(ch)) + stroke
        bbox = img.point(lambda v: 255 if v > 8 else 0).getbbox()
        g = {"cp": ord(ch), "w": 0, "h": 0, "ox": 0, "oy": 0, "adv": adv, "bits": []}
        if bbox:
            x0, y0, x1, y1 = bbox
            g["w"], g["h"] = x1 - x0, y1 - y0
            g["ox"], g["oy"] = x0 - bx, by - y0
            pix = img.load()
            for y in range(y0, y1):
                for x in range(x0, x1, 2):
                    v0 = (pix[x, y] + 8) // 17
                    v1 = (pix[x + 1, y] + 8) // 17 if x + 1 < x1 else 0
                    g["bits"].append((v0 << 4) | v1)
        glyphs.append(g)

    base = os.path.basename(path).split("-")[0].split(".")[0]
    o = [f"// Erzeugt von tools/mkfont.py, abgeleitet von {base} {px} px (SIL Open Font License 1.1, siehe licenses/)\n",
         '#pragma once\n#include "font.h"\n\nnamespace fonts {\n\n', f"static const uint8_t {name}_bits[] = {{"]
    off, offs = 0, []
    for g in glyphs:
        offs.append(off)
        o.append("\n " + "".join(f"0x{b:02X}," for b in g["bits"]))
        off += len(g["bits"])
    o.append(f"\n}};\n\nstatic const Glyph {name}_glyphs[] = {{\n")
    for g, of in zip(glyphs, offs):
        o.append(f"  {{0x{g['cp']:x}, {g['w']}, {g['h']}, {g['ox']}, {g['oy']}, {g['adv']}, {of}}},\n")
    o.append(f"}};\n\nconst Font {name} = {{{name}_glyphs, {len(glyphs)}, {name}_bits, {ascent + descent}, {ascent}}};\n\n}}  // namespace fonts\n")
    with open(out, "w", encoding="utf-8") as f:
        f.write("".join(o))
    print(f"{name}: {len(glyphs)} Zeichen, {off} Byte, Höhe {ascent + descent}, Oberlänge {ascent}")


if __name__ == "__main__":
    main()
