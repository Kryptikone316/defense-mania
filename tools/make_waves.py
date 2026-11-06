#!/usr/bin/env python3
"""Writes <data>/maps/<map>.xml/waves.xml for every map from ONE table of creatures and one plan per map. Each wave names its creatures and how many;
the health is then worked out so that the wave asks for the share of the ideal damage (the load, see check_maps.py) the plan sets, on Easy. Normal and Hard
have two and three times the health and are paid a little more, which is what check_maps.py shows.
Usage: make_waves.py <the game's data folder> [map ...]"""
import os, sys, math
DATA = sys.argv[1]
ONLY = sys.argv[2:]   # optional: only these maps (the others keep the waves.xml they have)
sys.argv = sys.argv[:2]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'check_maps.py')).read().split("maps = [l.strip()")[0])

SPIDER = '../../river_side.xml/data/'   # the original sheets live with River Side
NEW = '../../../monsters/'
# name: sheet, icon, health relative to a spider, speed, animation, gold, extra attributes (kind: what the Bastion's soldiers are trained against: vermin, giant or flyer)
MON = {
 'spider': (SPIDER + 'charset1.png', SPIDER + 'icon1.png', 1.0, 70, 15, 2, {'kind': 'vermin'}),
 'brood':  (NEW + 'brood.png', NEW + 'brood_icon.png', 1.7, 66, 15, 3, {'kind': 'vermin', 'armor': 1, 'damage': 1, 'attackradius': 45}),
 'warg':   (NEW + 'warg.png', NEW + 'warg_icon.png', 1.5, 120, 15, 3, {'kind': 'vermin'}),
 'gnoll':  (NEW + 'gnoll.png', NEW + 'gnoll_icon.png', 2.8, 85, 12, 4, {'kind': 'vermin'}),
 'orc':    (NEW + 'orc.png', NEW + 'orc_icon.png', 7.0, 55, 10, 8, {'kind': 'giant', 'armor': 2, 'damage': 2, 'attackradius': 50}),
 'ogre':   (SPIDER + 'charset2.png', SPIDER + 'icon2.png', 14.0, 45, 8, 12, {'kind': 'giant', 'damage': 4, 'attackradius': 55, 'thrower': 1, 'throwrange': 150, 'throwdamage': 90, 'lifttime': 2500}),
 'troll':  (NEW + 'halftroll.png', NEW + 'halftroll_icon.png', 20.0, 40, 8, 18, {'kind': 'giant', 'armor': 1, 'damage': 8, 'attackradius': 60, 'thrower': 1, 'throwrange': 160, 'throwdamage': 130, 'lifttime': 2500}),
 'wyvern': (NEW + 'wyvern.png', NEW + 'wyvern_icon.png', 6.0, 75, 15, 10, {'kind': 'flyer', 'flying': 1, 'damage': 6, 'attackradius': 65}),
}
def ramp(n, lo, hi, dips=()):
    out = [lo + (hi - lo) * (k / (n - 1.0)) ** 1.15 for k in range(n)]
    for k, f in dips: out[k] *= f
    return out
