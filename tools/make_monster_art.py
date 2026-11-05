#!/usr/bin/env python3
"""Makes the new creatures' sheets from the four the game has (spider, green ogre, winged blue creature, black dog) by recolouring and resizing:
gnoll, orc, half-troll (from the ogre), warg (the dog), wyvern (the winged creature), brood spider (the spider). Same 64 px frames, same layout,
so they work wherever the old ones do. Also a 30 x 30 icon of each for the wave preview.
Usage: make_monster_art.py <the game's data folder>      (needs pillow and numpy)"""
import os, sys
import numpy as np
from PIL import Image
DATA = sys.argv[1]
SRC = os.path.join(DATA, 'maps', 'river_side.xml', 'data')
OUT = os.path.join(DATA, 'monsters')
os.makedirs(OUT, exist_ok=True)
T = 64

def load(n): return Image.open(os.path.join(SRC, n)).convert('RGBA')

def gradient(img, stops, boost=1.0):
    """colour by brightness: stops = [(r,g,b), ...] from dark to light"""
    a = np.array(img).astype(np.float32)
    lum = (0.30 * a[..., 0] + 0.59 * a[..., 1] + 0.11 * a[..., 2]) / 255.0
    lum = np.clip(lum * boost, 0, 1)
    pos = lum * (len(stops) - 1)
    lo = np.clip(np.floor(pos).astype(int), 0, len(stops) - 2)
    f = (pos - lo)[..., None]
    st = np.array(stops, dtype=np.float32)
    rgb = st[lo] * (1 - f) + st[lo + 1] * f
    out = a.copy(); out[..., :3] = rgb
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), 'RGBA')

def brighten(img, mul, add):
    a = np.array(img).astype(np.float32)
    a[..., :3] = np.clip(a[..., :3] * mul + np.array(add, dtype=np.float32), 0, 255)
    return Image.fromarray(a.astype(np.uint8), 'RGBA')

def scale_sheet(img, f, anchor_y=50):
    """every frame grown or shrunk about its feet (the point 32, anchor_y)"""
    if abs(f - 1) < 0.01: return img
    out = Image.new('RGBA', img.size, (0, 0, 0, 0))
    for r in range(img.size[1] // T):
        for c in range(img.size[0] // T):
            fr = img.crop((c * T, r * T, (c + 1) * T, (r + 1) * T))
            n = max(1, int(round(T * f)))
            big = fr.resize((n, n), Image.LANCZOS)
            canvas = Image.new('RGBA', (T, T), (0, 0, 0, 0))
            ox = int(round(32 - 32 * f)); oy = int(round(anchor_y - anchor_y * f))
            canvas.alpha_composite(big, (ox, oy)) if ox >= 0 and oy >= 0 else canvas.paste(big, (ox, oy))
            out.paste(canvas, (c * T, r * T))
    return out

def icon(sheet):
    fr = sheet.crop((T, 0, 2 * T, T))   # the second standing frame, facing the player
    bb = fr.getbbox()
    ic = fr.crop(bb)
    sc = min(26.0 / ic.size[0], 26.0 / ic.size[1], 3.0)
    ic = ic.resize((max(1, int(ic.size[0] * sc)), max(1, int(ic.size[1] * sc))), Image.LANCZOS)
    out = Image.new('RGBA', (30, 30), (0, 0, 0, 0))
    out.alpha_composite(ic, ((30 - ic.size[0]) // 2, (30 - ic.size[1]) // 2))
    return out

spider, ogre, wing, dog = load('charset1.png'), load('charset2.png'), load('charset3.png'), load('charset4.png')
made = {
 'warg':      brighten(gradient(dog, [(30, 32, 40), (96, 102, 116), (200, 206, 216)], 2.2), 1.0, (0, 0, 0)),
 'gnoll':     scale_sheet(gradient(ogre, [(54, 34, 16), (150, 108, 56), (232, 196, 128)], 1.05), 0.84),
 'orc':       scale_sheet(gradient(ogre, [(24, 30, 16), (92, 100, 60), (190, 196, 140)], 1.0), 0.96),
 'halftroll': scale_sheet(gradient(ogre, [(24, 30, 46), (92, 112, 150), (196, 214, 240)], 1.0), 1.2),
 'wyvern':    gradient(wing, [(36, 8, 12), (150, 40, 36), (250, 140, 90)], 1.6),
 'brood':     gradient(spider, [(30, 8, 8), (120, 40, 30), (230, 130, 90)], 3.0),
}
for name, sh in made.items():
    sh.save(os.path.join(OUT, name + '.png'))
    icon(sh).save(os.path.join(OUT, name + '_icon.png'))
print('monsters drawn:', ', '.join(made))
