#!/usr/bin/env python3
"""Draws the three path traps (spikes, glue, boulder): a picture each for the map and a 30 x 30 icon for the menu.
Usage: make_trap_art.py <the game's data folder>      (needs pillow)"""
import os, sys, math, random
from PIL import Image, ImageDraw, ImageFilter
DATA = sys.argv[1]
OUT = os.path.join(DATA, 'traps')
os.makedirs(OUT, exist_ok=True)
S = 4
random.seed(7)

def spikes():
    n = 40
    im = Image.new('RGBA', (n * S, n * S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.ellipse((2 * S, 24 * S, 38 * S, 37 * S), fill=(0, 0, 0, 110))
    d.polygon([(3 * S, 25 * S), (20 * S, 17 * S), (37 * S, 25 * S), (20 * S, 34 * S)], fill=(92, 62, 32, 255), outline=(40, 26, 12, 255))
    d.polygon([(3 * S, 25 * S), (20 * S, 34 * S), (20 * S, 37 * S), (3 * S, 28 * S)], fill=(62, 40, 20, 255))
    d.polygon([(37 * S, 25 * S), (20 * S, 34 * S), (20 * S, 37 * S), (37 * S, 28 * S)], fill=(74, 48, 24, 255))
    for k in range(5):
        for j in range(4):
            x = (8 + k * 5.6 + (j % 2) * 2.8) * S; y = (23.5 + j * 2.4 - k * 0.5 - (j % 2) * 0.2) * S
            d.polygon([(x - 1.9 * S, y), (x, y - 10 * S), (x + 1.9 * S, y)], fill=(206, 212, 224, 255), outline=(60, 64, 76, 255))
            d.line((x - 0.4 * S, y - 9 * S, x - 0.4 * S, y), fill=(255, 255, 255, 255), width=max(1, S // 2))
    return im.resize((n, n), Image.LANCZOS)

def glue():
    n = 56
    im = Image.new('RGBA', (n * S, n * S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    pts = []
    for i in range(36):
        a = i / 36 * 2 * math.pi
        r = (21 + 3.5 * math.sin(a * 3 + 1) + 2.5 * math.sin(a * 5 + 2)) * S
        pts.append((28 * S + math.cos(a) * r, 28 * S + math.sin(a) * r * 0.7))
    d.polygon(pts, fill=(128, 150, 30, 215), outline=(70, 86, 14, 240))
    inner = [(28 * S + (x - 28 * S) * 0.62, 28 * S + (y - 28 * S) * 0.62) for x, y in pts]
    d.polygon(inner, fill=(176, 200, 56, 225))
    for _ in range(6):
        x = (28 + random.uniform(-11, 11)) * S; y = (28 + random.uniform(-6, 6)) * S
        d.ellipse((x - 2 * S, y - 1.5 * S, x + 2 * S, y + 1.5 * S), fill=(236, 246, 150, 220))
    d.ellipse((19 * S, 21 * S, 27 * S, 25 * S), fill=(255, 255, 220, 210))
    d.ellipse((34 * S, 30 * S, 38 * S, 32 * S), fill=(255, 255, 220, 190))
    return im.filter(ImageFilter.GaussianBlur(0.6)).resize((n, n), Image.LANCZOS)

def boulder():
    n = 44
    im = Image.new('RGBA', (n * S, n * S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.ellipse((5 * S, 32 * S, 39 * S, 42 * S), fill=(0, 0, 0, 100))
    d.ellipse((6 * S, 5 * S, 38 * S, 37 * S), fill=(112, 112, 118, 255), outline=(50, 50, 56, 255), width=S)
    d.pieslice((8 * S, 7 * S, 36 * S, 35 * S), 200, 340, fill=(150, 150, 156, 255))
    d.pieslice((9 * S, 9 * S, 37 * S, 37 * S), 20, 120, fill=(78, 78, 86, 255))
    for _ in range(7):
        x = random.uniform(12, 32) * S; y = random.uniform(11, 31) * S; r = random.uniform(1.5, 3.2) * S
        d.ellipse((x - r, y - r * 0.8, x + r, y + r * 0.8), fill=(92, 92, 98, 255))
    d.line((14 * S, 22 * S, 22 * S, 18 * S, 27 * S, 24 * S), fill=(46, 46, 52, 255), width=S // 2)
    d.ellipse((12 * S, 9 * S, 22 * S, 14 * S), fill=(190, 190, 196, 160))
    return im.resize((n, n), Image.LANCZOS)

def cracks(level):
    n = 44
    im = Image.new('RGBA', (n * S, n * S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    lines = [[(14, 12), (19, 19), (17, 25), (22, 31)], [(30, 10), (26, 18), (29, 24)]] + ([[(10, 22), (17, 22), (21, 27)], [(33, 28), (27, 28), (24, 33)], [(22, 19), (28, 21), (33, 18)]] if level > 1 else [])
    for ln in lines:
        d.line([(x * S, y * S) for x, y in ln], fill=(24, 24, 28, 255), width=S)
        d.line([(x * S + S, y * S + S) for x, y in ln], fill=(190, 190, 198, 140), width=S // 2)
    return im.resize((n, n), Image.LANCZOS)

def icon(pic):
    bbox = pic.getbbox(); ic = pic.crop(bbox)
    sc = min(28.0 / ic.size[0], 28.0 / ic.size[1])
    ic = ic.resize((max(1, int(ic.size[0] * sc)), max(1, int(ic.size[1] * sc))), Image.LANCZOS)
    out = Image.new('RGBA', (30, 30), (0, 0, 0, 0))
    out.alpha_composite(ic, ((30 - ic.size[0]) // 2, (30 - ic.size[1]) // 2))
    return out

for i, (name, pic) in enumerate((('spikes', spikes()), ('glue', glue()), ('boulder', boulder())), 1):
    pic.save(os.path.join(OUT, 'trap%d.png' % i))
    icon(pic).save(os.path.join(OUT, 'trap_icon%d.png' % i))
cracks(1).save(os.path.join(OUT, 'trap3_c1.png'))
cracks(2).save(os.path.join(OUT, 'trap3_c2.png'))
print('traps drawn')