# per map: gap between creatures (ms) and the composition of each wave; the targets come from ramp(...) (load on Easy, 1.0 = all the damage the gold could buy)
PLANS = {
 'mountains': (ramp(8, 0.07, 0.36, [(4, 0.8)]), [
   (1400, {'spider': 14}), (1100, {'spider': 16, 'warg': 6}), (900, {'warg': 24}), (900, {'spider': 30, 'gnoll': 8}),
   (1400, {'orc': 8}), (800, {'warg': 20, 'gnoll': 12}), (1000, {'wyvern': 8, 'orc': 6}), (1200, {'troll': 3, 'orc': 8, 'gnoll': 14})]),
 'maze': (ramp(7, 0.07, 0.36), [
   (450, {'spider': 60}), (400, {'spider': 70, 'warg': 25}), (380, {'warg': 60, 'gnoll': 20}), (900, {'ogre': 6, 'gnoll': 12}),
   (400, {'spider': 80, 'warg': 40, 'orc': 8}), (700, {'wyvern': 14, 'gnoll': 30}), (500, {'troll': 4, 'orc': 14, 'warg': 40, 'gnoll': 24})]),
 'woods': (ramp(8, 0.07, 0.33, [(5, 0.85)]), [
   (900, {'spider': 16}), (700, {'spider': 20, 'brood': 8}), (600, {'warg': 26}), (900, {'orc': 8, 'brood': 10}),
   (700, {'gnoll': 24, 'warg': 16}), (1000, {'ogre': 5, 'orc': 6}), (800, {'wyvern': 10, 'brood': 18}), (900, {'troll': 4, 'orc': 10, 'gnoll': 20})]),
 'river_side': (ramp(10, 0.06, 0.36, [(3, 0.8), (7, 0.85)]), [
   (900, {'spider': 14}), (800, {'spider': 18, 'warg': 8}), (700, {'warg': 20, 'gnoll': 8}), (1000, {'orc': 8}),
   (700, {'brood': 24, 'gnoll': 12}), (800, {'ogre': 6, 'warg': 20}), (700, {'wyvern': 10, 'gnoll': 16}), (900, {'orc': 12, 'brood': 14}),
   (800, {'troll': 4, 'ogre': 4, 'wyvern': 6}), (700, {'troll': 5, 'orc': 12, 'gnoll': 20, 'wyvern': 8})]),
 'swamp': (ramp(10, 0.06, 0.36, [(5, 0.85)]), [
   (900, {'spider': 16}), (800, {'brood': 14, 'spider': 14}), (700, {'brood': 26}), (900, {'gnoll': 16, 'brood': 14}),
   (800, {'ogre': 6}), (700, {'brood': 30, 'warg': 16}), (800, {'wyvern': 12, 'gnoll': 14}), (900, {'troll': 4, 'ogre': 4, 'brood': 16}),
   (700, {'orc': 14, 'gnoll': 24, 'wyvern': 6}), (800, {'troll': 5, 'ogre': 5, 'orc': 10, 'wyvern': 8, 'brood': 24})]),
 'tundra': (ramp(10, 0.06, 0.36, [(7, 0.85)]), [
   (450, {'warg': 60}), (400, {'warg': 80, 'spider': 40}), (400, {'gnoll': 60, 'warg': 40}), (800, {'orc': 10}),
   (400, {'warg': 70, 'gnoll': 40, 'orc': 8}), (400, {'warg': 90, 'gnoll': 50, 'orc': 10}), (700, {'troll': 6, 'wyvern': 12}),
   (400, {'warg': 100, 'gnoll': 60}), (600, {'troll': 6, 'orc': 24, 'wyvern': 10}), (400, {'warg': 90, 'gnoll': 60, 'orc': 16, 'troll': 8, 'wyvern': 8})]),
 'bigger': (ramp(10, 0.06, 0.31, [(7, 0.85)]), [
   (400, {'spider': 70, 'warg': 30}), (350, {'warg': 100}), (350, {'gnoll': 80, 'warg': 40}), (700, {'orc': 20, 'brood': 20}),
   (350, {'warg': 110, 'gnoll': 60, 'orc': 12}), (500, {'ogre': 14, 'orc': 20}), (400, {'wyvern': 30, 'gnoll': 60, 'brood': 40}),
   (350, {'warg': 120, 'gnoll': 80}), (500, {'troll': 10, 'orc': 30, 'wyvern': 16}), (350, {'warg': 120, 'gnoll': 90, 'orc': 24, 'troll': 10, 'ogre': 8, 'wyvern': 16})]),
 'coast': (ramp(10, 0.06, 0.31, [(7, 0.85)]), [
   (400, {'spider': 50, 'warg': 30, 'brood': 12}), (350, {'warg': 70, 'gnoll': 40}), (400, {'brood': 40, 'spider': 40, 'wyvern': 10}),
   (650, {'orc': 16, 'gnoll': 40, 'brood': 12}), (350, {'warg': 90, 'gnoll': 50, 'ogre': 6}), (450, {'wyvern': 24, 'orc': 14, 'brood': 30}),
   (450, {'ogre': 10, 'troll': 4, 'warg': 60, 'gnoll': 40}), (350, {'spider': 80, 'warg': 80, 'brood': 40, 'wyvern': 12}),
   (450, {'troll': 8, 'orc': 26, 'wyvern': 14, 'gnoll': 50}), (350, {'troll': 10, 'ogre': 8, 'orc': 24, 'wyvern': 16, 'warg': 100, 'gnoll': 70, 'brood': 30})]),
}
def armor_mult(a):   # an arrow at its best hits for 12: armour takes a flat part off, never more than three quarters
    return 12.0 / max(12.0 - a, 3.0) if a > 0 else 1.0
