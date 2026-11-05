#!/usr/bin/env python3
"""Draws the Bastion: a small stone keep with two towers, a gate and a blue banner, 72 x 72, to <data>/bastion.png. With a second argument, the Blender render (tools/blender/bastion.py, 256 x 512), the keep is that shrunk to 64 x 128, two of the towers' tiles tall.
Usage: make_bastion_art.py <data folder> [<render png>]   (needs pillow)"""
import os, sys
from PIL import Image, ImageDraw
DATA = sys.argv[1]
if len(sys.argv) > 2:
    big = Image.open(sys.argv[2]).convert('RGBA').convert('RGBa').resize((64, 128), Image.LANCZOS).convert('RGBA')
    out = Image.new('RGBA', (64, 128), (0, 0, 0, 0))
    ImageDraw.Draw(out).ellipse((3, 118, 61, 127), fill=(0, 0, 0, 80))
    out.alpha_composite(big)
    out.save(os.path.join(DATA, 'bastion.png'))
    print('bastion 64 x 128 from the render')
    sys.exit(0)
S = 4
n = 72
im = Image.new('RGBA', (n * S, n * S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
def stone(x0, y0, x1, y1, base=(150, 146, 140)):
    d.rectangle((x0 * S, y0 * S, x1 * S, y1 * S), fill=base + (255,), outline=(60, 56, 52, 255), width=S // 2)
    for yy in range(int(y0) + 4, int(y1), 6):
        d.line((x0 * S, yy * S, x1 * S, yy * S), fill=(100, 96, 90, 255), width=max(1, S // 4))
d.ellipse((4 * S, 56 * S, 68 * S, 70 * S), fill=(0, 0, 0, 110))
stone(18, 30, 54, 62, (160, 156, 150))              # the curtain wall
for k in range(6):                                  # its battlements
    x = 18 + k * 7
    stone(x, 25, x + 4.5, 31, (170, 166, 160))
stone(6, 22, 22, 62, (140, 136, 130))              # left tower
stone(50, 22, 66, 62, (140, 136, 130))             # right tower
for tx in (6, 50):                                  # tower tops
    d.polygon([((tx - 1) * S, 22 * S), ((tx + 8) * S, 8 * S), ((tx + 17) * S, 22 * S)], fill=(52, 92, 168, 255), outline=(26, 40, 90, 255))
    d.rectangle(((tx + 6) * S, 32 * S, (tx + 10) * S, 40 * S), fill=(30, 28, 26, 255))   # an arrow slit
d.rectangle((30 * S, 40 * S, 42 * S, 62 * S), fill=(96, 62, 30, 255), outline=(44, 28, 12, 255), width=S // 2)   # the gate
d.pieslice((30 * S, 34 * S, 42 * S, 46 * S), 180, 360, fill=(96, 62, 30, 255), outline=(44, 28, 12, 255))
for k in range(1, 4):
    d.line(((30 + k * 3) * S, 40 * S, (30 + k * 3) * S, 62 * S), fill=(60, 38, 18, 255), width=S // 2)
d.line((36 * S, 25 * S, 36 * S, 8 * S), fill=(90, 62, 30, 255), width=S)             # the flagpole and banner
d.polygon([(36 * S, 8 * S), (50 * S, 12 * S), (36 * S, 18 * S)], fill=(52, 92, 168, 255), outline=(26, 40, 90, 255))
d.ellipse((39 * S, 11 * S, 43 * S, 15 * S), fill=(255, 214, 40, 255))
im.resize((n, n), Image.LANCZOS).save(os.path.join(DATA, 'bastion.png'))
print('bastion drawn')
