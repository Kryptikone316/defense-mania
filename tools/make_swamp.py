#!/usr/bin/env python3
"""Draws "The Swamp", a standard-size (20 x 15 tiles of 48 px) Defense Mania map, and writes its data files
into <data>/maps/swamp.xml/ . The picture is painted here from the game's own grass texture (recoloured and
hazed to a swamp look) plus procedural water, mud, trees and reeds, so the map is original artwork.
Usage: make_swamp.py <the game's data folder> [preview.png]"""
import math, os, random, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageChops

DATA = sys.argv[1]
OUT = os.path.join(DATA, 'maps', 'swamp.xml')
T, W, H = 48, 20, 15
PW, PH = W * T, H * T
rng = np.random.default_rng(1203)
random.seed(1203)

# ------------------------------------------------------------------ the layout
# The way points, in pixels. Enemies walk straight from one to the next, so the track is a crooked, kinked trail with
# one curl in the middle that doubles back on itself: a place to wander and get lost in. It enters on the left edge and
# leaves on the right one.
PATH_PX = [(24, 120), (130, 96), (215, 170), (310, 140), (395, 205), (365, 300), (270, 345), (175, 315), (110, 395),
           (150, 495), (250, 535), (355, 505), (430, 420), (525, 455), (560, 555), (480, 640), (590, 668), (700, 610),
           (715, 505), (640, 405), (720, 320), (640, 235), (545, 255), (520, 160), (610, 95), (720, 120), (800, 200),
           (860, 300), (830, 410), (880, 520), (936, 600)]

