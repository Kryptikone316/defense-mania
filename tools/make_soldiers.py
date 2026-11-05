#!/usr/bin/env python3
"""Writes <data>/soldiers.xml (what the Bastion's four kinds of soldier are) and draws their pictures into <data>/soldiers/ (needs pillow).
A soldier is a small mobile tower: it guards the stretch of path before the keep (its zone, set by research), walks to the nearest creature of the kind it is trained against, holds there and strikes whatever is in reach.
Against its own kind a blow does the full damage, against anything else the 'off' share of it. Research (tools/make_research.py) sets how many go out, for how long and how tough they are.
  vs      creature kinds it is trained against (see tools/make_waves.py)     pierce  armour does not count against its own kind
  ranged  may strike flyers (the rest cannot reach them)                     roller  pushes a boulder: every ground creature it touches is hit, and the boulder wears with every hit
With a second argument, the folder of Blender renders (tools/blender/soldiers.py), the pictures are sheets of 64 px frames, six across (three walking, three striking)
and eight down (looking down, left, right, up, then dying in those four ways, three frames each: stagger, tipping, down) packed from them; without it, a plain drawn stand-in.
Usage: make_soldiers.py <the game's data folder> [<folder of renders>]"""
import os, sys
from PIL import Image, ImageDraw
DATA = sys.argv[1]
SOLD = [
 # id, name, health, damage, pct (or this percent of the target's full health, if more), rate (ms), reach (px), speed (px/s), vs, flags, text
 ('vermin', 'Vermin Hunter', 130, 12, 9, 1000, 28, 60, 'vermin', '', 'Cleavers by the keep. Full damage to spiders, wargs and gnolls, a third to anything else.'),
 ('giant', 'Giant Slayer', 300, 36, 6, 1300, 30, 38, 'giant', 'pierce="1"', 'Pikemen by the keep, ignoring armour: full damage to orcs, ogres and trolls.'),
 ('sky', 'Skyguard', 100, 14, 9, 900, 150, 50, 'flyer', 'ranged="1"', 'Slingers by the keep; the only soldiers who reach flyers. Full damage to wyverns.'),
 ('roller', 'Boulder Roller', 300, 30, 4, 500, 26, 36, 'vermin giant', 'roller="1" wear="5" wearpct="3"', 'Rolls a boulder up the path: hits every ground creature it touches, and wears down as it goes.'),
]
out = ["<?xml version='1.0'?>\n<soldiers off=\"0.3\">\n"]
for sid, name, hp, dmg, pct, rate, reach, speed, vs, flags, text in SOLD:
    out.append('  <soldier id="%s" name="%s" health="%d" damage="%d" pct="%d" rate="%d" reach="%d" speed="%d" vs="%s" %s picture="%s.png">%s</soldier>\n' % (sid, name, hp, dmg, pct, rate, reach, speed, vs, flags, sid, text))
out.append('</soldiers>\n')
open(os.path.join(DATA, 'soldiers.xml'), 'w').write(''.join(out))
os.makedirs(os.path.join(DATA, 'soldiers'), exist_ok=True)
RENDERS = sys.argv[2] if len(sys.argv) > 2 else None
def pack(sid):
    """the 384 x 512 sheet of one soldier from its renders"""
    sheet = Image.new('RGBA', (6 * 64, 8 * 64), (0, 0, 0, 0))
    shadow = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).ellipse((19, 47, 45, 56), fill=(0, 0, 0, 80))
    for f in range(9):
        for d in range(4):
            im = Image.open(os.path.join(RENDERS, '%s_%d_%d.png' % (sid, f, d))).convert('RGBA')
            im = im.convert('RGBa').resize((64, 64), Image.LANCZOS).convert('RGBA')   # premultiplied, so no dark fringes
            tile = shadow.copy(); tile.alpha_composite(im)
            col, row = (f, d) if f < 6 else (f - 6, 4 + d)
            sheet.paste(tile, (col * 64, row * 64))
    return sheet
if RENDERS:
    for sid in ('vermin', 'giant', 'sky', 'roller'):
        pack(sid).save(os.path.join(DATA, 'soldiers', sid + '.png'))
    print('soldiers.xml and 4 sheets')
    sys.exit(0)
S = 4
def fig(sid, body, helm, weapon):
    n = 32
    im = Image.new('RGBA', (n * S, n * S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.ellipse((6 * S, 26 * S, 26 * S, 31 * S), fill=(0, 0, 0, 110))
    d.rectangle((11 * S, 15 * S, 21 * S, 27 * S), fill=body + (255,), outline=(30, 26, 24, 255), width=S // 2)    # body
    d.rectangle((11 * S, 26 * S, 15 * S, 30 * S), fill=(60, 44, 30, 255)); d.rectangle((17 * S, 26 * S, 21 * S, 30 * S), fill=(60, 44, 30, 255))   # boots
    d.ellipse((11 * S, 5 * S, 21 * S, 16 * S), fill=(232, 190, 150, 255), outline=(30, 26, 24, 255), width=S // 2)   # head
    helm(d)
    weapon(d)
    return im.resize((n, n), Image.LANCZOS)
def helm_cap(d): d.pieslice((10 * S, 3 * S, 22 * S, 14 * S), 180, 360, fill=(70, 120, 60, 255), outline=(30, 26, 24, 255))
def helm_iron(d):
    d.pieslice((10 * S, 3 * S, 22 * S, 15 * S), 180, 360, fill=(150, 156, 164, 255), outline=(30, 26, 24, 255)); d.rectangle((15 * S, 4 * S, 17 * S, 11 * S), fill=(150, 156, 164, 255))
def helm_hood(d): d.pieslice((9 * S, 2 * S, 23 * S, 16 * S), 160, 380, fill=(60, 96, 150, 255), outline=(30, 26, 24, 255))
def helm_rock(d): d.pieslice((10 * S, 3 * S, 22 * S, 14 * S), 180, 360, fill=(160, 120, 70, 255), outline=(30, 26, 24, 255))
def w_cleaver(d): d.line((22 * S, 20 * S, 28 * S, 10 * S), fill=(150, 90, 40, 255), width=S); d.polygon([(26 * S, 12 * S), (31 * S, 6 * S), (30 * S, 13 * S)], fill=(200, 204, 210, 255))
def w_pike(d): d.line((24 * S, 28 * S, 24 * S, 2 * S), fill=(120, 80, 40, 255), width=S); d.polygon([(22 * S, 6 * S), (24 * S, 0 * S), (26 * S, 6 * S)], fill=(210, 214, 220, 255)); d.ellipse((5 * S, 15 * S, 12 * S, 25 * S), fill=(130, 70, 40, 255), outline=(30, 26, 24, 255))
def w_sling(d): d.line((21 * S, 19 * S, 28 * S, 12 * S), fill=(120, 80, 40, 255), width=S // 2); d.ellipse((27 * S, 8 * S, 31 * S, 12 * S), fill=(110, 110, 116, 255))
def w_none(d): d.rectangle((6 * S, 18 * S, 11 * S, 22 * S), fill=(232, 190, 150, 255)); d.rectangle((21 * S, 18 * S, 26 * S, 22 * S), fill=(232, 190, 150, 255))
for sid, body, helm, weapon in (('vermin', (96, 140, 70), helm_cap, w_cleaver), ('giant', (150, 70, 56), helm_iron, w_pike), ('sky', (70, 110, 170), helm_hood, w_sling), ('roller', (170, 130, 70), helm_rock, w_none)):
    fig(sid, body, helm, weapon).save(os.path.join(DATA, 'soldiers', sid + '.png'))
print('soldiers.xml and 4 pictures')