def sheet_attr(name): return MON[name]
# the very large hordes are thinned (and every creature made correspondingly tougher: the load stays the same): a hundred small creatures waste far more shots than the paper model says
COUNT_SCALE = {'bigger': 0.7, 'tundra': 0.8, 'coast': 0.7}
for m, (targets, plan) in PLANS.items():
    if ONLY and m not in ONLY: continue
    cs = COUNT_SCALE.get(m, 1.0)
    plan = [(sp, dict((t, max(1, int(round(c * cs)))) for t, c in comp.items())) for sp, comp in plan]
    mapdir = os.path.join(DATA, 'maps', m + '.xml')
    start, lives, tile, buildable, towers, _ = analyse(mapdir)
    arrow_only = [t for t in towers if not t['splash'] and t['name'].startswith('Arrow')]
    gold = start
    waves = []
    for k, ((spacing, comp), target) in enumerate(zip(plan, targets)):
        n = sum(comp.values())
        slow = min(MON[t][3] for t in comp)
        flyers = any(MON[t][6].get('flying') for t in comp)
        rho, nm, lv, rad, cost = best(arrow_only if flyers and False else towers, tile, slow * spacing / 1000.0, k)
        if flyers:   # the flyers can only be hit by the arrow, freeze and ballista towers: count a third of them as out of reach of the ground artillery
            rho = rho * 0.8
        duration = (n - 1) * spacing / 1000.0 + 2 * rad / slow
        capacity = rho * gold * duration
        weight = sum(c * MON[t][2] * armor_mult(MON[t][6].get('armor', 0)) for t, c in comp.items())
        hp_s = max(3.0, target * capacity / weight)
        es = []
        for t, c in comp.items():
            sheet, icon, ratio, speed, anim, money, extra = MON[t]
            es.append((t, sheet, icon, max(2, int(round(hp_s * ratio))), money, speed, anim, c, extra))
        waves.append((spacing, es))
        gold += sum(e[4] * e[7] for e in es) * gold_factor(1, k, len(plan))
    with open(os.path.join(mapdir, 'waves.xml'), 'w') as f:
        f.write("<?xml version='1.0'?>\n  <enemies>\n    <waves>\n")
        for spacing, es in waves:
            f.write('\n    \t<wave enemydistance="%d">\n' % spacing)
            for (t, sheet, icon, hp, money, speed, anim, c, extra) in es:
                ex = ''.join(' %s="%s"' % (k2, v) for k2, v in sorted(extra.items()))
                if 'damage' not in extra: ex += ' damage="0"'
                f.write('    \t\t<enemy name="%s" charset="%s" icon="%s" tilesize="64" maxhealth="%d" money="%d" movespeed="%d" animationspeed="%d" count="%d"%s>\n            </enemy>\n' % (t, sheet, icon, hp, money, speed, anim, c, ex))
            f.write('        </wave>\n')
        f.write("    </waves>\n  </enemies>\n")
    print('%-11s %d waves, %d creatures' % (m, len(waves), sum(e[7] for _, es in waves for e in es)))
