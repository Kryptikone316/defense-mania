#!/usr/bin/env python3
"""Is every map winnable? For each map and difficulty it works out, wave by wave, the most damage the gold could possibly buy and
deliver, and compares it with the health of the wave (health is multiplied by the difficulty: 1, 2, 3).
Usage: check_maps.py <the game's data folder>

Model (an upper bound for the player, so a ratio above 1.0 means the wave cannot be killed):
 * gold at the start of a wave = start money + the kill gold of every earlier wave (all of it killed), all spent on the tower
   with the best damage-per-second per gold (counting its upgrades, and splash damage on the neighbours the wave's spacing allows)
 * those towers are shooting for the whole time the wave is in reach: (n - 1) x spacing + the time the slowest creature needs
   to cross a tower's reach (2 x radius / speed); the radius the game really uses is fireRadius / 2 + tileSize / 4
 * 'ratio' = wave health / that capacity. About 0.2 or less is comfortable, 0.3-0.4 asks for sensible placement, above 0.6 for
   near-perfect play, above 1.0 is impossible. Leaks are free until the lives run out (every creature that gets through costs one life)."""
import math, os, re, sys
import xml.etree.ElementTree as ET

DATA = sys.argv[1]
GOLD_STEP, GOLD_GROWTH = 0.10, 0.8   # the same as gold_difficulty_step / gold_wave_growth in defensemania.cfg (0, 0 = the original flat reward)

def gold_factor(d, k, n):
    return (1 + GOLD_STEP * (d - 1)) * (1 + (GOLD_GROWTH * k / (n - 1) if n > 1 else 0))

def load(path):
    txt = open(path, encoding='utf-8', errors='replace').read()
    return ET.fromstring(re.sub(r'^\s*<\?xml[^>]*\?>', '', txt).strip())

def towers_of(m):
    t = []
    for e in load(os.path.join(m, 'towers.xml')).iter('tower'):
        a = e.attrib
        lv = [(int(a['costs']), int(a['damage']), int(a['firerate']), int(a['fireradius']))]
        cost = int(a['costs'])
        for u in range(1, int(a.get('updates', '0')) + 1):
            cost += int(a['u%d-costs' % (u - 1)]) if False else 0
        # upgrade u costs u{u-1}... the file names the cost of upgrade i as u<i>-costs: u1 = first upgrade
        cum = int(a['costs'])
        for u in range(1, int(a.get('updates', '0')) + 1):
            cum += int(a.get('u%d-costs' % u, '0'))
            lv.append((cum, int(a.get('u%d-damage' % u, a['damage'])), int(a.get('u%d-firerate' % u, a['firerate'])), int(a.get('u%d-fireradius' % u, a['fireradius']))))
        t.append(dict(name=a['name'], size=int(a['tilesize']), splash=a.get('splash') == '1', sr=int(a.get('splashradius', '0')), sf=float(a.get('splashfactor', '0')),
                      freeze=float(a.get('freeze', '0')), levels=lv))
    return t

def analyse(mapdir):
    tree = load(os.path.join(mapdir, 'maptree.xml'))
    game = tree.find('game')
    start, lives = int(game.attrib['startmoney']), int(game.attrib['lifes'])
    tile = int(tree.find('tilesize').attrib['s'])
    kinds = tree.find('levels').findall('level')[2].text.strip()
    cols = [[tuple(int(v) for v in c.split(',')) for c in col.split('#')] for col in kinds.split('_')]
    buildable = sum(1 for col in cols for c in col if c[0] not in (1, 3, 4, 5))
    towers = towers_of(mapdir)
    waves = []
    for w in load(os.path.join(mapdir, 'waves.xml')).iter('wave'):
        es = [(int(e.attrib['maxhealth']), int(e.attrib['money']), int(e.attrib['movespeed']), int(e.attrib['count'])) for e in w.iter('enemy')]
        waves.append((int(w.attrib['enemydistance']), es))
    return start, lives, tile, buildable, towers, waves

def best(towers, tile, spacing_px_by_wave, wave_i):
    """best damage per second per gold over the towers and their levels; returns (ratio, name, level, radius, cost, spots-needed-per-gold)"""
    out = (0, '', 0, 0, 0)
    for t in towers:
        for li, (cost, dmg, fr, rad) in enumerate(t['levels']):
            dps = dmg * 1000.0 / fr
            if t['splash']:
                neigh = max(0.0, 2 * t['sr'] / max(1.0, spacing_px_by_wave) - 0.0)
                dps *= 1 + t['sf'] * min(neigh, 6)
            r = dps / cost
            if r > out[0]:
                out = (r, t['name'], li, rad / 2 + t['size'] / 4, cost)
    return out

