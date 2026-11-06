#!/usr/bin/env python3
"""Draws "The Coast", a 40 x 18 tile (48 px) Defense Mania map the size of Bigger, and writes its data files into <data>/maps/coast.xml/ .

Two large land masses face each other across an ocean inlet. The way in crosses the west one in switchbacks, goes over a long stone-and-timber bridge
(with a paved quay in the middle where towers may stand) and winds across the east one to the exit on the right edge. Towers can only stand on the
marked land spots and on the quay: all of the water, the islands and the bridge deck are closed to building.

The picture is painted here from scratch (the game's grass detail for the ground texture, everything else procedural), so it is original artwork.
Needs numpy, PIL and scipy.
Usage: make_coast.py <the game's data folder> [preview.png]"""
import math, os, random, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage

DATA = sys.argv[1]
OUT = os.path.join(DATA, 'maps', 'coast.xml')
T, W, H = 48, 40, 18
PW, PH = W * T, H * T
rng = np.random.default_rng(4107)
random.seed(4107)

# ------------------------------------------------------------------ the layout
# way points in pixels: switchbacks on the west land, the bridge along y = 576, switchbacks on the east land
BRIDGE_Y = 576
PATH_PX = [(24, 100), (456, 100), (456, 250), (120, 250), (120, 400), (560, 400), (560, BRIDGE_Y),
           (1360, BRIDGE_Y), (1780, BRIDGE_Y), (1780, 400), (1450, 400), (1450, 250), (1760, 250), (1760, 100), (1944, 100)]
DECK_X0, DECK_X1 = 600, 1320            # the timber deck; the stone aprons at each end carry it over the shore
QUAYS = [(720, 912), (1008, 1200)]       # two paved quays on each side of the middle of the bridge, a gap of two tiles between them
QUAY_X0, QUAY_X1 = QUAYS[0][0], QUAYS[-1][1]

