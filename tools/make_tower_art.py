#!/usr/bin/env python3
"""Draws the pictures of the two towers that were missing: finishes the Ballista's sheet (third level: the same frames with a second
upgrade star, like the other towers) and paints the Resupply Depot (sheet + menu icon) from scratch.
Usage: make_tower_art.py <the game's data folder>      (needs pillow and numpy)"""
import os, sys, math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
DATA = sys.argv[1]
TW = os.path.join(DATA, 'towers')
T = 64
S = 4   # drawing scale

# ---- Ballista: a third level
b = Image.open(os.path.join(TW, 'tower5.png')).convert('RGBA')
if b.size[0] < 27 * T:
    new = Image.new('RGBA', (27 * T, b.size[1]), (0, 0, 0, 0))
    new.paste(b, (0, 0))
    a = np.array(b)
    ys, xs = np.nonzero((a[:, 9 * T:10 * T, 3] > 0) & (a[:, 9 * T:10 * T, 0] > 200) & (a[:, 9 * T:10 * T, 1] > 170) & (a[:, 9 * T:10 * T, 2] < 120))
    ys, xs = ys[ys < T], xs[ys < T]      # the star of the first row (a star is yellow)
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    star = b.crop((9 * T + x0, y0, 9 * T + x1 + 1, y1 + 1))
    gap = (x1 - x0) + 3
    for row in range(b.size[1] // T):
        for c in range(9):
            frame = b.crop(((9 + c) * T, row * T, (10 + c) * T, (row + 1) * T)).copy()
            fa = np.array(frame)
            # where is this frame's star? take it from the same place as in the first row
            frame.alpha_composite(star, (int(x0 - gap) , int(y0)))
            new.paste(frame, ((18 + c) * T, row * T))
    new.save(os.path.join(TW, 'tower5.png'))
    print('Ballista: third level added')

# ---- Resupply Depot
def ellipse_shadow(d, cx, cy, rx, ry, alpha):
    d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=(0, 0, 0, alpha))

