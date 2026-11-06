#!/usr/bin/env python3
"""Draws the per-map versions of the top-left HUD plank (gold and lives) as small pixel-art touches on the original plank:
moss for the Swamp, snow for the Tundra, river rocks, saplings for Dark Woods, stone for the Mountains, a compass for the Maze.
Bigger keeps the original plank (no file).

    ~/venvs/pydev/bin/python tools/make_hud_variants.py <data folder> [<contact sheet.png>]

Writes <data>/hud/<map>.png, each 160 x 39: the plank stretched to the width the game lays the gold and lives out for (the game
stretches the same way for any other width and repeats the strip at columns 70-86, which every variant keeps free of decoration),
with the decoration baked in. Deterministic: the same files come out every time."""
import sys, os, random
from PIL import Image

W, H = 160, 39
END, MIDX, MIDW = 16, 50, 50

def base_plank(src):
    im = Image.open(src).convert('RGBA')
    out = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    out.alpha_composite(im.crop((0, 0, MIDX, H)), (0, 0))
    body = W - END
    x = MIDX
    while x < body:
        w = min(MIDW, body - x)
        out.alpha_composite(im.crop((MIDX, 0, MIDX + w, H)), (x, 0))
        x += w
    out.alpha_composite(im.crop((im.width - END, 0, im.width, H)), (body, 0))
    return out

def edges(im):
    top, bot = [], []
    for x in range(W):
        col = [im.getpixel((x, y))[3] > 128 for y in range(H)]
        top.append(next((y for y in range(H) if col[y]), None))
        bot.append(next((y for y in range(H - 1, -1, -1) if col[y]), None))
    return top, bot

def put(im, x, y, c):
    if 0 <= x < W and 0 <= y < H:
        im.putpixel((x, y), c if len(c) == 4 else c + (255,))

def solid(im, x, y):
    return 0 <= x < W and 0 <= y < H and im.getpixel((x, y))[3] > 128

def tint(im, mul, add=(0, 0, 0), only_wood=True):
    """colours the plank: multiplies and adds to every opaque pixel"""
    for y in range(H):
        for x in range(W):
            r, g, b, a = im.getpixel((x, y))
            if a:
                im.putpixel((x, y), (max(0, min(255, int(r * mul[0] + add[0]))), max(0, min(255, int(g * mul[1] + add[1]))),
                                     max(0, min(255, int(b * mul[2] + add[2]))), a))

def frost(im):
    """half way from the brown wood to a cold slate"""
    for y in range(H):
        for x in range(W):
            r, g, b, a = im.getpixel((x, y))
            if a:
                l = 0.3 * r + 0.59 * g + 0.11 * b
                sl = (l * 0.9 + 6, l * 1.0 + 16, l * 1.1 + 34)
                im.putpixel((x, y), (min(255, int((r + sl[0]) / 2)), min(255, int((g + sl[1]) / 2)), min(255, int((b + sl[2]) / 2)), a))

def blob(im, cx, cy, rx, ry, cols, rnd, shade=True):
    """a small rounded lump in cols = (dark, mid, light), light up and to the left"""
    for y in range(cy - ry, cy + ry + 1):
        for x in range(cx - rx, cx + rx + 1):
            d = ((x - cx) / (rx + 0.3)) ** 2 + ((y - cy) / (ry + 0.3)) ** 2
            if d <= 1:
                c = cols[1]
                if shade:
                    if (x - cx) + (y - cy) < -(rx + ry) * 0.35: c = cols[2]
                    elif (x - cx) + (y - cy) > (rx + ry) * 0.45: c = cols[0]
                put(im, x, y, c)

# ---------------------------------------------------------------- the maps

def swamp(im, top, bot):
    """Spanish moss: grey-green beards draped from the top edge and hanging below the bottom edge, on a slightly murkier plank"""
    tint(im, (0.9, 0.94, 0.84), (0, 3, 0))
    rnd = random.Random(11)
    cols = [(92, 108, 84), (124, 140, 108), (158, 172, 136), (186, 196, 158)]
    def beard(x, y, wid, ln):
        """a tapering clump: wid wide at the top, narrowing to a ragged strand"""
        for k in range(ln):
            w = max(1, round(wid * (1 - k / ln)))
            x0 = x + (wid - w) // 2 + (1 if (k // 2) % 2 and w < 3 else 0)
            for i in range(w):
                c = cols[3] if k == 0 else cols[2] if (i + k) % 3 == 0 else cols[1] if (i + k) % 3 == 1 else cols[0]
                if k > ln - 3: c = cols[0]
                put(im, x0 + i, y + k, c)
    x = 1
    while x < 150:
        beard(x, 0, rnd.randint(4, 6), rnd.randint(4, 8))
        x += rnd.randint(7, 12)
    for x, wid, ln in [(2, 5, 14), (20, 4, 10), (92, 5, 11), (126, 4, 10), (149, 5, 15)]:   # the longer drapes
        beard(x, 3, wid, ln)
    x = 8
    while x < 152:
        beard(x, (bot[x] or 31) - 1, rnd.randint(3, 4), rnd.randint(4, 7))
        x += rnd.randint(14, 22)