def simulate(start, lives, towers, tile, waves, d, e, boost=1.0):
    """play the waves with a fraction e of the ideal capacity; returns the leaks, or None if the lives ran out"""
    gold, leaks = start, 0
    for k, (spacing, es) in enumerate(waves):
        n = sum(x[3] for x in es)
        slow = min(x[2] for x in es)
        rho, name, lvl, rad, cost = best(towers, tile, slow * spacing / 1000.0, k)
        duration = (n - 1) * spacing / 1000.0 + 2 * rad / slow
        cap = boost * e * rho * gold * duration
        hp = sum(x[0] * x[3] for x in es) * d
        f = min(1.0, cap / hp)
        gold_wave = sum(x[1] * x[3] for x in es)
        leaks += math.ceil(n * (1 - f) - 1e-9)
        if leaks > lives - 1:
            return None
        gold += gold_wave * f * gold_factor(d, k, len(waves))
    return leaks

def needed(start, lives, towers, tile, waves, d, boost=1.0):
    """the smallest fraction of the ideal capacity that still wins (None: not even the ideal does)"""
    if simulate(start, lives, towers, tile, waves, d, 1.0, boost) is None:
        return None
    lo, hi = 0.01, 1.0
    for _ in range(30):
        mid = (lo + hi) / 2
        if simulate(start, lives, towers, tile, waves, d, mid, boost) is None: lo = mid
        else: hi = mid
    return hi

maps = [l.strip() for l in re.findall(r'url="([^"]+)"', open(os.path.join(DATA, 'maps.xml')).read())]
worst_all = []
boosted = []
for url in maps:
    mapdir = os.path.join(DATA, 'maps', url)
    start, lives, tile, buildable, towers, waves = analyse(mapdir)
    print('== %s: start gold %d, lives %d, %d buildable tiles, towers: %s' % (url[:-4], start, lives, buildable, ', '.join('%s(%d)' % (t['name'].split()[0], t['levels'][-1][0]) for t in towers)))
    for d in (1, 2, 3):
        gold = start
        rows = []
        for k, (spacing, es) in enumerate(waves):
            n = sum(x[3] for x in es)
            slow = min(x[2] for x in es)
            rho, name, lvl, rad, cost = best(towers, tile, slow * spacing / 1000.0, k)
            duration = (n - 1) * spacing / 1000.0 + 2 * rad / slow
            cap = rho * gold * duration
            hp = sum(x[0] * x[3] for x in es) * d
            rows.append((k + 1, hp / cap, hp, n, gold, name.split()[0], lvl, gold / cost))
            gold += sum(x[1] * x[3] for x in es) * gold_factor(d, k, len(waves))
        top = max(rows, key=lambda r: r[1])
        need = needed(start, lives, towers, tile, waves, d)
        hasfreeze = any(t['freeze'] > 0 for t in towers)
        boost = (1.6 if hasfreeze else 1.0) * 1.15   # slowed creatures stay in reach longer; kills pay for towers during the wave
        need2 = needed(start, lives, towers, tile, waves, d, boost)
        boosted.append((url[:-4], d, need2))
        verdict = 'IMPOSSIBLE' if need is None else 'near-perfect play' if need > 0.6 else 'demanding' if need > 0.4 else 'fair' if need > 0.25 else 'comfortable'
        worst_all.append((url[:-4], d, top[1], top[0], need, verdict))
        print('  difficulty %d: hardest wave %2d (ratio %.2f) | needs %s of the ideal damage to win with %d lives -> %s' % (d, top[0], top[1], ('%.0f%%' % (need * 100)) if need else 'more than 100%', lives, verdict))
        if d == 1:
            print('     ratio per wave: ' + ' '.join('%d:%.2f' % (r[0], r[1]) for r in rows))
print()
print('summary: share of the ideal (best-possible) damage the player must reach to win; lower is easier')
print('  %-12s %-12s %-12s %-12s' % ('map', 'easy', 'normal', 'hard'))
for url in maps:
    m = url[:-4]
    cells = []
    for d in (1, 2, 3):
        r = [x for x in worst_all if x[0] == m and x[1] == d][0]
        cells.append(('%3.0f%% %s' % (r[4] * 100, r[5].split()[0])) if r[4] else 'IMPOSSIBLE')
    print('  %-12s %-12s %-12s %-12s' % (m, cells[0], cells[1], cells[2]))
print()
print('the same with the two things the model leaves out: Freeze towers slow creatures to 60% (x1.6 time in reach, only on maps that have one) and kill gold paid during a wave (x1.15)')
print('  %-12s %-12s %-12s %-12s' % ('map', 'easy', 'normal', 'hard'))
for url in maps:
    m = url[:-4]
    cells = []
    for d in (1, 2, 3):
        r = [x for x in boosted if x[0] == m and x[1] == d][0]
        cells.append(('%3.0f%%' % (r[2] * 100)) if r[2] else 'IMPOSSIBLE')
    print('  %-12s %-12s %-12s %-12s' % (m, cells[0], cells[1], cells[2]))