def seg_point_dist(px_, py_, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    l2 = dx * dx + dy * dy
    t = 0 if l2 == 0 else max(0, min(1, ((px_ - a[0]) * dx + (py_ - a[1]) * dy) / l2))
    return math.hypot(px_ - (a[0] + t * dx), py_ - (a[1] + t * dy))

def path_dist(x, y):
    return min(seg_point_dist(x, y, a, b) for a, b in zip(PATH_PX, PATH_PX[1:]))

def centre(c, r): return (c * T + T // 2, r * T + T // 2)

PT = {(c, r) for c in range(W) for r in range(H) if path_dist(*centre(c, r)) <= 41}
SAMPLES = []
for a, b in zip(PATH_PX, PATH_PX[1:]):
    n = int(math.hypot(b[0] - a[0], b[1] - a[1]) // 16)
    for i in range(n):
        SAMPLES.append((a[0] + (b[0] - a[0]) * i / n, a[1] + (b[1] - a[1]) * i / n))
REACH = 1.9 * T

# ------------------------------------------------------------------ noise, textures
yy, xx = np.mgrid[0:PH, 0:PW].astype(np.float32)

def noise(scale, seed=0):
    r = np.random.default_rng(seed)
    n = r.random((PH // scale + 3, PW // scale + 3)).astype(np.float32)
    im = Image.fromarray((n * 255).astype(np.uint8)).resize((PW + 3 * scale, PH + 3 * scale), Image.BICUBIC)
    return np.asarray(im, dtype=np.float32)[:PH, :PW] / 255.0

N1, N2, N3, N4 = noise(160, 1), noise(60, 2), noise(24, 3), noise(9, 4)

ts = Image.open(os.path.join(DATA, 'maps', 'woods.xml', 'data', 'tileset.png')).convert('RGB')
luma = np.asarray(ts.crop((0, 0, T, T)).convert('L'), dtype=np.float32) / 255.0
luma = (luma - luma.mean()) / (luma.std() + 1e-6)
DETAIL = np.zeros((PH, PW), np.float32)
for c in range(W):
    for r in range(H):
        t_ = luma
        if rng.random() < .5: t_ = t_[:, ::-1]
        if rng.random() < .5: t_ = t_[::-1, :]
        if rng.random() < .5: t_ = t_.T
        DETAIL[r * T:(r + 1) * T, c * T:(c + 1) * T] = t_

def colour_field(c0, c1, mix, detail_amt=0.10):
    c0, c1 = np.array(c0, np.float32), np.array(c1, np.float32)
    f = c0[None, None, :] + (c1 - c0)[None, None, :] * mix[:, :, None]
    return f * (1 + detail_amt * DETAIL[:, :, None])

def to_img(arr): return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))
def soft_mask(m, blur):
    return np.asarray(Image.fromarray((np.clip(m, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(blur)), np.float32) / 255.0
def paste_colour(base, colour_arr, mask): return base * (1 - mask[:, :, None]) + colour_arr * mask[:, :, None]
def poly_mask(draw_fn, blur=0):
    im = Image.new('L', (PW, PH), 0)
    draw_fn(ImageDraw.Draw(im))
    m = np.asarray(im, np.float32) / 255.0
    return soft_mask(m, blur) if blur else m
def smooth(x): return x * x * (3 - 2 * x)

# ------------------------------------------------------------------ 1. the water and the land
CX = 960
def half_width(y):
    """a strait: two headlands reach towards each other at the bridge and the water opens out into bays to the north and the south"""
    return 238 + 112 * np.clip(np.abs(y - BRIDGE_Y) / 330.0, 0, 1) ** 1.3 + 14 * np.sin(y / 90.0 + 0.6)
wx = xx + 34 * (N1 - 0.5) * 2 + 18 * (N2 - 0.5) * 2 + 12 * (N3 - 0.5) * 2
water_ch = (wx > CX - half_width(yy) + 20 * np.sin(yy / 61.0 + 2)) & (wx < CX + half_width(yy) + 20 * np.sin(yy / 57.0))
ISLANDS = [(1010, 196, 92, 52), (812, 318, 50, 34), (1182, 318, 58, 40), (850, 812, 90, 52), (1100, 814, 54, 36)]
isl = np.zeros((PH, PW), bool)
for (ix, iy, rx, ry) in ISLANDS:
    if rx == 0: continue
    d = np.sqrt(((xx - ix) / rx) ** 2 + ((yy - iy) / ry) ** 2)
    isl |= d + (N2 - 0.5) * 0.7 + (N3 - 0.5) * 0.3 < 1.0
water = water_ch & ~isl
land = ~water
d_water = ndimage.distance_transform_edt(land).astype(np.float32)     # how far inland
d_land = ndimage.distance_transform_edt(water).astype(np.float32)     # how far from a shore, on the water

def tile_land(c, r):
    cx_, cy_ = centre(c, r)
    return all(not water[min(PH - 1, max(0, cy_ + dy)), min(PW - 1, max(0, cx_ + dx))] for dx in (-14, 0, 14) for dy in (-14, 0, 14))

# the quay's tiles: a paved platform on each side of the deck, over the water
QUAY = {(c, r) for (qa, qb) in QUAYS for c in range(qa // T, qb // T) for r in (BRIDGE_Y // T - 2, BRIDGE_Y // T + 1)}

# ------------------------------------------------------------------ 2. tower spots
def choose_spots(target=120):
    rng_sp = random.Random(5)
    cand = [(c, r) for c in range(W) for r in range(H)
            if (c, r) not in PT and 42 <= path_dist(*centre(c, r)) <= 110
            and ((c, r) in QUAY or (tile_land(c, r) and d_water[centre(c, r)[1], centre(c, r)[0]] >= 10))]
    reach = {t: [i for i, p in enumerate(SAMPLES) if math.hypot(p[0] - centre(*t)[0], p[1] - centre(*t)[1]) <= REACH] for t in cand}
    cover = [0] * len(SAMPLES)
    inner = [i for i, p in enumerate(SAMPLES) if 100 < p[0] < PW - 100]
    spots, side_by_side = [], False
    while len(spots) < target + 6 and (len(spots) < 90 or min(cover[i] for i in inner) < 2):
        best, bs = None, 0
        for t in cand:
            if t in spots: continue
            if not side_by_side and any(abs(t[0] - s[0]) + abs(t[1] - s[1]) == 1 for s in spots): continue
            sc = sum(1.0 / (1 + cover[i]) ** 1.5 for i in reach[t]) * (0.75 + 0.5 * rng_sp.random())
            if sc > bs: best, bs = t, sc
        if not best:
            if side_by_side: break
            side_by_side = True
            continue
        if side_by_side and min(cover[i] for i in inner) >= 2: break
        spots.append(best)
        for i in reach[best]: cover[i] += 1
    return spots, cover, inner

SPOTS, COVER, INNER = choose_spots()
SPOT_SET = set(SPOTS)
QUAY_SPOTS = [s for s in SPOTS if s in QUAY]

# ------------------------------------------------------------------ colours
SAND, SAND_DARK, WET_SAND = (214, 196, 150), (184, 164, 118), (150, 132, 98)
GRASS_A, GRASS_B = (80, 106, 54), (116, 126, 68)           # rugged, olive coastal scrub rather than lush meadow
DRY_GRASS = (154, 144, 82)
ROCK_A, ROCK_B = (66, 62, 58), (122, 114, 104)
SHALLOW, MID, DEEP = (72, 158, 168), (26, 92, 148), (8, 36, 82)

# ------------------------------------------------------------------ 3. painting the ground
tone = np.clip(0.5 + 0.9 * (N1 - 0.5) + 0.55 * (N2 - 0.5), 0, 1)
img = colour_field(GRASS_A, GRASS_B, tone, 0.12)
img = paste_colour(img, colour_field((60, 104, 46), (60, 104, 46), N3, 0.12), np.clip((N2 - 0.62) * 5, 0, 1) * 0.55)    # darker thickets
img = paste_colour(img, colour_field(DRY_GRASS, (170, 168, 92), N3, 0.1), np.clip((0.5 - N1) * 3, 0, 1) * np.clip((110 - d_water) / 60, 0, 1) * 0.8)   # sun-dried grass towards the shore
# the shore: sand only in pockets (where the noise runs high), bare rock and shingle everywhere else
pocket = np.clip((N1 * 0.65 + N2 * 0.35 - 0.52) * 7, 0, 1)
sand_w = (40 + 30 * (N2 - 0.5) * 2) * pocket
sand_m = np.clip((sand_w - d_water) / 9.0, 0, 1) * land
ripple = 0.5 + 0.5 * np.sin((xx * 0.7 + yy * 1.0) / 6.0 + N2 * 14)
sand_col = colour_field(SAND_DARK, SAND, np.clip(0.55 + 0.5 * (N3 - 0.5) + 0.25 * (ripple - 0.5), 0, 1), 0.07)
rock_w = (22 + 26 * (N2 - 0.5) * 2 + 14 * N4) * (1 - pocket * 0.85)
rock_m = np.clip((rock_w - d_water) / 6.0, 0, 1) * land * (1 - sand_m)
rock_col = colour_field(ROCK_A, ROCK_B, np.clip(0.4 + 0.9 * (N3 - 0.5) + 0.7 * (N4 - 0.5), 0, 1), 0.3)
img = paste_colour(img, rock_col, rock_m * 0.92)
img = paste_colour(img, sand_col, sand_m)
wet = np.clip((13 - d_water) / 8.0, 0, 1) * land
img = paste_colour(img, colour_field(WET_SAND, (96, 90, 82), N3, 0.12), wet * 0.8)
# outcrops of bare rock inland, and tufts of dune grass on the sand
outcrop = np.clip((N2 * 0.5 + N3 * 0.5 - 0.66) * 9, 0, 1) * land * np.clip((d_water - 30) / 20, 0, 1)
img = paste_colour(img, colour_field(ROCK_A, ROCK_B, np.clip(0.5 + 0.8 * (N4 - 0.5), 0, 1), 0.25), outcrop * 0.8)
tuft = np.clip((N4 - 0.74) * 8, 0, 1) * np.clip((sand_m - 0.3) * 2, 0, 1) * np.clip((d_water - 16) / 8, 0, 1)
img = paste_colour(img, np.full((PH, PW, 3), 98, np.float32) * np.array([0.8, 1.2, 0.6], np.float32), tuft * 0.8)

# the water: pale shallows, then a clear mid blue, then deep navy; a fine ripple over all of it as seen from above
t_deep = np.clip((d_land - 14) / 200.0, 0, 1)
wcol = colour_field(SHALLOW, MID, np.clip(d_land / 40.0, 0, 1) ** 0.8, 0.05)
wcol = wcol * (1 - t_deep)[:, :, None] + np.array(DEEP, np.float32)[None, None, :] * t_deep[:, :, None] + \
       np.array(MID, np.float32)[None, None, :] * 0
fine = 0.5 + 0.5 * np.sin((xx * 0.55 + yy * 0.9) / 3.1 + N3 * 9 + N4 * 4)          # tight ripples
mid_w = 0.5 + 0.5 * np.sin((xx * 0.4 + yy * 0.8) / 9.0 + N2 * 10 + N3 * 3)           # longer swell
swell2 = 0.5 + 0.5 * np.sin((xx * 0.9 - yy * 0.3) / 16.0 + N1 * 12)
wcol = wcol * (1 + 0.13 * (fine - 0.5) * np.clip(N3 * 2, 0.4, 1) + 0.10 * (mid_w - 0.5) + 0.06 * (swell2 - 0.5))[:, :, None]
crest = np.clip((mid_w * swell2 - 0.55) * 3.5, 0, 1) * np.clip((N3 - 0.4) * 3, 0, 1)
wcol = wcol + np.array([190, 225, 238], np.float32)[None, None, :] * (crest * 0.10)[:, :, None]
glint = np.clip((N4 - 0.9) * 14, 0, 1) * np.clip((N2 - 0.5) * 3, 0, 1)
wcol = wcol + np.array([255, 255, 255], np.float32)[None, None, :] * (glint * 0.3)[:, :, None]
# the sea floor shows through the shallows
wcol = paste_colour(wcol, colour_field((150, 168, 140), (120, 150, 140), N3, 0.1), np.clip((8 - d_land) / 8.0, 0, 1) * 0.5)
img = paste_colour(img, wcol, water.astype(np.float32))
# surf: foam on the waterline, broken by noise, and a few loose lines of breakers further out
foam_line = np.clip((6.5 - d_land) / 3.0, 0, 1) * np.clip((N4 * 0.6 + N3 * 0.6 - 0.35) * 5, 0, 1)
foam_out = np.clip(1.0 - np.abs(d_land - (17 + 5 * np.sin(yy / 23.0 + N2 * 6))) / 1.6, 0, 1) * np.clip((N3 - 0.58) * 7, 0, 1)
foam_edge = np.clip((2.2 - d_water) / 2.2, 0, 1) * land * np.clip((N4 - 0.3) * 3, 0, 1)
foam = np.clip(foam_line + 0.6 * foam_out, 0, 1) * water + foam_edge * 0.8
img = paste_colour(img, np.full((PH, PW, 3), 246.0, np.float32), np.clip(foam, 0, 1) * 0.82)

# ------------------------------------------------------------------ 4. the trodden track (not on the bridge)
def path_draw(d):
    pts = list(PATH_PX)
    pts[0] = (-30, pts[0][1])
    pts[-1] = (PW + 30, pts[-1][1])
    d.line(pts, fill=255, width=40, joint='curve')
    for p in pts[1:-1]:
        d.ellipse((p[0] - 20, p[1] - 20, p[0] + 20, p[1] + 20), fill=255)
pm = poly_mask(path_draw)
pm_soft = soft_mask(pm, 3.0)
on_bridge = np.clip(((xx > DECK_X0 - 90) & (xx < DECK_X1 + 90)).astype(np.float32) * (np.abs(yy - BRIDGE_Y) < 70), 0, 1)
pm_soft = pm_soft * (1 - on_bridge)
edge = np.clip(pm_soft * (1 - pm_soft) * 4, 0, 1)
ragged = np.clip((np.clip(pm_soft + (N3 - 0.5) * 0.5, 0, 1) - 0.45) * 5, 0, 1)
dirt = colour_field((158, 128, 84), (190, 160, 108), np.clip(0.5 + 0.9 * (N2 - 0.5), 0, 1), 0.16)
img = paste_colour(img, np.full((PH, PW, 3), 96.0, np.float32) * np.array([1.0, 0.85, 0.6], np.float32), soft_mask(pm * (1 - on_bridge), 6) * 0.35 * (1 - ragged))
img = paste_colour(img, dirt, ragged * 0.96)
img = paste_colour(img, np.full((PH, PW, 3), 110.0, np.float32) * np.array([1.0, 0.85, 0.6], np.float32), edge * 0.38 * ragged)

# ------------------------------------------------------------------ 5. the tower pads
base = to_img(img).convert('RGBA')
pd_ = ImageDraw.Draw(base)
rpad = random.Random(404)
for (c, r) in SPOTS:   # the tower pads: a ring of packed gravel, a lighter paved centre and a few pebbles
    if (c, r) in QUAY: continue
    cx_, cy_ = centre(c, r)
    pd_.ellipse((cx_ - 21, cy_ - 19, cx_ + 21, cy_ + 21), fill=(60, 56, 48, 70))
    pd_.ellipse((cx_ - 20, cy_ - 20, cx_ + 20, cy_ + 20), fill=(116, 108, 94, 255))
    pd_.ellipse((cx_ - 18, cy_ - 18, cx_ + 18, cy_ + 18), fill=(166, 158, 140, 255))
    pd_.ellipse((cx_ - 13, cy_ - 13, cx_ + 13, cy_ + 13), fill=(186, 178, 160, 255))
    for k in range(7):
        ang = rpad.random() * 6.28; rad = 15 + rpad.random() * 3
        px_, py_ = cx_ + math.cos(ang) * rad, cy_ + math.sin(ang) * rad
        pd_.ellipse((px_ - 2, py_ - 1.5, px_ + 2, py_ + 1.5), fill=(rpad.choice([(128, 120, 106), (196, 188, 172), (146, 138, 122)]) + (255,)))

# ------------------------------------------------------------------ 6. the bridge
def blur_layer(layer, r): return layer.filter(ImageFilter.GaussianBlur(r))
rb = random.Random(808)

def shade(c, f): return tuple(max(0, min(255, int(v * f))) for v in c)

def stone_block_fill(d, x0, y0, x1, y1, base_c, size=(22, 11), seed=0, mortar=(88, 84, 80)):
    """courses of masonry"""
    r = random.Random(seed)
    d.rectangle((x0, y0, x1, y1), fill=mortar + (255,))
    y = y0
    row = 0
    while y < y1:
        h = size[1] + r.randint(-1, 1)
        x = x0 - (size[0] // 2 if row % 2 else 0)
        while x < x1:
            w = size[0] + r.randint(-4, 4)
            c = shade(base_c, 0.86 + 0.28 * r.random())
            d.rectangle((max(x0, x + 1), y + 1, min(x1, x + w - 1), min(y1, y + h - 1)), fill=c + (255,))
            d.line((max(x0, x + 1), y + 1, min(x1, x + w - 1), y + 1), fill=shade(c, 1.18) + (255,))
            x += w
        y += h
        row += 1

def planks(d, x0, y0, x1, y1, seed=0):
    """a timber deck laid across the direction of travel"""
    r = random.Random(seed)
    x = x0
    while x < x1:
        w = r.choice([9, 10, 11, 12])
        c = shade((150, 108, 64), 0.8 + 0.4 * r.random())
        d.rectangle((x, y0, min(x1, x + w - 1), y1), fill=c + (255,))
        d.line((x, y0, x, y1), fill=shade(c, 0.55) + (255,))                      # the gap
        d.line((x + 2, y0, x + 2, y1), fill=shade(c, 1.12) + (255,))               # a lit edge
        for k in range(r.randint(1, 3)):                                          # grain
            gy = r.randint(y0 + 3, y1 - 3)
            d.line((x + 3, gy, min(x1, x + w - 3), gy + r.randint(-1, 1)), fill=shade(c, 0.78) + (255,))
        for ny in (y0 + 5, y1 - 5):                                               # nails
            d.point((x + w // 2, ny), fill=shade(c, 0.45) + (255,))
        x += w

def flagstones(d, x0, y0, x1, y1, seed=0):
    r = random.Random(seed)
    d.rectangle((x0, y0, x1, y1), fill=(94, 90, 86, 255))
    y = y0
    while y < y1:
        h = r.choice([14, 16, 18])
        x = x0
        while x < x1:
            w = r.choice([14, 18, 22, 26])
            c = shade((168, 164, 156), 0.82 + 0.3 * r.random())
            d.rectangle((x + 1, y + 1, min(x1, x + w - 1), min(y1, y + h - 1)), fill=c + (255,))
            d.line((x + 1, y + 1, min(x1, x + w - 1), y + 1), fill=shade(c, 1.15) + (255,))
            x += w
        y += h

bridge = Image.new('RGBA', base.size, (0, 0, 0, 0))
shadow = Image.new('RGBA', base.size, (0, 0, 0, 0))
sd = ImageDraw.Draw(shadow)
bd = ImageDraw.Draw(bridge)
DY0, DY1 = BRIDGE_Y - T, BRIDGE_Y + T          # the deck: two tiles wide
FACE = 26                                       # the visible south wall of the bridge
# shadows and foam on the water first
sd.rectangle((DECK_X0 - 40, DY0 + 8, DECK_X1 + 40, DY1 + FACE + 26), fill=(6, 26, 50, 150))
for qy0, qy1 in ((BRIDGE_Y - 2 * T, DY0), (DY1, DY1 + T)):
    for qa, qb in QUAYS:
        sd.rectangle((qa - 8, qy0 + 10, qb + 8, qy1 + FACE + 30), fill=(6, 26, 50, 150))
shadow = blur_layer(shadow, 7)
base.alpha_composite(shadow)
foam_l = Image.new('RGBA', base.size, (0, 0, 0, 0))
fd = ImageDraw.Draw(foam_l)
piers = list(range(DECK_X0 + 90, DECK_X1 - 60, 120))
for px_ in piers:   # the stone piers: each parts the water with a pointed cutwater
    fd.ellipse((px_ - 38, DY1 + FACE - 6, px_ + 38, DY1 + FACE + 22), fill=(250, 252, 252, 150))
    fd.ellipse((px_ - 30, DY0 - 16, px_ + 30, DY0 + 6), fill=(250, 252, 252, 120))
for qa, qb in QUAYS:
    fd.rectangle((qa - 6, DY1 + T + FACE - 4, qb + 6, DY1 + T + FACE + 12), fill=(250, 252, 252, 120))
    fd.rectangle((qa - 6, BRIDGE_Y - 2 * T - 14, qb + 6, BRIDGE_Y - 2 * T + 2), fill=(250, 252, 252, 110))
foam_l = blur_layer(foam_l, 3.2)
base.alpha_composite(foam_l)

# the south wall with its arches, where the deck stands on the piers
stone_block_fill(bd, DECK_X0 - 20, DY1, DECK_X1 + 20, DY1 + FACE, (140, 136, 128), seed=11)
for px_ in piers:   # an arch between every pair of piers, water showing through
    pass
for a, b in zip([DECK_X0 - 20] + piers, piers + [DECK_X1 + 20]):
    cx_ = (a + b) // 2
    rw = (b - a) // 2 - 22
    bd.pieslice((cx_ - rw, DY1 - 6, cx_ + rw, DY1 + FACE + 14), 0, 180, fill=(12, 56, 96, 255))
    bd.arc((cx_ - rw - 1, DY1 - 7, cx_ + rw + 1, DY1 + FACE + 15), 180, 360, fill=(190, 186, 176, 255), width=2)
    bd.rectangle((cx_ - rw, DY1 - 6, cx_ + rw, DY1 + 1), fill=(0, 0, 0, 0))
# the deck
planks(bd, DECK_X0, DY0, DECK_X1, DY1, seed=3)
# stone aprons at both ends, flaring where the bridge meets the shore
for ax0, ax1, flare in ((DECK_X0 - 70, DECK_X0 + 8, -1), (DECK_X1 - 8, DECK_X1 + 70, 1)):
    bd.polygon([(ax0 if flare < 0 else ax1, DY0 - 14), (ax1 if flare < 0 else ax0, DY0), (ax1 if flare < 0 else ax0, DY1), (ax0 if flare < 0 else ax1, DY1 + 14)], fill=(150, 146, 138, 255))
    flagstones(bd, ax0, DY0 - 2, ax1, DY1 + 2, seed=20 + flare)
# the paved quay on each side, level with the deck
for qy0, qy1, face in ((BRIDGE_Y - 2 * T, DY0, False), (DY1, DY1 + T, True)):
    for qa, qb in QUAYS:
        flagstones(bd, qa, qy0, qb, qy1, seed=30 + qy0 + qa)
        if face:
            stone_block_fill(bd, qa, qy1, qb, qy1 + FACE, (140, 136, 128), seed=41 + qa)
# parapets: a stone wall each side of the deck, wooden posts and a lantern at each pier
def parapet(x0, x1, y, h=10):
    stone_block_fill(bd, x0, y, x1, y + h, (176, 172, 164), size=(18, 8), seed=int(x0 + y), mortar=(100, 96, 90))
    bd.line((x0, y, x1, y), fill=(214, 210, 200, 255), width=2)
gaps = [DECK_X0] + [v for q in QUAYS for v in q] + [DECK_X1]
for (x0, x1) in ((gaps[0], gaps[1]), (gaps[2], gaps[3]), (gaps[4], gaps[5])):
    parapet(x0, x1, DY0 - 8)
    parapet(x0, x1, DY1 - 2)
for qa, qb in QUAYS:
    parapet(qa, qb, BRIDGE_Y - 2 * T - 6)                # the outer edge of each quay
    parapet(qa, qb, DY1 + T - 2)
    for qy in (BRIDGE_Y - 2 * T + 2, DY1 + 4):            # a low wall on the two ends of each quay
        pass
LAMPS = piers + [v for q in QUAYS for v in q] + [DECK_X0 + 6, DECK_X1 - 6]
for px_ in LAMPS:
    for py_ in (DY0 - 10, DY1 + 2):
        bd.rectangle((px_ - 4, py_ - 6, px_ + 4, py_ + 10), fill=(104, 72, 40, 255))
        bd.rectangle((px_ - 4, py_ - 6, px_ + 4, py_ - 4), fill=(142, 104, 62, 255))
        bd.rectangle((px_ - 2, py_ - 14, px_ + 2, py_ - 7), fill=(255, 214, 110, 255))
        bd.rectangle((px_ - 3, py_ - 15, px_ + 3, py_ - 14), fill=(60, 44, 30, 255))
lamp = Image.new('RGBA', base.size, (0, 0, 0, 0))
ld = ImageDraw.Draw(lamp)
for px_ in LAMPS:
    for py_ in (DY0 - 10, DY1 + 2):
        ld.ellipse((px_ - 12, py_ - 22, px_ + 12, py_ + 2), fill=(255, 220, 120, 70))
bridge.alpha_composite(blur_layer(lamp, 4))
for (c, r) in QUAY_SPOTS:
    cx_, cy_ = centre(c, r)
    bd.ellipse((cx_ - 19, cy_ - 19, cx_ + 19, cy_ + 19), fill=(120, 116, 110, 255))
    bd.ellipse((cx_ - 17, cy_ - 17, cx_ + 17, cy_ + 17), fill=(196, 192, 182, 255))
    bd.ellipse((cx_ - 12, cy_ - 12, cx_ + 12, cy_ + 12), outline=(140, 136, 128, 255), width=1)
base.alpha_composite(bridge)

# ------------------------------------------------------------------ 7. sprites
sprites = []
def add(y, fn): sprites.append((y, fn))

def soft_shadow(layer, x, y, rx, ry, a=90):
    sh = Image.new('RGBA', layer.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse((x - rx + 6, y - ry, x + rx + 6, y + ry), fill=(20, 40, 20, a))
    layer.alpha_composite(sh.filter(ImageFilter.GaussianBlur(3.5)))

def palm(x, y, s=1.0):
    def draw(layer):
        soft_shadow(layer, x, y, 18 * s, 6 * s)
        d = ImageDraw.Draw(layer)
        rr = random.Random(int(x * 31 + y * 7))
        lean = rr.choice([-1, 1]) * (8 + rr.random() * 10) * s
        h = (46 + rr.random() * 14) * s
        pts = [(x + lean * (t / 6.0) ** 2 * 1.4, y - h * t / 6.0) for t in range(7)]
        for a, b in zip(pts, pts[1:]):
            d.line([a, b], fill=(112, 84, 52, 255), width=int(5 * s) + 1)
            d.line([(a[0] - 1, a[1]), (b[0] - 1, b[1])], fill=(150, 116, 74, 255), width=2)
        for k, p in enumerate(pts[1:]):   # the trunk's rings
            d.line([(p[0] - 3 * s, p[1]), (p[0] + 3 * s, p[1])], fill=(84, 62, 38, 255), width=1)
        tx, ty = pts[-1]
        for k in range(8):
            ang = k * math.pi / 4 + rr.random() * 0.3
            ln = (24 + rr.random() * 8) * s
            ex, ey = tx + math.cos(ang) * ln, ty + math.sin(ang) * ln * 0.55 + ln * 0.3 * (1 if math.sin(ang) > -0.2 else 0)
            mx, my = tx + math.cos(ang) * ln * 0.5, ty + math.sin(ang) * ln * 0.3 - 6 * s
            col = (46, 122, 52, 255) if k % 2 else (62, 148, 62, 255)
            d.polygon([(tx, ty), (mx - math.sin(ang) * 5 * s, my + math.cos(ang) * 4 * s), (ex, ey), (mx + math.sin(ang) * 5 * s, my - math.cos(ang) * 4 * s)], fill=col)
            d.line([(tx, ty), (ex, ey)], fill=(94, 176, 86, 255), width=1)
        for k in range(3):
            d.ellipse((tx - 4 + k * 3, ty + 1, tx + k * 3, ty + 5), fill=(112, 74, 38, 255))
    add(y, draw)

def tree(x, y, s=1.0):
    def draw(layer):
        soft_shadow(layer, x, y, 22 * s, 8 * s)
        d = ImageDraw.Draw(layer)
        rr = random.Random(int(x * 13 + y * 5))
        d.rectangle((x - 3 * s, y - 16 * s, x + 3 * s, y + 2 * s), fill=(84, 60, 38, 255))
        for k in range(9):
            cx_ = x + (rr.random() - 0.5) * 26 * s
            cy_ = y - (26 + rr.random() * 22) * s
            r_ = (11 + rr.random() * 7) * s
            c = tuple(int(v) for v in (shade((54, 112, 48), 0.8 + 0.3 * rr.random())))
            d.ellipse((cx_ - r_, cy_ - r_, cx_ + r_, cy_ + r_), fill=c + (255,))
        for k in range(6):
            cx_ = x - 6 * s + (rr.random() - 0.5) * 16 * s
            cy_ = y - (36 + rr.random() * 14) * s
            r_ = (5 + rr.random() * 4) * s
            d.ellipse((cx_ - r_, cy_ - r_, cx_ + r_, cy_ + r_), fill=(112, 168, 70, 255))
    add(y, draw)

def bush(x, y, s=1.0, flowers=False):
    def draw(layer):
        soft_shadow(layer, x, y, 14 * s, 5 * s, 70)
        d = ImageDraw.Draw(layer)
        rr = random.Random(int(x * 3 + y * 11))
        for k in range(6):
            cx_ = x + (rr.random() - 0.5) * 18 * s
            cy_ = y - (4 + rr.random() * 8) * s
            r_ = (6 + rr.random() * 4) * s
            d.ellipse((cx_ - r_, cy_ - r_, cx_ + r_, cy_ + r_), fill=shade((60, 124, 54), 0.8 + 0.4 * rr.random()) + (255,))
        if flowers:
            for k in range(5):
                fx, fy = x + (rr.random() - 0.5) * 18 * s, y - 4 - rr.random() * 10 * s
                d.ellipse((fx, fy, fx + 3, fy + 3), fill=rr.choice([(250, 240, 120, 255), (250, 250, 250, 255), (240, 150, 170, 255)]))
    add(y, draw)

def rock(x, y, s=1.0, wet=False):
    def draw(layer):
        soft_shadow(layer, x, y, 15 * s, 5 * s, 80)
        d = ImageDraw.Draw(layer)
        d.ellipse((x - 14 * s, y - 8 * s, x + 14 * s, y + 7 * s), fill=(70, 68, 66, 255))
        d.ellipse((x - 13 * s, y - 11 * s, x + 12 * s, y + 4 * s), fill=(138, 134, 128, 255) if not wet else (96, 100, 100, 255))
        d.ellipse((x - 9 * s, y - 11 * s, x + 3 * s, y - 4 * s), fill=(178, 174, 166, 255))
    add(y, draw)

def reeds(x, y, n=6):
    def draw(layer):
        d = ImageDraw.Draw(layer)
        rr = random.Random(int(x * 17 + y * 3))
        for k in range(n):
            bx = x + (rr.random() - 0.5) * 18
            h = 12 + rr.random() * 14
            d.line([(bx, y), (bx + (rr.random() - 0.5) * 6, y - h)], fill=rr.choice([(150, 156, 82, 255), (118, 138, 70, 255), (176, 164, 98, 255)]), width=1)
    add(y, draw)

def driftwood(x, y):
    def draw(layer):
        d = ImageDraw.Draw(layer)
        rr = random.Random(int(x + y))
        a = rr.random() * 3.14
        ln = 10 + rr.random() * 10
        d.line([(x - math.cos(a) * ln, y - math.sin(a) * ln * 0.4), (x + math.cos(a) * ln, y + math.sin(a) * ln * 0.4)], fill=(150, 130, 104, 255), width=3)
        d.line([(x - math.cos(a) * ln, y - math.sin(a) * ln * 0.4 - 1), (x + math.cos(a) * ln, y + math.sin(a) * ln * 0.4 - 1)], fill=(196, 178, 148, 255), width=1)
    add(y, draw)

def shell(x, y):
    def draw(layer):
        d = ImageDraw.Draw(layer)
        d.ellipse((x - 2, y - 1, x + 2, y + 2), fill=(250, 226, 214, 255))
    add(y, draw)

def lighthouse(x, y):
    def draw(layer):
        soft_shadow(layer, x + 6, y, 34, 11, 110)
        d = ImageDraw.Draw(layer)
        d.ellipse((x - 26, y - 12, x + 26, y + 10), fill=(120, 116, 110, 255))                       # the rock it stands on
        d.ellipse((x - 24, y - 15, x + 22, y + 4), fill=(160, 156, 148, 255))
        top = y - 112
        for k, (c1, c2) in enumerate([((246, 246, 244), (210, 210, 206)), ((200, 60, 52), (160, 44, 40)), ((246, 246, 244), (210, 210, 206)), ((200, 60, 52), (160, 44, 40))]):
            y0 = y - 12 - k * 22
            w0, w1 = 17 - k * 1.8, 15.2 - k * 1.8
            d.polygon([(x - w0, y0), (x + w0, y0), (x + w1, y0 - 22), (x - w1, y0 - 22)], fill=c1 + (255,))
            d.polygon([(x + w0 * 0.25, y0), (x + w0, y0), (x + w1, y0 - 22), (x + w1 * 0.25, y0 - 22)], fill=c2 + (255,))
        d.rectangle((x - 12, top - 8, x + 12, top + 2), fill=(60, 60, 64, 255))                      # the gallery
        d.rectangle((x - 8, top - 22, x + 8, top - 8), fill=(255, 232, 140, 255))                    # the lamp room
        d.rectangle((x - 8, top - 22, x + 8, top - 20), fill=(220, 190, 90, 255))
        for mx in (x - 8, x - 2, x + 4):
            d.line([(mx, top - 22), (mx, top - 8)], fill=(70, 70, 72, 255), width=1)
        d.polygon([(x - 11, top - 22), (x + 11, top - 22), (x, top - 36)], fill=(60, 60, 64, 255))
        d.ellipse((x - 2, top - 40, x + 2, top - 36), fill=(200, 60, 52, 255))
        d.rectangle((x - 4, y - 20, x + 3, y - 12), fill=(80, 56, 36, 255))                           # the door
    add(y, draw)

def rowboat(x, y, ang=0.0):
    def draw(layer):
        sh = Image.new('RGBA', layer.size, (0, 0, 0, 0))
        ImageDraw.Draw(sh).ellipse((x - 24, y - 4, x + 28, y + 14), fill=(0, 30, 60, 110))
        layer.alpha_composite(sh.filter(ImageFilter.GaussianBlur(3)))
        d = ImageDraw.Draw(layer)
        d.pieslice((x - 26, y - 14, x + 26, y + 12), 0, 180, fill=(140, 92, 52, 255))
        d.ellipse((x - 26, y - 14, x + 26, y + 2), fill=(176, 122, 72, 255))
        d.ellipse((x - 21, y - 11, x + 21, y - 1), fill=(96, 64, 38, 255))
        d.line([(x - 8, y - 6), (x + 8, y - 6)], fill=(170, 130, 86, 255), width=2)
        d.line([(x - 4, y - 8), (x - 22, y - 22)], fill=(200, 170, 120, 255), width=2)
        d.line([(x + 4, y - 8), (x + 22, y - 22)], fill=(200, 170, 120, 255), width=2)
        d.ellipse((x - 26, y - 14, x + 26, y + 2), outline=(236, 214, 170, 255), width=1)
    add(y, draw)

def buoy(x, y):
    def draw(layer):
        d = ImageDraw.Draw(layer)
        sh = Image.new('RGBA', layer.size, (0, 0, 0, 0))
        ImageDraw.Draw(sh).ellipse((x - 8, y, x + 10, y + 7), fill=(0, 30, 60, 100))
        layer.alpha_composite(sh.filter(ImageFilter.GaussianBlur(2)))
        d.polygon([(x - 6, y), (x + 6, y), (x + 4, y - 14), (x - 4, y - 14)], fill=(210, 52, 44, 255))
        d.rectangle((x - 5, y - 8, x + 5, y - 5), fill=(250, 250, 250, 255))
        d.ellipse((x - 4, y - 18, x + 4, y - 12), fill=(250, 250, 250, 255))
    add(y, draw)

# the little islands: a skirt of sand under a green top, with palms, rocks and shrubs
for (ix, iy, rx, ry) in ISLANDS:
    if rx == 0: continue
    r_ = random.Random(ix * 7 + iy)
    if rx >= 90:
        for k in range(3): palm(ix + r_.randint(-rx // 2, rx // 2), iy + r_.randint(-ry // 3, ry // 4), 0.9 + r_.random() * 0.3)
        for k in range(3): rock(ix + r_.randint(-rx // 2, rx // 2), iy + r_.randint(-ry // 4, ry // 3), 0.6 + r_.random() * 0.5)
        for k in range(3): bush(ix + r_.randint(-rx // 2, rx // 2), iy + r_.randint(-ry // 4, ry // 3), 0.8, True)
    elif rx >= 50:
        palm(ix + r_.randint(-rx // 3, rx // 3), iy, 0.85)
        rock(ix + r_.randint(-rx // 2, rx // 2), iy + ry // 4, 0.55)
        bush(ix + r_.randint(-rx // 3, rx // 3), iy + ry // 5, 0.7, True)
    else:
        palm(ix, iy, 0.75)
        rock(ix + 12, iy + 8, 0.5)
rowboat(1100, 252)
rowboat(700, 800)
buoy(690, 480)
buoy(1250, 700)
buoy(1210, 440)

occupied = set(PT) | SPOT_SET
free = [(c, r) for c in range(W) for r in range(H) if (c, r) not in occupied and (c, r) not in QUAY and tile_land(c, r)]
rr = random.Random(77)
LIGHT = None
for (c, r) in free:
    x, y = centre(c, r)
    x += rr.randint(-10, 10); y += rr.randint(-8, 8)
    dw = d_water[min(PH - 1, max(0, y)), min(PW - 1, max(0, x))]
    pd = path_dist(x, y)
    if pd < 44: continue
    if abs(y - BRIDGE_Y) < 100 and DECK_X0 - 90 < x < DECK_X1 + 90: continue
    roll = rr.random()
    if dw < 20:       # the waterline: rocks, reeds, driftwood, shells
        if roll < 0.12: rock(x, y + 4, 0.6 + rr.random() * 0.7, wet=True)
        elif roll < 0.26: reeds(x, y + 6, 5 + rr.randint(0, 4))
        elif roll < 0.32: driftwood(x, y + 4)
        elif roll < 0.38: shell(x, y)
    elif dw < 70:     # the beach and the dunes: palms, dune grass, a few rocks
        if pd >= 66 and roll < 0.18: palm(x, y + 10, 0.85 + rr.random() * 0.4)
        elif roll < 0.26: bush(x, y + 6, 0.6 + rr.random() * 0.4)
        elif roll < 0.32: rock(x, y + 5, 0.6 + rr.random() * 0.6)
        elif roll < 0.40: reeds(x, y + 6, 4)
    else:             # inland: broadleaf trees, thickets, flowering shrubs
        if pd >= 70 and roll < 0.34: tree(x, y + 12, 0.85 + rr.random() * 0.5)
        elif roll < 0.46: bush(x, y + 6, 0.7 + rr.random() * 0.6, rr.random() < 0.4)
        elif roll < 0.52: rock(x, y + 6, 0.6 + rr.random() * 0.8)
for (c, r) in SPOTS:
    if (c, r) in QUAY: continue
    x, y = centre(c, r)
    if rr.random() < 0.3: rock(x + rr.choice([-16, 16]), y + 16, 0.4)
    if rr.random() < 0.3: reeds(x + rr.choice([-17, 17]), y + 18, 3)
# the lighthouse on the south-west headland: the free land tile nearest to (470, 790) that is clear of everything
best = None
for (c, r) in free:
    x, y = centre(c, r)
    if path_dist(x, y) > 120 and d_water[y, x] > 36 and not any(abs(c - s[0]) < 2 and abs(r - s[1]) < 2 for s in SPOTS):
        dd = math.hypot(x - 540, y - 800)
        if best is None or dd < best[0]: best = (dd, x, y)
if best:
    lighthouse(best[1], best[2] + 6)
    LIGHT = (best[1], best[2])

layer = Image.new('RGBA', base.size, (0, 0, 0, 0))
for _, fn in sorted(sprites, key=lambda s: s[0]):
    fn(layer)
layer = layer.filter(ImageFilter.GaussianBlur(0.5))
base.alpha_composite(layer)

# ------------------------------------------------------------------ 8. the light: a warm afternoon, a soft vignette
arr = np.asarray(base.convert('RGB'), np.float32)
arr = arr * np.array([1.03, 1.0, 0.96], np.float32)[None, None, :]
vig = np.clip((((xx - PW / 2) / (PW / 2)) ** 2 + ((yy - PH / 2) / (PH / 2)) ** 2) ** 1.4 - 0.45, 0, 1) * 0.22
arr = arr * (1 - vig[:, :, None])
final = to_img(arr)
os.makedirs(os.path.join(OUT, 'data'), exist_ok=True)
final.save(os.path.join(OUT, 'data', 'map.png'))
if len(sys.argv) > 2:
    final.save(sys.argv[2])

# ------------------------------------------------------------------ data files
def grid_text(fn):
    return '_'.join('#'.join(fn(c, r) for r in range(H)) for c in range(W))
def kind(c, r):
    if (c, r) in PT: return '1,0'
    if (c, r) in SPOT_SET: return '-1,-1'
    return '3,0'
blank = grid_text(lambda c, r: '-1,-1')
START_GOLD = 200
with open(os.path.join(OUT, 'maptree.xml'), 'w') as f:
    f.write("<?xml version='1.0'?>\n  <maptree>\n    <levels>\n")
    f.write("      <level id='0'>%s</level>\n      <level id='1'>%s</level>\n      <level id='2'>%s</level>\n" % (blank, blank, grid_text(kind)))
    f.write("    </levels>\n    <mapsize w='%d' h='%d'></mapsize>\n    <tilesize s='%d'></tilesize>\n    <game lifes=\"10\" startmoney=\"%d\"></game>\n  </maptree>\n" % (W, H, T, START_GOLD))
with open(os.path.join(OUT, 'waypoints.xml'), 'w') as f:
    f.write("<?xml version='1.0'?>\n  <pathfinding>\n    <waypoints>\n")
    for i, (x, y) in enumerate(PATH_PX):
        f.write("      <waypoint id='%d'>%d,%d,2</waypoint>\n" % (i, x, y))
    f.write("    </waypoints>\n  </pathfinding>\n")
open(os.path.join(OUT, 'towers.xml'), 'w').write(open(os.path.join(DATA, 'maps', 'bigger.xml', 'towers.xml')).read())   # the same tower set as Bigger
# the icon in the map list: the bridge and the water around it
x0 = max(0, min(PW - 360, 960 - 180)); y0 = max(0, min(PH - 360, BRIDGE_Y - 180))
final.crop((x0, y0, x0 + 360, y0 + 360)).resize((50, 50), Image.LANCZOS).save(os.path.join(OUT, 'map_logo.png'))

length = sum(math.dist(a_, b_) for a_, b_ in zip(PATH_PX, PATH_PX[1:]))
mincover = min(COVER[i] for i in INNER)
low = [(int(SAMPLES[i][0]), int(SAMPLES[i][1]), COVER[i]) for i in INNER if COVER[i] < 2]
print('weak stretches (x, y, spots in reach):', low[:14])
print('trail %d px (about %d tiles, %d way points), %d spots (%d on the quay) of %d tiles, every stretch (bar the ends) in reach of at least %d spots'
      % (length, length // T, len(PATH_PX), len(SPOTS), len(QUAY_SPOTS), W * H, mincover))
print('lighthouse at', LIGHT)