def crate(d, x, y, w, h, tone=0):
    base = [(140, 98, 52), (156, 110, 58), (122, 84, 44)][tone % 3]
    d.rectangle((x, y, x + w, y + h), fill=base + (255,), outline=(60, 38, 18, 255), width=S)
    d.line((x, y, x + w, y + h), fill=(92, 60, 30, 255), width=S)
    d.line((x + w, y, x, y + h), fill=(92, 60, 30, 255), width=S)
    d.line((x + S, y + S, x + w - S, y + S), fill=(190, 150, 96, 255), width=S // 2)

def barrel(d, x, y, w, h):
    d.ellipse((x, y + h - w // 3, x + w, y + h + w // 3), fill=(80, 52, 26, 255))
    d.rectangle((x, y, x + w, y + h), fill=(132, 92, 48, 255))
    d.ellipse((x, y - w // 3, x + w, y + w // 3), fill=(160, 118, 66, 255), outline=(70, 44, 20, 255), width=S // 2)
    for yy in (y + h // 4, y + 3 * h // 4):
        d.line((x, yy, x + w, yy), fill=(60, 60, 66, 255), width=S)
    d.line((x, y, x, y + h), fill=(60, 38, 18, 255), width=S // 2)
    d.line((x + w, y, x + w, y + h), fill=(60, 38, 18, 255), width=S // 2)

def sack(d, x, y, w, h):
    d.ellipse((x, y, x + w, y + h), fill=(204, 184, 140, 255), outline=(110, 92, 60, 255), width=S // 2)
    d.polygon([(x + w * .35, y + h * .1), (x + w * .5, y - h * .18), (x + w * .65, y + h * .1)], fill=(190, 168, 124, 255), outline=(110, 92, 60, 255))

def star5(d, cx, cy, r):
    pts = []
    for i in range(10):
        ang = -math.pi / 2 + i * math.pi / 5
        rr = r if i % 2 == 0 else r * 0.45
        pts.append((cx + math.cos(ang) * rr, cy + math.sin(ang) * rr))
    d.polygon(pts, fill=(255, 214, 40, 255), outline=(140, 96, 0, 255))

def frame(level, step):
    """one 64 x 64 picture of the depot; step 0..2 moves the pennant"""
    im = Image.new('RGBA', (T * S, T * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    ellipse_shadow(d, 32 * S, 54 * S, 22 * S, 6 * S, 120)
    # the ground: a trodden plank platform
    d.polygon([(10 * S, 52 * S), (32 * S, 44 * S), (54 * S, 52 * S), (32 * S, 60 * S)], fill=(112, 86, 52, 255), outline=(58, 40, 22, 255))
    for k in range(1, 4):
        t = k / 4.0
        d.line((10 * S + 22 * S * t, 52 * S - 8 * S * t, 32 * S + 22 * S * t, 44 * S + 8 * S * t - 8 * S * 0), fill=(78, 56, 32, 255), width=S // 2)
    # stores
    if level == 0:
        crate(d, 15 * S, 42 * S, 12 * S, 10 * S, 0)
        crate(d, 36 * S, 43 * S, 12 * S, 10 * S, 1)
        barrel(d, 27 * S, 44 * S, 8 * S, 9 * S)
        sack(d, 46 * S, 47 * S, 8 * S, 7 * S)
        # a small canvas lean-to
        d.polygon([(18 * S, 40 * S), (32 * S, 22 * S), (46 * S, 40 * S)], fill=(228, 220, 196, 255), outline=(96, 84, 60, 255))
        d.polygon([(32 * S, 22 * S), (46 * S, 40 * S), (36 * S, 40 * S)], fill=(200, 190, 164, 255))
        d.line((32 * S, 22 * S, 32 * S, 12 * S), fill=(96, 64, 32, 255), width=S)
        top = (32 * S, 12 * S)
    else:
        crate(d, 12 * S, 41 * S, 12 * S, 11 * S, 0)
        crate(d, 12 * S, 31 * S, 12 * S, 10 * S, 2)
        crate(d, 40 * S, 42 * S, 13 * S, 10 * S, 1)
        barrel(d, 25 * S, 45 * S, 8 * S, 8 * S)
        barrel(d, 53 * S, 46 * S, 7 * S, 7 * S)
        sack(d, 4 * S, 46 * S, 8 * S, 7 * S)
        # a larger striped awning in blue and cream, on two posts
        d.line((20 * S, 44 * S, 20 * S, 26 * S), fill=(96, 64, 32, 255), width=S)
        d.line((44 * S, 44 * S, 44 * S, 26 * S), fill=(96, 64, 32, 255), width=S)
        d.polygon([(14 * S, 28 * S), (32 * S, 16 * S), (50 * S, 28 * S), (46 * S, 32 * S), (18 * S, 32 * S)], fill=(232, 224, 200, 255), outline=(60, 70, 120, 255))
        for k in range(5):
            x0 = 14 * S + k * 7.2 * S
            d.polygon([(x0, 28 * S), (x0 + 3.6 * S, 28 * S + 0), (32 * S + (x0 - 32 * S) * 0.55 + 1.8 * S * 0, 16 * S + 6 * S * 0), (32 * S + (x0 - 32 * S) * 0.55, 16 * S)], fill=(46, 82, 150, 255)) if False else None
        for k in range(4):
            xa = 14 * S + k * 9 * S
            d.polygon([(xa, 28 * S), (xa + 4.5 * S, 28 * S), (32 * S + (xa + 4.5 * S - 32 * S) * 0.45, 20 * S), (32 * S + (xa - 32 * S) * 0.45, 20 * S)], fill=(52, 92, 168, 255))
        d.line((14 * S, 28 * S, 32 * S, 16 * S), fill=(60, 70, 120, 255), width=S // 2)
        d.line((50 * S, 28 * S, 32 * S, 16 * S), fill=(60, 70, 120, 255), width=S // 2)
        # a lantern
        d.line((32 * S, 32 * S, 32 * S, 35 * S), fill=(40, 40, 40, 255), width=S // 2)
        glow = (255, 200 + step * 15, 90 + step * 20, 255)
        d.rectangle((30 * S, 35 * S, 34 * S, 40 * S), fill=glow, outline=(60, 44, 20, 255))
        d.line((32 * S, 16 * S, 32 * S, 8 * S), fill=(96, 64, 32, 255), width=S)
        top = (32 * S, 8 * S)
    # the pennant, three little waves
    off = (-1, 0, 1)[step] * S
    px, py = top
    d.polygon([(px, py), (px + 10 * S, py + 2 * S + off), (px + 4 * S, py + 3 * S), (px + 11 * S, py + 6 * S - off), (px, py + 6 * S)], fill=(52, 92, 168, 255), outline=(26, 40, 90, 255))
    d.ellipse((px + 2 * S, py + S, px + 5 * S, py + 4 * S), fill=(255, 214, 40, 255))
    if level == 1:
        star5(d, 56 * S, 7 * S, 5 * S)
    return im.resize((T, T), Image.LANCZOS)

sheet = Image.new('RGBA', (18 * T, 8 * T), (0, 0, 0, 0))
for level in (0, 1):
    for step in range(3):
        f = frame(level, step)
        for row in range(8):
            sheet.paste(f, ((level * 9 + step) * T, row * T))
sheet.save(os.path.join(TW, 'tower6.png'))
# the menu icon: the first level, trimmed, on 30 x 30
f0 = frame(0, 0)
bbox = f0.getbbox()
ic = f0.crop(bbox)
sc = min(28.0 / ic.size[0], 28.0 / ic.size[1])
ic = ic.resize((max(1, int(ic.size[0] * sc)), max(1, int(ic.size[1] * sc))), Image.LANCZOS)
icon = Image.new('RGBA', (30, 30), (0, 0, 0, 0))
icon.alpha_composite(ic, ((30 - ic.size[0]) // 2, (30 - ic.size[1]) // 2))
icon.save(os.path.join(TW, 'tower_icon6.png'))
print('Resupply Depot: tower6.png and tower_icon6.png written')
