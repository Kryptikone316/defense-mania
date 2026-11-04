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
    n = 32
    im = Image.new('RGBA', (n * S, n * S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.ellipse((2 * S, 20 * S, 30 * S, 30 * S), fill=(0, 0, 0, 90))
    d.polygon([(3 * S, 20 * S), (16 * S, 14 * S), (29 * S, 20 * S), (16 * S, 27 * S)], fill=(104, 78, 46, 255), outline=(52, 36, 18, 255))
    for k in range(5):
        for j in range(3):
            x = (7 + k * 4.5 + (j % 2) * 2.2) * S; y = (19 + j * 2.2 - k * 0.4) * S
            d.polygon([(x - 1.3 * S, y), (x, y - 8 * S), (x + 1.3 * S, y)], fill=(190, 196, 206, 255), outline=(70, 74, 84, 255))
            d.line((x, y - 8 * S, x, y), fill=(236, 240, 246, 255), width=max(1, S // 2))
    return im.resize((n, n), Image.LANCZOS)

def glue():
    n = 48
    im = Image.new('RGBA', (n * S, n * S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    pts = []
    for i in range(36):
        a = i / 36 * 2 * math.pi
        r = (17 + 3 * math.sin(a * 3 + 1) + 2 * math.sin(a * 5 + 2)) * S
        pts.append((24 * S + math.cos(a) * r, 24 * S + math.sin(a) * r * 0.72))
    d.polygon(pts, fill=(150, 170, 40, 200), outline=(84, 100, 18, 230))
    inner = [(24 * S + (x - 24 * S) * 0.6, 24 * S + (y - 24 * S) * 0.6) for x, y in pts]
    d.polygon(inner, fill=(190, 210, 70, 210))
    for _ in range(6):
        x = (24 + random.uniform(-9, 9)) * S; y = (24 + random.uniform(-5, 5)) * S
        d.ellipse((x - 2 * S, y - 1.5 * S, x + 2 * S, y + 1.5 * S), fill=(236, 246, 150, 220))
    d.ellipse((17 * S, 18 * S, 23 * S, 21 * S), fill=(255, 255, 220, 200))
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
print('traps drawn')