def seg_point_dist(px_, py_, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    l2 = dx * dx + dy * dy
    t = 0 if l2 == 0 else max(0, min(1, ((px_ - a[0]) * dx + (py_ - a[1]) * dy) / l2))
    return math.hypot(px_ - (a[0] + t * dx), py_ - (a[1] + t * dy))

def path_dist(x, y):
    return min(seg_point_dist(x, y, a, b) for a, b in zip(PATH_PX, PATH_PX[1:]))

def centre(c, r): return (c * T + T // 2, r * T + T // 2)

# tiles the trail covers: nothing can be built there
PT = {(c, r) for c in range(W) for r in range(H) if path_dist(*centre(c, r)) <= 41}

# points along the trail, for checking that every stretch of it can be reached by some towers
SAMPLES = []
for a, b in zip(PATH_PX, PATH_PX[1:]):
    n = int(math.hypot(b[0] - a[0], b[1] - a[1]) // 16)
    for i in range(n):
        SAMPLES.append((a[0] + (b[0] - a[0]) * i / n, a[1] + (b[1] - a[1]) * i / n))
REACH = 1.9 * T     # an arrow tower's real reach

def choose_spots(target=72):
    """tower spots: scarce, scattered along both sides of the trail and clustered at the kinks, and every stretch of
    the trail (bar its ends) stays within reach of at least two of them"""
    rng_sp = random.Random(5)
    cand = [(c, r) for c in range(W) for r in range(H) if (c, r) not in PT and 42 <= path_dist(*centre(c, r)) <= 110]
    reach = {t: [i for i, p in enumerate(SAMPLES) if math.hypot(p[0] - centre(*t)[0], p[1] - centre(*t)[1]) <= REACH] for t in cand}
    cover = [0] * len(SAMPLES)
    inner = [i for i, p in enumerate(SAMPLES) if 100 < p[0] < PW - 100]
    spots = []
    side_by_side = False
    while len(spots) < target + 6 and (len(spots) < 52 or min(cover[i] for i in inner) < 2):
        best, bs = None, 0
        for t in cand:
            if t in spots:
                continue
            if not side_by_side and any(abs(t[0] - s[0]) + abs(t[1] - s[1]) == 1 for s in spots):   # no two side by side, so no solid lines
                continue
            sc = sum(1.0 / (1 + cover[i]) ** 1.5 for i in reach[t]) * (0.75 + 0.5 * rng_sp.random())
            if sc > bs:
                best, bs = t, sc
        if not best:
            if side_by_side:
                break
            side_by_side = True   # only to patch a weak stretch
            continue
        if side_by_side and min(cover[i] for i in inner) >= 2:
            break
        spots.append(best)
        for i in reach[best]:
            cover[i] += 1
    return spots, cover, inner

SPOTS, COVER, INNER = choose_spots()
SPOT_SET = set(SPOTS)

# ------------------------------------------------------------------ colours and textures
def lerp(a, b, t): return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))

GRASS_LIGHT, GRASS_DARK = (106, 108, 62), (64, 74, 44)
MARSH = (62, 74, 44)
MUD_LIGHT, MUD_DARK = (166, 140, 92), (112, 92, 60)
WATER_DEEP, WATER_MID, WATER_LIGHT = (52, 60, 46), (82, 90, 68), (146, 150, 112)
HAZE = (214, 204, 150)

ts = Image.open(os.path.join(DATA, 'maps', 'woods.xml', 'data', 'tileset.png')).convert('RGB')
grass_src = ts.crop((0, 0, T, T))
luma = np.asarray(grass_src.convert('L'), dtype=np.float32) / 255.0
luma = (luma - luma.mean()) / (luma.std() + 1e-6)   # detail only

def noise(scale, seed=0):
    r = np.random.default_rng(seed)
    n = r.random((PH // scale + 3, PW // scale + 3)).astype(np.float32)
    im = Image.fromarray((n * 255).astype(np.uint8)).resize((PW + 3 * scale, PH + 3 * scale), Image.BICUBIC)
    return np.asarray(im, dtype=np.float32)[:PH, :PW] / 255.0

def detail_field():
    """the game's grass detail, tiled with random flips, covering the whole map"""
    out = np.zeros((PH, PW), np.float32)
    for c in range(W):
        for r in range(H):
            t = luma
            if rng.random() < .5: t = t[:, ::-1]
            if rng.random() < .5: t = t[::-1, :]
            if rng.random() < .5: t = t.T
            out[r * T:(r + 1) * T, c * T:(c + 1) * T] = t
    return out

DETAIL = detail_field()
N1, N2, N3 = noise(160, 1), noise(60, 2), noise(24, 3)

def colour_field(c0, c1, mix, detail_amt=0.10):
    c0, c1 = np.array(c0, np.float32), np.array(c1, np.float32)
    f = c0[None, None, :] + (c1 - c0)[None, None, :] * mix[:, :, None]
    f = f * (1 + detail_amt * DETAIL[:, :, None])
    return f

def to_img(arr): return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))

def soft_mask(m, blur):
    return np.asarray(Image.fromarray((np.clip(m, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(blur)), np.float32) / 255.0

def paste_colour(base, colour_arr, mask):
    return base * (1 - mask[:, :, None]) + colour_arr * mask[:, :, None]

def poly_mask(draw_fn, blur=0):
    im = Image.new('L', (PW, PH), 0)
    draw_fn(ImageDraw.Draw(im))
    m = np.asarray(im, np.float32) / 255.0
    return soft_mask(m, blur) if blur else m

# ------------------------------------------------------------------ 1. ground
mix = np.clip(0.35 + 0.9 * (N1 - 0.5) + 0.5 * (N2 - 0.5), 0, 1)
img = colour_field(GRASS_LIGHT, GRASS_DARK, mix, 0.16)

# darker marsh and reed beds
marsh_m = np.clip((N2 * 0.6 + N3 * 0.4 - 0.46) * 6, 0, 1) * np.clip((N1 - 0.25) * 4, 0, 1)
img = paste_colour(img, colour_field(MARSH, (48, 58, 36), N3, 0.2), marsh_m * 0.75)

# ------------------------------------------------------------------ 2. water: pools away from the path
def open_ground():
    pts = []
    for y in range(24, PH, 24):
        for x in range(24, PW, 24):
            d = path_dist(x, y)
            if d > 76:
                pts.append((d, x, y))
    pts.sort(reverse=True)
    chosen = []
    for d, x, y in pts:
        if all(math.hypot(x - cx, y - cy) > 118 for _, cx, cy, _ in chosen):
            chosen.append((d, x, y, d))
        if len(chosen) >= 13:
            break
    return chosen
POOLS = []   # (centre tile x, y as floats, radius in tiles x, y)
_rp = random.Random(9)
for d, x, y, _ in open_ground():
    rx = max(0.6, (d - 62) / T * (1.0 + 0.5 * _rp.random()))
    ry = max(0.5, (d - 62) / T * (0.7 + 0.4 * _rp.random()))
    POOLS.append((x / T, y / T, rx, ry))
water = np.zeros((PH, PW), np.float32)
yy, xx = np.mgrid[0:PH, 0:PW].astype(np.float32)
for cx, cy, rx, ry in POOLS:
    d = np.sqrt(((xx - cx * T) / (rx * T)) ** 2 + ((yy - cy * T) / (ry * T)) ** 2)
    wob = (N2 - 0.5) * 0.55 + (N3 - 0.5) * 0.25
    water = np.maximum(water, np.clip((1.0 + wob - d) * 5, 0, 1))
# keep water off the path and the tower spots
keep = np.zeros((PH, PW), np.float32)
for (c, r) in list(PT) + SPOTS:
    keep[r * T:(r + 1) * T, c * T:(c + 1) * T] = 1
_pi = Image.new('L', (PW, PH), 0)
_pd = ImageDraw.Draw(_pi)
_pd.line(PATH_PX, fill=255, width=92, joint='curve')
keep = np.maximum(keep, np.asarray(_pi, np.float32) / 255.0)
keep = soft_mask(keep, 5)
water = np.clip(water - keep * 1.2, 0, 1)
shore = soft_mask(np.clip(water * 3, 0, 1), 5)
shore = np.clip(shore - water * 0.9, 0, 1)
img = paste_colour(img, colour_field((88, 78, 52), (70, 62, 42), N3, 0.12), np.clip(shore * 1.6, 0, 1) * 0.85)
wcol = colour_field(WATER_DEEP, WATER_MID, np.clip(0.45 + 0.8 * (N2 - 0.5), 0, 1), 0.05)
# a pale sheen and soft ripples
ripple = np.clip(np.sin(yy * 0.9 + N3 * 14.0) * np.sin(xx * 0.05 + N2 * 9.0) - 0.82, 0, 1) * 5
sheen = np.clip(0.55 * (1 - yy / PH) + 0.9 * ripple, 0, 1)
wcol = wcol + np.array(WATER_LIGHT, np.float32)[None, None, :] * (sheen[:, :, None] * 0.30)
img = paste_colour(img, wcol, soft_mask(water, 1.2))

# ------------------------------------------------------------------ 3. the mud track
def path_draw(d):
    pts = list(PATH_PX)
    pts[0] = (-30, pts[0][1])
    pts[-1] = (PW + 30, pts[-1][1])
    d.line(pts, fill=255, width=40, joint='curve')
    for p in pts[1:-1]:
        d.ellipse((p[0] - 20, p[1] - 20, p[0] + 20, p[1] + 20), fill=255)
pm = poly_mask(path_draw)
pm_soft = soft_mask(pm, 3.2)
edge = np.clip(pm_soft * (1 - pm_soft) * 4, 0, 1)
ragged = np.clip(pm_soft + (N3 - 0.5) * 0.5, 0, 1)
ragged = np.clip((ragged - 0.45) * 5, 0, 1)
mud = colour_field(MUD_DARK, MUD_LIGHT, np.clip(0.55 + 0.9 * (N2 - 0.5), 0, 1), 0.2)
img = paste_colour(img, colour_field((60, 52, 34), (60, 52, 34), N3, 0.1), soft_mask(pm, 7) * 0.55 * (1 - ragged))   # damp rim
img = paste_colour(img, mud, ragged * 0.95)
img = paste_colour(img, np.array([84, 70, 46], np.float32)[None, None, :] * np.ones((PH, PW, 1), np.float32), edge * 0.45 * ragged)

# ------------------------------------------------------------------ 4. drier mounds where towers can go
mounds = np.zeros((PH, PW), np.float32)
for (c, r) in SPOTS:
    cx, cy = centre(c, r)
    d = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
    mounds = np.maximum(mounds, np.clip((19 - d) / 5.0 + (N3 - 0.5) * 0.6, 0, 1))
img = paste_colour(img, colour_field((152, 140, 92), (128, 116, 76), N3, 0.2), mounds * 0.72)

# ------------------------------------------------------------------ 5. sprites: trees, reeds, rocks, lily pads
base = to_img(img).convert('RGBA')
sprites = []   # (y for ordering, draw function)

def blob(d, x, y, r, col, n=1):
    d.ellipse((x - r, y - r, x + r, y + r), fill=col)

def tree(x, y, s=1.0):
    def draw(layer):
        d = ImageDraw.Draw(layer)
        # shadow
        sh = Image.new('RGBA', layer.size, (0, 0, 0, 0))
        ImageDraw.Draw(sh).ellipse((x - 26 * s + 8, y - 6 * s, x + 26 * s + 8, y + 14 * s), fill=(20, 24, 12, 110))
        layer.alpha_composite(sh.filter(ImageFilter.GaussianBlur(4)))
        # trunk and roots
        d.polygon([(x - 6 * s, y - 24 * s), (x + 6 * s, y - 24 * s), (x + 11 * s, y + 4 * s), (x - 11 * s, y + 4 * s)], fill=(84, 62, 44, 255))
        d.polygon([(x - 11 * s, y + 4 * s), (x - 20 * s, y + 9 * s), (x - 9 * s, y + 1 * s)], fill=(74, 54, 38, 255))
        d.polygon([(x + 11 * s, y + 4 * s), (x + 21 * s, y + 9 * s), (x + 9 * s, y + 1 * s)], fill=(74, 54, 38, 255))
        # canopy: overlapping blobs, lighter on the upper left
        rr = random.Random(int(x * 31 + y * 17))
        for k in range(16):
            ang = rr.random() * math.tau
            dist = rr.random() * 18 * s
            bx, by = x + math.cos(ang) * dist, y - 38 * s + math.sin(ang) * dist * 0.75
            rad = (10 + rr.random() * 8) * s
            t = 0.5 + 0.5 * (-(bx - x) - (by - (y - 38 * s))) / (26 * s)
            col = tuple(int(v) for v in lerp((52, 66, 36), (118, 126, 62), max(0, min(1, t))))
            d.ellipse((bx - rad, by - rad, bx + rad, by + rad), fill=col + (255,))
        # hanging moss
        for k in range(14):
            mx = x + (rr.random() - 0.5) * 40 * s
            my = y - 30 * s + rr.random() * 12 * s
            d.line([(mx, my), (mx + (rr.random() - 0.5) * 3, my + (8 + rr.random() * 12) * s)], fill=(150, 156, 98, 200), width=2)
    sprites.append((y, draw))

def reeds(x, y, n=7):
    def draw(layer):
        d = ImageDraw.Draw(layer)
        rr = random.Random(int(x * 13 + y * 7))
        for k in range(n):
            bx = x + (rr.random() - 0.5) * 22
            h = 16 + rr.random() * 18
            lean = (rr.random() - 0.5) * 9
            col = rr.choice([(168, 146, 74, 255), (140, 130, 66, 255), (184, 120, 50, 255), (104, 112, 60, 255), (126, 122, 66, 255)])
            d.line([(bx, y), (bx + lean, y - h)], fill=col, width=2)
            if rr.random() < 0.3:
                d.ellipse((bx + lean - 2, y - h - 6, bx + lean + 2, y - h + 1), fill=(96, 62, 36, 255))
    sprites.append((y, draw))

def rock(x, y, s=1.0):
    def draw(layer):
        d = ImageDraw.Draw(layer)
        d.ellipse((x - 14 * s, y - 8 * s, x + 14 * s, y + 8 * s), fill=(60, 58, 46, 255))
        d.ellipse((x - 12 * s, y - 10 * s, x + 11 * s, y + 5 * s), fill=(122, 118, 98, 255))
        d.ellipse((x - 8 * s, y - 9 * s, x + 2 * s, y - 2 * s), fill=(150, 146, 122, 255))
    sprites.append((y, draw))

def lily(x, y):
    def draw(layer):
        d = ImageDraw.Draw(layer)
        d.ellipse((x - 8, y - 5, x + 8, y + 5), fill=(90, 120, 54, 255))
        d.polygon([(x, y), (x + 9, y - 2), (x + 9, y + 2)], fill=(74, 82, 52, 255))
    sprites.append((y - 40, draw))

# place decor on tiles that are neither path nor spot
occupied = set(PT) | SPOT_SET
free = [(c, r) for c in range(W) for r in range(H) if (c, r) not in occupied]
wat_t = [(c, r) for (c, r) in free if water[r * T + T // 2, c * T + T // 2] > 0.5]
land_t = [(c, r) for (c, r) in free if (c, r) not in wat_t]
pathdist = {t: path_dist(*centre(*t)) for t in land_t}
rr = random.Random(77)
for (c, r) in land_t:
    x, y = centre(c, r)
    x += rr.randint(-8, 8); y += rr.randint(-6, 8)
    far = pathdist[(c, r)]
    roll = rr.random()
    if far >= 76 and roll < 0.7:
        tree(x, y + 14, 0.9 + rr.random() * 0.45)
    elif far >= 48 and roll < 0.86:
        reeds(x, y + 10, 5 + rr.randint(0, 5))
    elif roll < 0.9:
        rock(x, y + 6, 0.7 + rr.random() * 0.5)
for (c, r) in wat_t:
    x, y = centre(c, r)
    if rr.random() < 0.55: lily(x + rr.randint(-14, 14), y + rr.randint(-12, 12))
    if rr.random() < 0.2: reeds(x + rr.randint(-14, 14), y + 14, 6)
# a few reed tufts and tiny rocks on the spots, so they do not look bare
for (c, r) in SPOTS:
    x, y = centre(c, r)
    if rr.random() < 0.5: reeds(x + rr.choice([-15, 15]), y + 18, 3)
    if rr.random() < 0.3: rock(x + rr.choice([-14, 14]), y + 16, 0.45)
# along the path: the odd reed clump on the verge
for i, (sx, sy) in enumerate(SAMPLES):
    if i % 6 == 0 and rr.random() < 0.5:
        reeds(sx + rr.choice([-27, 27]), sy + rr.choice([-8, 14]), 4)

layer = Image.new('RGBA', base.size, (0, 0, 0, 0))
for _, fn in sorted(sprites, key=lambda s: s[0]):
    fn(layer)
layer = layer.filter(ImageFilter.GaussianBlur(0.7))
la = np.asarray(layer, np.float32)
la[:, :, :3] = np.clip(la[:, :, :3] * (1 + 0.10 * DETAIL[:, :, None]) * np.array([0.97, 0.97, 0.9], np.float32)[None, None, :], 0, 255)
layer = Image.fromarray(la.astype(np.uint8), 'RGBA')
base.alpha_composite(layer)

# ------------------------------------------------------------------ 6. haze, light, vignette
arr = np.asarray(base.convert('RGB'), np.float32)
mist = soft_mask(np.clip(water * 2.2, 0, 1), 18) * (0.55 + 0.45 * N2) * 0.40
haze = (0.26 * (1 - yy / PH) ** 1.3 + 0.10 + mist)[:, :, None]
rays = np.zeros((PH, PW), np.float32)
for k, (rx0, w) in enumerate([(210, 70), (470, 110), (760, 80)]):
    line = (xx - rx0 - yy * 0.35)
    rays = np.maximum(rays, np.exp(-(line / w) ** 2) * (1 - yy / PH))
arr = arr * (1 - haze) + np.array(HAZE, np.float32)[None, None, :] * haze
arr = arr + rays[:, :, None] * np.array([28, 26, 14], np.float32)[None, None, :]
vig = 1 - 0.34 * (((xx - PW / 2) / (PW / 2)) ** 2 + ((yy - PH / 2) / (PH / 2)) ** 2) ** 1.3
arr = arr * vig[:, :, None]
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
with open(os.path.join(OUT, 'maptree.xml'), 'w') as f:
    f.write("<?xml version='1.0'?>\n  <maptree>\n    <levels>\n")
    f.write("      <level id='0'>%s</level>\n      <level id='1'>%s</level>\n      <level id='2'>%s</level>\n" % (blank, blank, grid_text(kind)))
    f.write("    </levels>\n    <mapsize w='%d' h='%d'></mapsize>\n    <tilesize s='%d'></tilesize>\n    <game lifes=\"10\" startmoney=\"220\"></game>\n  </maptree>\n" % (W, H, T))
with open(os.path.join(OUT, 'waypoints.xml'), 'w') as f:
    f.write("<?xml version='1.0'?>\n  <pathfinding>\n    <waypoints>\n")
    for i, (x, y) in enumerate(PATH_PX):
        f.write("      <waypoint id='%d'>%d,%d,2</waypoint>\n" % (i, x, y))
    f.write("    </waypoints>\n  </pathfinding>\n")

RS = '../../river_side.xml/data/'
WD = '../../woods.xml/data/'
def enemy(cs, icon, hp, money, speed, anim, count, dmg=0):
    return ('    \t\t<enemy charset="%s%s" icon="%s%s" tilesize="64" maxhealth="%d" money="%d" movespeed="%d" animationspeed="%d" count="%d" damage="%d">\n            </enemy>\n'
            % (RS, cs, RS, icon, hp, money, speed, anim, count, dmg))
S, G, B, N = ('charset1.png', 'icon1.png'), ('charset2.png', 'icon2.png'), ('charset3.png', 'icon3.png'), ('charset4.png', 'icon4.png')
waves = [  # spacing, [(sheet, hp, money, speed, anim, count, dmg)]
    (1800, [(S, 6, 2, 60, 15, 10)]),
    (1400, [(S, 8, 2, 60, 15, 12), (G, 60, 5, 20, 8, 2)]),
    (1000, [(N, 12, 3, 80, 15, 22)]),
    (900, [(S, 12, 3, 60, 15, 18), (N, 16, 4, 80, 15, 10)]),
    (1500, [(G, 110, 10, 35, 8, 4), (S, 14, 3, 60, 15, 8)]),
    (1000, [(B, 150, 14, 50, 10, 2, 8), (N, 18, 4, 80, 15, 12)]),
    (800, [(S, 28, 4, 80, 15, 24), (N, 20, 5, 90, 15, 10)]),
    (1200, [(G, 160, 15, 50, 8, 4), (B, 210, 20, 50, 10, 2, 16)]),
    (900, [(N, 30, 5, 100, 15, 36)]),
    (1400, [(S, 30, 5, 60, 15, 18), (N, 38, 8, 80, 15, 14), (G, 360, 15, 50, 8, 4), (B, 420, 20, 70, 10, 3, 32)]),
]
with open(os.path.join(OUT, 'waves.xml'), 'w') as f:
    f.write("<?xml version='1.0'?>\n  <enemies>\n    <waves>\n")
    for dist, es in waves:
        f.write('\n    \t<wave enemydistance="%d">\n' % dist)
        for e in es:
            f.write(enemy(e[0][0], e[0][1], *e[1:]))
        f.write('        </wave>\n')
    f.write("    </waves>\n  </enemies>\n")
# the same four towers as Dark Woods, sharing its pictures
src = open(os.path.join(DATA, 'maps', 'woods.xml', 'towers.xml')).read()
for n in ('tower1', 'tower2', 'tower3', 'tower4'):
    src = src.replace('charset="%s.png"' % n, 'charset="%s%s.png"' % (WD, n))
for n in ('tower_icon1', 'tower_icon2', 'tower_icon3', 'tower_icon4'):
    src = src.replace('towericon="%s.png"' % n, 'towericon="%s%s.png"' % (WD, n))
open(os.path.join(OUT, 'towers.xml'), 'w').write(src)

# the icon in the map list: a 50 x 50 crop around a pool
logo = final.crop((560, 150, 560 + 300, 150 + 300)).resize((50, 50), Image.LANCZOS)
logo.save(os.path.join(OUT, 'map_logo.png'))

length = sum(math.dist(a_, b_) for a_, b_ in zip(PATH_PX, PATH_PX[1:]))
mincover = min(COVER[i] for i in INNER)
low = [(int(SAMPLES[i][0]), int(SAMPLES[i][1]), COVER[i]) for i in INNER if COVER[i] < 2]
print('weak stretches (x, y, spots in reach):', low[:12])
total_enemies = sum(e[5] for _, es in waves for e in es)
print('trail %d px (about %d tiles, %d way points), %d spots of %d tiles, every stretch (bar the ends) in reach of at least %d spots, %d enemies in %d waves'
      % (length, length // T, len(PATH_PX), len(SPOTS), W * H, mincover, total_enemies, len(waves)))
