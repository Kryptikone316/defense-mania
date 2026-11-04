#!/usr/bin/env python3
"""Draws "The Tundra", a mid-size (30 x 16 tiles of 48 px) Defense Mania map between the standard maps and Bigger, and
writes its data files into <data>/maps/tundra.xml/ . The picture is painted here from the game's own grass texture
(turned into wind-packed snow) plus procedural ice, rocks, pines and a blizzard, so the map is original artwork.
Usage: make_tundra.py <the game's data folder> [preview.png]"""
import math, os, random, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageChops

DATA = sys.argv[1]
OUT = os.path.join(DATA, 'maps', 'tundra.xml')
T, W, H = 48, 30, 16
PW, PH = W * T, H * T
rng = np.random.default_rng(2211)
random.seed(2211)

# ------------------------------------------------------------------ the layout
# The way points, in pixels: three long lanes across the map, joined at the ends, each one wandering up and down so the
# whole trail reads as a track that can barely be followed through a blizzard. It enters on the left edge, runs right,
# comes back left, and leaves on the right edge again.
def meander(x0, x1, y_mid, amp, step, seed):
    r = random.Random(seed)
    pts = []
    n = max(2, int(abs(x1 - x0) // step))
    y = y_mid
    for i in range(n + 1):
        x = x0 + (x1 - x0) * i / n
        y = y_mid + (r.random() * 2 - 1) * amp if i not in (0, n) else y_mid
        pts.append((int(x), int(y)))
    return pts

LANE1 = meander(24, 1330, 130, 64, 105, 5)
LANE2 = meander(1330, 130, 384, 78, 105, 6)
LANE3 = meander(130, 1416, 640, 64, 105, 7)
PATH_PX = LANE1 + [(1396, 205)] + LANE2[1:] + [(100, 512)] + LANE3[1:]
PATH_PX[-1] = (1416, 640)

def _segdist(a, b, c, d):
    best = 1e9
    for i in range(21):
        p = (a[0] + (b[0] - a[0]) * i / 20, a[1] + (b[1] - a[1]) * i / 20)
        for j in range(21):
            q = (c[0] + (d[0] - c[0]) * j / 20, c[1] + (d[1] - c[1]) * j / 20)
            best = min(best, math.hypot(p[0] - q[0], p[1] - q[1]))
    return best
_close = [(i, j, int(_segdist(PATH_PX[i], PATH_PX[i + 1], PATH_PX[j], PATH_PX[j + 1])))
          for i in range(len(PATH_PX) - 1) for j in range(i + 2, len(PATH_PX) - 1)
          if _segdist(PATH_PX[i], PATH_PX[i + 1], PATH_PX[j], PATH_PX[j + 1]) < 90]
print('parts of the trail closer than 90 px to each other:', _close)

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

def choose_spots(target=112):
    """tower spots: scarce, scattered along both sides of the trail and clustered at the kinks, and every stretch of
    the trail (bar its ends) stays within reach of at least two of them"""
    rng_sp = random.Random(5)
    cand = [(c, r) for c in range(W) for r in range(H) if (c, r) not in PT and 42 <= path_dist(*centre(c, r)) <= 110]
    reach = {t: [i for i, p in enumerate(SAMPLES) if math.hypot(p[0] - centre(*t)[0], p[1] - centre(*t)[1]) <= REACH] for t in cand}
    cover = [0] * len(SAMPLES)
    inner = [i for i, p in enumerate(SAMPLES) if 100 < p[0] < PW - 100]
    spots = []
    side_by_side = False
    while len(spots) < target + 6 and (len(spots) < 84 or min(cover[i] for i in inner) < 2):
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

SNOW_LIGHT, SNOW_DARK = (232, 238, 246), (160, 176, 202)
DRIFT = (246, 249, 252)
SLUSH_LIGHT, SLUSH_DARK = (190, 198, 212), (138, 148, 166)
SLATE_LIGHT, SLATE_DARK = (150, 160, 178), (60, 68, 86)
ICE_DEEP, ICE_MID, ICE_LIGHT = (120, 150, 176), (170, 198, 218), (226, 240, 248)
ROCK = (96, 100, 112)
HAZE = (236, 242, 250)

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

# ------------------------------------------------------------------ 1. ground: frosted rock, from the game's own stone tiles
yy, xx = np.mgrid[0:PH, 0:PW].astype(np.float32)
rock_tiles = []
for tx, ty in [(1, 0), (2, 0), (3, 0), (0, 2), (1, 2), (2, 2), (3, 2)]:
    t_ = np.asarray(ts.crop((tx * T, ty * T, tx * T + T, ty * T + T)).convert('L'), np.float32) / 255.0
    t_ = (t_ - t_.min()) / (t_.max() - t_.min() + 1e-6)
    rock_tiles.append(t_)
rl = np.zeros((PH, PW), np.float32)
for c in range(W):
    for r in range(H):
        t_ = rock_tiles[int(rng.integers(len(rock_tiles)))]
        if rng.random() < .5: t_ = t_[:, ::-1]
        if rng.random() < .5: t_ = t_[::-1, :]
        if rng.random() < .5: t_ = t_.T
        rl[r * T:(r + 1) * T, c * T:(c + 1) * T] = t_
rl = soft_mask(rl, 0.6)
tone = np.clip(0.38 + 0.9 * (N1 - 0.5) + 0.5 * (N2 - 0.5), 0, 1)
slate = np.array(SLATE_DARK, np.float32)[None, None, :] + (np.array(SLATE_LIGHT, np.float32) - np.array(SLATE_DARK, np.float32))[None, None, :] * (0.25 * tone + 0.75 * rl)[:, :, None]
img = slate
# hoarfrost: a fine white crystal fur on the rock, thick in the hollows and on the ridges the wind did not scour
hf = noise(5, 8) * 0.6 + noise(11, 9) * 0.4
frost = np.clip((hf * 0.55 + N2 * 0.45 - 0.52) * 7, 0, 1) * (0.35 + 0.65 * np.clip((N1 - 0.2) * 2.5, 0, 1))
img = paste_colour(img, np.full((PH, PW, 3), 236.0, np.float32) * np.array([0.96, 0.99, 1.02], np.float32), frost * 0.78)
# snow lying in the lows
snow_m = np.clip((N1 * 0.6 + N2 * 0.4 - 0.60) * 8, 0, 1)
img = paste_colour(img, colour_field(DRIFT, SNOW_LIGHT, N3, 0.05), snow_m * 0.92)
# the wind: long diagonal drifts of snow across it
ridge = np.sin((xx * 0.85 - yy * 0.38) / 21.0 + N2 * 11.0) * np.sin((xx * 0.2 + yy * 0.1) / 90.0 + N1 * 6.0)
crest = np.clip((ridge - 0.55) * 2.4, 0, 1) * np.clip((N1 - 0.3) * 3, 0, 1)
img = paste_colour(img, colour_field(DRIFT, DRIFT, N3, 0.04), crest * 0.5)

# ------------------------------------------------------------------ 2. frozen lakes in the open ground
def open_ground():
    pts = []
    for y in range(24, PH, 24):
        for x in range(24, PW, 24):
            d = path_dist(x, y)
            if d > 80:
                pts.append((d, x, y))
    pts.sort(reverse=True)
    chosen = []
    for d, x, y in pts:
        if all(math.hypot(x - cx, y - cy) > 130 for _, cx, cy, _ in chosen):
            chosen.append((d, x, y, d))
        if len(chosen) >= 16:
            break
    return chosen
POOLS = []
_rp = random.Random(4)
for d, x, y, _ in open_ground():
    rx = max(0.6, (d - 64) / T * (1.0 + 0.5 * _rp.random()))
    ry = max(0.5, (d - 64) / T * (0.7 + 0.4 * _rp.random()))
    POOLS.append((x / T, y / T, rx, ry))
water = np.zeros((PH, PW), np.float32)
for cx, cy, rx, ry in POOLS:
    d = np.sqrt(((xx - cx * T) / (rx * T)) ** 2 + ((yy - cy * T) / (ry * T)) ** 2)
    wob = (N2 - 0.5) * 0.55 + (N3 - 0.5) * 0.25
    water = np.maximum(water, np.clip((1.0 + wob - d) * 5, 0, 1))
keep = np.zeros((PH, PW), np.float32)
for (c, r) in list(PT) + SPOTS:
    keep[r * T:(r + 1) * T, c * T:(c + 1) * T] = 1
_pi = Image.new('L', (PW, PH), 0)
ImageDraw.Draw(_pi).line(PATH_PX, fill=255, width=92, joint='curve')
keep = np.maximum(keep, np.asarray(_pi, np.float32) / 255.0)
keep = soft_mask(keep, 5)
water = np.clip(water - keep * 1.2, 0, 1)
shore = np.clip(soft_mask(np.clip(water * 3, 0, 1), 5) - water * 0.9, 0, 1)
img = paste_colour(img, colour_field(DRIFT, SNOW_LIGHT, N3, 0.05), np.clip(shore * 1.8, 0, 1) * 0.8)
ice = colour_field(ICE_DEEP, ICE_MID, np.clip(0.45 + 0.9 * (N2 - 0.5), 0, 1), 0.05)
glint = np.clip(np.sin(xx * 0.05 - yy * 0.04 + N3 * 8) - 0.7, 0, 1) * 2.2
ice = ice + np.array(ICE_LIGHT, np.float32)[None, None, :] * (glint[:, :, None] * 0.3)
img = paste_colour(img, ice, soft_mask(water, 1.2))
# cracks across the ice
crack_im = Image.new('L', (PW, PH), 0)
cd = ImageDraw.Draw(crack_im)
_rc = random.Random(11)
for cx, cy, rx, ry in POOLS:
    for k in range(5):
        x, y = cx * T + (_rc.random() - 0.5) * rx * T, cy * T + (_rc.random() - 0.5) * ry * T
        pts = [(x, y)]
        for j in range(5):
            x += (_rc.random() - 0.5) * 46
            y += (_rc.random() - 0.5) * 30
            pts.append((x, y))
        cd.line(pts, fill=200, width=1)
cracks = np.asarray(crack_im.filter(ImageFilter.GaussianBlur(0.6)), np.float32) / 255.0 * soft_mask(water, 1.5)
img = paste_colour(img, np.full((PH, PW, 3), 236.0, np.float32), cracks * 0.55)

# ------------------------------------------------------------------ 3. the trodden track
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
ragged = np.clip((np.clip(pm_soft + (N3 - 0.5) * 0.5, 0, 1) - 0.45) * 5, 0, 1)
slush = colour_field(SLUSH_DARK, SLUSH_LIGHT, np.clip(0.55 + 0.9 * (N2 - 0.5), 0, 1), 0.16)
img = paste_colour(img, np.full((PH, PW, 3), 120.0, np.float32) * np.array([0.86, 0.92, 1.0], np.float32), soft_mask(pm, 7) * 0.45 * (1 - ragged))
img = paste_colour(img, slush, ragged * 0.96)
img = paste_colour(img, np.full((PH, PW, 3), 92.0, np.float32) * np.array([0.9, 0.95, 1.1], np.float32), edge * 0.4 * ragged)

# ------------------------------------------------------------------ 4. wind-cleared patches where towers can go
mounds = np.zeros((PH, PW), np.float32)
for (c, r) in SPOTS:
    cx, cy = centre(c, r)
    d = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
    mounds = np.maximum(mounds, np.clip((19 - d) / 5.0 + (N3 - 0.5) * 0.6, 0, 1))
img = paste_colour(img, colour_field((150, 156, 170), (128, 134, 150), N3, 0.2), mounds * 0.70)

# ------------------------------------------------------------------ 5. sprites: pines, dead trees, rocks, drifts
base = to_img(img).convert('RGBA')
sprites = []

def pine(x, y, s=1.0):
    def draw(layer):
        d = ImageDraw.Draw(layer)
        sh = Image.new('RGBA', layer.size, (0, 0, 0, 0))
        ImageDraw.Draw(sh).ellipse((x - 20 * s + 8, y - 4 * s, x + 20 * s + 8, y + 12 * s), fill=(40, 52, 76, 100))
        layer.alpha_composite(sh.filter(ImageFilter.GaussianBlur(4)))
        d.rectangle((x - 3 * s, y - 8 * s, x + 3 * s, y + 4 * s), fill=(70, 56, 50, 255))
        rr = random.Random(int(x * 29 + y * 13))
        for k, (w, top) in enumerate([(24, -20), (19, -36), (14, -50), (9, -62)]):
            w *= s; top *= s
            col = tuple(int(v) for v in lerp((40, 66, 70), (62, 96, 96), rr.random()))
            d.polygon([(x - w, y + top + 22 * s), (x + w, y + top + 22 * s), (x, y + top - 4 * s)], fill=col + (255,))
            # the snow lying on it
            d.polygon([(x - w * 0.8, y + top + 12 * s), (x + w * 0.5, y + top + 8 * s), (x, y + top - 4 * s)], fill=(236, 242, 250, 235))
    sprites.append((y, draw))

def dead_tree(x, y, s=1.0):
    def draw(layer):
        d = ImageDraw.Draw(layer)
        rr = random.Random(int(x * 7 + y * 3))
        d.line([(x, y), (x + rr.randint(-4, 4), y - 38 * s)], fill=(58, 52, 56, 255), width=4)
        for k in range(5):
            by = y - (12 + k * 7) * s
            dx = (rr.random() * 2 - 1) * 22 * s
            d.line([(x, by), (x + dx, by - 12 * s)], fill=(58, 52, 56, 255), width=2)
            d.line([(x + dx, by - 12 * s), (x + dx * 1.3, by - 20 * s)], fill=(70, 64, 68, 255), width=1)
        d.ellipse((x - 6 * s, y - 3 * s, x + 6 * s, y + 4 * s), fill=(244, 248, 252, 230))
    sprites.append((y, draw))

def rock(x, y, s=1.0):
    def draw(layer):
        d = ImageDraw.Draw(layer)
        d.ellipse((x - 15 * s, y - 8 * s, x + 15 * s, y + 8 * s), fill=(52, 56, 70, 255))
        d.ellipse((x - 13 * s, y - 11 * s, x + 12 * s, y + 5 * s), fill=(112, 116, 130, 255))
        d.ellipse((x - 9 * s, y - 11 * s, x + 3 * s, y - 4 * s), fill=(150, 154, 168, 255))
        d.ellipse((x - 11 * s, y - 13 * s, x + 7 * s, y - 7 * s), fill=(244, 248, 252, 245))
    sprites.append((y, draw))

def drift(x, y, s=1.0):
    def draw(layer):
        sh = Image.new('RGBA', layer.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(sh)
        d.ellipse((x - 30 * s, y - 8 * s, x + 30 * s, y + 10 * s), fill=(190, 204, 226, 180))
        d.ellipse((x - 26 * s, y - 12 * s, x + 22 * s, y + 4 * s), fill=(250, 252, 254, 235))
        layer.alpha_composite(sh.filter(ImageFilter.GaussianBlur(1.6)))
    sprites.append((y, draw))

def scrub(x, y, n=6):
    def draw(layer):
        d = ImageDraw.Draw(layer)
        rr = random.Random(int(x * 13 + y * 7))
        for k in range(n):
            bx = x + (rr.random() - 0.5) * 16
            h = 8 + rr.random() * 12
            d.line([(bx, y), (bx + (rr.random() - 0.5) * 8, y - h)], fill=(112, 94, 74, 255), width=1)
    sprites.append((y, draw))

occupied = set(PT) | SPOT_SET
free = [(c, r) for c in range(W) for r in range(H) if (c, r) not in occupied]
ice_t = [(c, r) for (c, r) in free if water[r * T + T // 2, c * T + T // 2] > 0.5]
land_t = [(c, r) for (c, r) in free if (c, r) not in ice_t]
pathdist = {t: path_dist(*centre(*t)) for t in land_t}
rr = random.Random(77)
for (c, r) in land_t:
    x, y = centre(c, r)
    x += rr.randint(-9, 9); y += rr.randint(-6, 8)
    far = pathdist[(c, r)]
    roll = rr.random()
    if far >= 76 and roll < 0.42:
        pine(x, y + 12, 0.85 + rr.random() * 0.45)
    elif far >= 76 and roll < 0.50:
        dead_tree(x, y + 10, 0.9 + rr.random() * 0.4)
    elif roll < 0.58:
        rock(x, y + 6, 0.8 + rr.random() * 0.9)
    elif roll < 0.68:
        drift(x, y + 8, 0.6 + rr.random() * 0.5)
    elif roll < 0.80:
        scrub(x, y + 8, 4 + rr.randint(0, 4))
for (c, r) in SPOTS:
    x, y = centre(c, r)
    if rr.random() < 0.35: rock(x + rr.choice([-15, 15]), y + 16, 0.4)
    if rr.random() < 0.3: scrub(x + rr.choice([-16, 16]), y + 18, 3)
for i, (sx, sy) in enumerate(SAMPLES):
    if i % 7 == 0 and rr.random() < 0.5:
        drift(sx + rr.choice([-30, 30]), sy + rr.choice([-6, 12]), 0.6)

layer = Image.new('RGBA', base.size, (0, 0, 0, 0))
for _, fn in sorted(sprites, key=lambda s: s[0]):
    fn(layer)
layer = layer.filter(ImageFilter.GaussianBlur(0.6))
base.alpha_composite(layer)

# ------------------------------------------------------------------ 6. the blizzard: haze, streaks of snow, cold light
arr = np.asarray(base.convert('RGB'), np.float32)
band = 0.5 + 0.5 * np.sin(yy / 70.0 + N1 * 9.0)
fog = (0.16 + 0.14 * band * N2 + 0.10 * (yy / PH)) [:, :, None]
arr = arr * (1 - fog) + np.array(HAZE, np.float32)[None, None, :] * fog
streak_im = Image.new('L', (PW, PH), 0)
sd = ImageDraw.Draw(streak_im)
_rs = random.Random(31)
for k in range(2600):
    x, y = _rs.random() * PW, _rs.random() * PH
    ln = 10 + _rs.random() * 30
    sd.line([(x, y), (x + ln * 0.9, y + ln * 0.36)], fill=int(70 + _rs.random() * 120), width=1)
streaks = np.asarray(streak_im.filter(ImageFilter.GaussianBlur(0.5)), np.float32) / 255.0
arr = arr + streaks[:, :, None] * (np.array([255, 255, 255], np.float32)[None, None, :] - arr) * 0.42
# the edges disappear into the white
edge_fog = np.clip((((xx - PW / 2) / (PW / 2)) ** 2 + ((yy - PH / 2) / (PH / 2)) ** 2) ** 1.5 - 0.35, 0, 1) * 0.55
arr = arr * (1 - edge_fog[:, :, None]) + np.array(HAZE, np.float32)[None, None, :] * edge_fog[:, :, None]
# a cool cast
arr = arr * np.array([0.96, 0.99, 1.04], np.float32)[None, None, :]
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
START_GOLD = 250
with open(os.path.join(OUT, 'maptree.xml'), 'w') as f:
    f.write("<?xml version='1.0'?>\n  <maptree>\n    <levels>\n")
    f.write("      <level id='0'>%s</level>\n      <level id='1'>%s</level>\n      <level id='2'>%s</level>\n" % (blank, blank, grid_text(kind)))
    f.write("    </levels>\n    <mapsize w='%d' h='%d'></mapsize>\n    <tilesize s='%d'></tilesize>\n    <game lifes=\"10\" startmoney=\"%d\"></game>\n  </maptree>\n" % (W, H, T, START_GOLD))
with open(os.path.join(OUT, 'waypoints.xml'), 'w') as f:
    f.write("<?xml version='1.0'?>\n  <pathfinding>\n    <waypoints>\n")
    for i, (x, y) in enumerate(PATH_PX):
        f.write("      <waypoint id='%d'>%d,%d,2</waypoint>\n" % (i, x, y))
    f.write("    </waypoints>\n  </pathfinding>\n")

# ---- the waves: hordes, in the spirit of Bigger but about 15 percent smaller and sized to what the gold can pay for
RS = '../../river_side.xml/data/'
WD = '../../woods.xml/data/'
# type: (sheet, icon, health relative to a spider, speed, animation speed, attack on towers)
TYPES = {'S': ('charset1.png', 'icon1.png', 1.0, 70, 15, 0), 'N': ('charset4.png', 'icon4.png', 1.5, 110, 15, 0),
         'O': ('charset2.png', 'icon2.png', 14.0, 50, 8, 0), 'B': ('charset3.png', 'icon3.png', 17.0, 60, 10, 12)}
GOLD = {'S': [2, 2, 2, 3, 3, 3, 3, 4, 4, 4], 'N': [3, 3, 4, 4, 5, 5, 5, 6, 6, 6], 'O': [12, 12, 14, 14, 16, 16, 18, 18, 20, 20], 'B': [20, 20, 22, 22, 24, 24, 26, 28, 30, 30]}
PLAN = [  # spacing ms, {type: count}, target load on Normal (1.0 = all the damage the gold could buy, see the notes)
    (450, {'S': 60}, 0.05),
    (400, {'S': 120}, 0.09),
    (400, {'S': 80, 'N': 40}, 0.16),
    (800, {'O': 10}, 0.28),
    (400, {'S': 100, 'N': 50, 'O': 10}, 0.38),
    (400, {'S': 100, 'N': 60, 'O': 12}, 0.48),
    (800, {'B': 12}, 0.58),
    (400, {'S': 100, 'N': 50}, 0.50),
    (700, {'B': 12, 'O': 20}, 0.76),
    (400, {'S': 100, 'N': 50, 'O': 15, 'B': 10}, 0.66),
]
waves = []
gold_before = START_GOLD
for k, (spacing, comp, target) in enumerate(PLAN):
    n = sum(comp.values())
    slow = min(TYPES[t][3] for t in comp)
    duration = (n - 1) * spacing / 1000.0 + 182.0 / slow
    capacity = 0.36 * gold_before * duration
    weight = sum(c * TYPES[t][2] for t, c in comp.items())
    hp_s = max(3, int(round(target * capacity / (2.0 * weight))))
    es = []
    for t, c in comp.items():
        sheet, icon, ratio, speed, anim, dmg = TYPES[t]
        es.append(((sheet, icon), max(2, int(round(hp_s * ratio))), GOLD[t][k], speed, anim, c, dmg))
    waves.append((spacing, es))
    gold_before += sum(e[2] * e[5] for e in es)
def enemy(cs, icon, hp, money, speed, anim, count, dmg=0):
    return ('    \t\t<enemy charset="%s%s" icon="%s%s" tilesize="64" maxhealth="%d" money="%d" movespeed="%d" animationspeed="%d" count="%d" damage="%d">\n            </enemy>\n'
            % (RS, cs, RS, icon, hp, money, speed, anim, count, dmg))
with open(os.path.join(OUT, 'waves.xml'), 'w') as f:
    f.write("<?xml version='1.0'?>\n  <enemies>\n    <waves>\n")
    for dist, es in waves:
        f.write('\n    \t<wave enemydistance="%d">\n' % dist)
        for e in es:
            f.write(enemy(e[0][0], e[0][1], *e[1:]))
        f.write('        </wave>\n')
    f.write("    </waves>\n  </enemies>\n")
src = open(os.path.join(DATA, 'maps', 'woods.xml', 'towers.xml')).read()
for n in ('tower1', 'tower2', 'tower3', 'tower4'):
    src = src.replace('charset="%s.png"' % n, 'charset="%s%s.png"' % (WD, n))
for n in ('tower_icon1', 'tower_icon2', 'tower_icon3', 'tower_icon4'):
    src = src.replace('towericon="%s.png"' % n, 'towericon="%s%s.png"' % (WD, n))
open(os.path.join(OUT, 'towers.xml'), 'w').write(src)

# the icon in the map list: a 50 x 50 crop
if POOLS:
    cx, cy = int(POOLS[0][0] * T), int(POOLS[0][1] * T)
else:
    cx, cy = PW // 2, PH // 2
x0 = max(0, min(PW - 300, cx - 150)); y0 = max(0, min(PH - 300, cy - 150))
final.crop((x0, y0, x0 + 300, y0 + 300)).resize((50, 50), Image.LANCZOS).save(os.path.join(OUT, 'map_logo.png'))

length = sum(math.dist(a_, b_) for a_, b_ in zip(PATH_PX, PATH_PX[1:]))
mincover = min(COVER[i] for i in INNER)
low = [(int(SAMPLES[i][0]), int(SAMPLES[i][1]), COVER[i]) for i in INNER if COVER[i] < 2]
print('weak stretches (x, y, spots in reach):', low[:12])
total_enemies = sum(e[5] for _, es in waves for e in es)
hp_total = sum(e[1] * e[5] for _, es in waves for e in es); gold_total = sum(e[2] * e[5] for _, es in waves for e in es)
print('trail %d px (about %d tiles, %d way points), %d spots of %d tiles, every stretch (bar the ends) in reach of at least %d spots'
      % (length, length // T, len(PATH_PX), len(SPOTS), W * H, mincover))
print('%d enemies in %d waves, base health %d, gold from kills %d (%.1f health per gold), start gold %d' % (total_enemies, len(waves), hp_total, gold_total, hp_total / gold_total, START_GOLD))
for i, (sp, es) in enumerate(waves, 1):
    print('  wave %2d: %s' % (i, '  '.join('%s x%d hp %d' % (e[0][0][7], e[5], e[1]) for e in es)))