def tundra(im, top, bot):
    """a cold-blue plank with a snow cap on top and a few icicles under the bottom edge"""
    frost(im)
    rnd = random.Random(5)
    white, shade, ice = (246, 249, 253), (196, 214, 234), (190, 226, 248)
    for x in range(W):   # snow caps the top: a ragged white band, thicker in humps
        if top[x] is None: continue
        depth = 3 + (2 if (x // 9) % 2 == 0 else 0) + (1 if (x // 4) % 3 == 0 else 0)
        for y in range(0, depth):
            put(im, x, y, white if y < depth - 1 else shade)
    for x in range(W - 18, W):   # and over the ragged right end
        if top[x] is not None and top[x] > 0:
            for y in range(top[x], top[x] + 3): put(im, x, y, white)
    for x0, ln in [(10, 5), (22, 3), (38, 6), (52, 3), (92, 4), (108, 6), (121, 3), (134, 5), (148, 4)]:   # icicles
        for k in range(ln):
            y = (bot[x0] or 31) + 1 + k
            put(im, x0, y, ice if k < ln - 1 else white)
            if k < 2: put(im, x0 + 1, y, shade)
    for x in range(4, 150, 11):   # a few snowflakes on the wood
        put(im, x + rnd.randint(0, 3), rnd.randint(14, 26), (225, 236, 248))

def river(im, top, bot):
    """river stones along the bottom edge, in two heaps, and a few tucked in the top corners"""
    tint(im, (0.96, 0.98, 1.0), (0, 2, 5))
    rnd = random.Random(3)
    stones = [((86, 90, 96), (128, 132, 138), (172, 176, 180)), ((108, 98, 86), (152, 140, 122), (192, 180, 160)), ((78, 92, 106), (118, 132, 146), (162, 176, 190))]
    placed = [(5, 4, 3), (13, 3, 2), (20, 4, 3), (29, 3, 2), (37, 4, 2), (48, 3, 2), (93, 4, 3), (101, 3, 2), (109, 4, 3), (118, 3, 2), (127, 4, 3), (135, 3, 2), (143, 4, 3), (152, 3, 2)]
    for i, (x, rx, ry) in enumerate(placed):
        by = (bot[min(x, W - 1)] or 31) - 1
        blob(im, x, by, rx, ry, stones[i % 3], rnd)
    for x, y, rx, ry, k in [(153, 6, 3, 2, 1), (148, 9, 2, 2, 0), (143, 4, 2, 1, 2)]:   # three more, stacked in the top right
        blob(im, x, y, rx, ry, stones[k], rnd)

def woods(im, top, bot):
    """saplings growing from the bottom edge, with fallen autumn leaves and a few dark leaves along the top"""
    tint(im, (0.9, 0.9, 0.82), (0, 0, 0))
    rnd = random.Random(9)
    stem = (88, 60, 24)
    leaf = [(40, 92, 28), (70, 128, 38), (110, 164, 56)]
    autumn = [(150, 76, 20), (192, 112, 30), (214, 150, 44)]
    def sapling(x, y, h):
        for k in range(h): put(im, x, y - k, stem)
        for hh, side in [(3, 1), (5, -1), (7, 1)]:
            if hh >= h: continue
            put(im, x + side, y - hh, leaf[1]); put(im, x + 2 * side, y - hh, leaf[2]); put(im, x + 3 * side, y - hh - 1, leaf[2])
            put(im, x + side, y - hh + 1, leaf[0]); put(im, x + 2 * side, y - hh + 1, leaf[0])
        for dx in (-1, 0, 1): put(im, x + dx, y - h, leaf[1 + (dx == 0)])
        put(im, x, y - h - 1, leaf[2])
    for x, h in [(6, 8), (30, 6), (92, 7), (151, 9)]:
        sapling(x, (bot[x] or 31) - 1, h)
    # leaves lying along the bottom edge and a few autumn ones among the green at the top
    for x in [16, 24, 41, 52, 101, 115, 124, 137, 145]:
        c = autumn[rnd.randint(0, 2)] if rnd.random() < 0.6 else leaf[rnd.randint(1, 2)]
        y = (bot[x] or 31) - 1
        put(im, x, y, c); put(im, x + 1, y, c); put(im, x + 1, y - 1, c); put(im, x, y + 1, leaf[0] if c in leaf else autumn[0])
    for x, y in [(10, 3), (34, 5), (63, 2), (84, 4), (104, 3), (127, 5), (146, 2)]:
        c = autumn[rnd.randint(0, 2)]
        put(im, x, y, c); put(im, x + 1, y, c); put(im, x, y + 1, c); put(im, x + 1, y + 1, autumn[0])

def mountain(im, top, bot):
    """a grey-brown plank with a broken row of stones over the leaves, stone chips and a few rocks on the bottom edge"""
    tint(im, (0.88, 0.86, 0.8), (14, 12, 10))
    rnd = random.Random(21)
    rock = [(84, 84, 88), (126, 126, 130), (170, 170, 172)]
    x = 1
    while x < W - 4:
        rx = rnd.randint(3, 5)
        ry = rnd.randint(2, 3)
        blob(im, x + rx, ry - 1, rx, ry, rock, rnd)
        x += rx * 2 + rnd.choice([1, 3, 5, 7])   # gaps, so the leaves still show between the stones
    for x, y in [(30, 18), (66, 24), (94, 16), (120, 22), (140, 18)]:   # chips and cracks
        put(im, x, y, rock[2]); put(im, x + 1, y + 1, rock[0]); put(im, x + 2, y + 1, rock[0])
    for x, rx in [(18, 3), (50, 3), (112, 4), (148, 3)]:
        blob(im, x, (bot[x] or 31) - 1, rx, 2, rock, rnd)

def maze(im, top, bot):
    """a small compass rose in the right end and a dotted trail along the bottom edge (the hedge-green leaves stay)"""
    tint(im, (0.97, 0.97, 0.9), (0, 0, 0))
    ink, paper, red, gold = (60, 38, 6), (232, 214, 160), (190, 40, 30), (255, 214, 64)
    cx, cy, r = 144, 21, 7
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            d2 = (x - cx) ** 2 + (y - cy) ** 2
            if d2 <= r * r: put(im, x, y, paper)
            if r * r - r * 1.6 <= d2 <= r * r: put(im, x, y, ink)
    for k in range(-5, 6):   # the cross
        put(im, cx, cy + k, ink); put(im, cx + k, cy, ink)
    for k in range(1, 6):    # north arm in red, south in ink
        put(im, cx, cy - k, red)
    put(im, cx - 1, cy - 2, red); put(im, cx + 1, cy - 2, red)
    put(im, cx, cy, gold)
    put(im, cx, cy - r - 2, ink); put(im, cx - 1, cy - r - 1, ink); put(im, cx + 1, cy - r - 1, ink)   # the N tick
    # dotted trail
    for x in range(8, 124, 6):
        y = (bot[x] or 31) - 3 + (1 if (x // 6) % 4 == 2 else 0)
        put(im, x, y, paper); put(im, x + 1, y, paper)
    put(im, 122, (bot[122] or 31) - 4, red); put(im, 123, (bot[123] or 31) - 4, red)   # the x that marks the spot
    put(im, 121, (bot[121] or 31) - 5, red); put(im, 124, (bot[124] or 31) - 5, red)
    put(im, 121, (bot[121] or 31) - 3, red); put(im, 124, (bot[124] or 31) - 3, red)

MAPS = { 'swamp': swamp, 'tundra': tundra, 'river_side': river, 'woods': woods, 'mountains': mountain, 'maze': maze }

def main():
    data = sys.argv[1]
    sheet = sys.argv[2] if len(sys.argv) > 2 else None
    os.makedirs(os.path.join(data, 'hud'), exist_ok=True)
    made = {}
    plain = base_plank(os.path.join(data, 'hud_top.png'))
    for name, fn in MAPS.items():
        im = base_plank(os.path.join(data, 'hud_top.png'))
        top, bot = edges(im)
        fn(im, top, bot)
        im.save(os.path.join(data, 'hud', name + '.png'))
        made[name] = im
    if sheet:
        order = ['plain'] + list(MAPS)
        made['plain'] = plain
        S = 4
        cell = Image.new('RGBA', (W * S, H * S + 6), (96, 130, 40, 255))
        out = Image.new('RGBA', (W * S * 2 + 12, (H * S + 6) * 4), (96, 130, 40, 255))
        for i, n in enumerate(order):
            im = made[n].resize((W * S, H * S), Image.NEAREST)
            out.alpha_composite(im, ((i % 2) * (W * S + 12), (i // 2) * (H * S + 6)))
        out.save(sheet)

main()
