#!/usr/bin/env python3
"""Draws the gold nugget that stands for the currency: money.png (14 x 12, the top-left HUD) and money_small.png (9 x 8, the
price tags of the build menu).

    ~/venvs/pydev/bin/python tools/make_gold_icon.py <data folder> [<preview.png>]
"""
import sys, math
from PIL import Image

OUT = (0, 0, 0, 255)
SHADES = [(150, 92, 0, 255), (206, 140, 0, 255), (255, 200, 0, 255), (255, 232, 74, 255), (255, 250, 200, 255)]   # dark -> sparkle

def nugget(w, h, lumps):
    """lumps: (cx, cy, rx, ry) ellipses whose union is the nugget; shaded from the top left, outlined in black"""
    inside = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            for cx, cy, rx, ry in lumps:
                if ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2 <= 1.0:
                    inside[y][x] = True
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    def at(x, y): return 0 <= x < w and 0 <= y < h and inside[y][x]
    for y in range(h):
        for x in range(w):
            if not inside[y][x]: continue
            edge = not (at(x - 1, y) and at(x + 1, y) and at(x, y - 1) and at(x, y + 1))
            if edge:
                im.putpixel((x, y), OUT); continue
            # light from the top left: how far the pixel sits towards the lit side of its own lump
            best, lit = 9, 0.0
            for cx, cy, rx, ry in lumps:
                d = ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2
                if d <= 1.0 and d < best:
                    best = d; lit = -((x + 0.5 - cx) / rx) * 0.6 - ((y + 0.5 - cy) / ry) * 0.8
            k = 2 + (1 if lit > 0.25 else 0) - (1 if lit < -0.2 else 0) - (1 if lit < -0.55 else 0)
            im.putpixel((x, y), SHADES[max(0, min(3, k))])
    return im, at

def main():
    data = sys.argv[1]
    # the HUD nugget: a big lump, a smaller one climbing up its right side
    big, at = nugget(14, 12, [(6.2, 7.0, 6.0, 4.6), (9.6, 4.6, 3.6, 3.2)])
    for x, y in [(4, 5), (5, 5), (4, 4)]: big.putpixel((x, y), SHADES[4])          # a sparkle on the lit shoulder
    for x, y in [(8, 9), (9, 9), (10, 8)]: big.putpixel((x, y), SHADES[1])          # a facet in the shade
    big.putpixel((8, 3), SHADES[4])
    big.save(data + '/money.png')
    small, _ = nugget(9, 8, [(4.0, 4.8, 3.9, 3.0), (6.2, 3.2, 2.4, 2.2)])
    small.putpixel((3, 3), SHADES[4])
    small.save(data + '/money_small.png')
    if len(sys.argv) > 2:
        S = 12
        sheet = Image.new('RGBA', (14 * S + 9 * S + 40, 12 * S), (130, 100, 0, 255))
        sheet.alpha_composite(big.resize((14 * S, 12 * S), Image.NEAREST), (0, 0))
        sheet.alpha_composite(small.resize((9 * S, 8 * S), Image.NEAREST), (14 * S + 40, 20))
        sheet.save(sys.argv[2])

main()
