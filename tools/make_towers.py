#!/usr/bin/env python3
"""Writes <data>/maps/<map>.xml/towers.xml for every map from ONE table of tower stats, so a tower is the same on every map.
Costs of upgrades (u1-costs, u2-costs) are what the upgrade costs, not totals. The pictures are shared (<data>/towers/).
Usage: make_towers.py <the game's data folder>"""
import os, sys
DATA = sys.argv[1]
PIC = '../../../towers/'   # from <data>/maps/<map>.xml/data/ to <data>/towers/

# name, picture number, description, base stats, upgrades (cost, damage, firerate, fireradius) x2, extras
TOWERS = {
 'Arrow Tower':   dict(n=1, text='moderate firerate, moderate damage', hp=150, cost=25, dmg=3, rate=500, rad=150, anim=0, expl=1,
                       up=[(30, 8, 400, 170), (40, 12, 350, 190)]),
 'Catapult Tower': dict(n=2, text='Low firerate, splash damage', hp=300, cost=85, dmg=8, rate=1500, rad=200, anim=1, expl=0,
                       splash=(60, 0.3), up=[(95, 10, 1400, 220), (105, 12, 1300, 250)]),
 'Freeze Tower':  dict(n=3, text='High firerate, freezing enemies', hp=200, cost=55, dmg=1, rate=500, rad=150, anim=0, expl=0, freeze=0.6,
                       up=[(65, 2, 400, 180), (85, 4, 250, 220)]),
 'Canon Tower':   dict(n=4, text='High firerate, splash damage', hp=300, cost=95, dmg=4, rate=500, rad=100, anim=1, expl=0,
                       splash=(60, 0.3), up=[(105, 6, 450, 120), (125, 9, 350, 150)]),
 'Resupply Depot': dict(n=6, text='Supplies the towers around it. No weapon.', hp=200, cost=70, dmg=0, rate=1000, rad=0, anim=0, expl=0,
                       support=dict(range=[96, 112], rate=[10, 20], heal=[0.5, 1.0], gold=[1, 2], goldpct=[0, 0]), up=[(90, 0, 1000, 0)]),
 'Ballista Tower': dict(n=5, text='Long range, every shot on the same target hits harder', hp=300, cost=100, dmg=6, rate=1000, rad=200, anim=1, expl=1,
                       ramp=1.5, up=[(150, 12, 800, 250), (170, 18, 650, 280)]),
}
# which towers each map offers, in the order of the build menu
ROSTER = {
 'mountains': ['Arrow Tower', 'Catapult Tower', 'Freeze Tower', 'Canon Tower', 'Ballista Tower', 'Resupply Depot'],
 'maze':      ['Arrow Tower', 'Catapult Tower', 'Freeze Tower', 'Canon Tower', 'Ballista Tower', 'Resupply Depot'],
 'woods':     ['Arrow Tower', 'Catapult Tower', 'Freeze Tower', 'Canon Tower', 'Ballista Tower', 'Resupply Depot'],
 'river_side': ['Arrow Tower', 'Catapult Tower', 'Freeze Tower', 'Canon Tower', 'Ballista Tower', 'Resupply Depot'],
 'swamp':     ['Arrow Tower', 'Catapult Tower', 'Freeze Tower', 'Canon Tower', 'Ballista Tower', 'Resupply Depot'],
 'tundra':    ['Arrow Tower', 'Catapult Tower', 'Freeze Tower', 'Canon Tower', 'Ballista Tower', 'Resupply Depot'],
 'bigger':    ['Arrow Tower', 'Catapult Tower', 'Freeze Tower', 'Canon Tower', 'Ballista Tower', 'Resupply Depot'],
}
def line(name, t):
    sp = t.get('splash')
    a = ['name="%s"' % name, 'damageincrease="%s"' % t.get('ramp', 0), 'explosionatenemy="%d"' % t['expl'], 'hasshootinganimation="%d"' % t['anim'],
         'charset="%stower%d.png"' % (PIC, t['n']), 'towericon="%stower_icon%d.png"' % (PIC, t['n']), 'towericonsize="30"', 'tilesize="64"',
         'damage="%d"' % t['dmg'], 'firerate="%d"' % t['rate'], 'fireradius="%d"' % t['rad'],
         'splash="%d"' % (1 if sp else 0), 'splashradius="%d"' % (sp[0] if sp else 0), 'splashfactor="%s"' % (sp[1] if sp else 0),
         'costs="%d"' % t['cost'], 'freeze="%s"' % t.get('freeze', 0), 'maxhealth="%d"' % t['hp'], 'updates="%d"' % len(t['up'])]
    for i, (c, d, r, f) in enumerate(t['up'], 1):
        a += ['u%d-costs="%d"' % (i, c), 'u%d-damage="%d"' % (i, d), 'u%d-firerate="%d"' % (i, r), 'u%d-fireradius="%d"' % (i, f)]
    su = t.get('support')
    if su:
        a += ['support="1"']
        for lvl in range(len(su['range'])):
            pre = '' if lvl == 0 else 'u%d-' % lvl
            a += ['%saurarange="%s"' % (pre, su['range'][lvl]), '%ssupportrate="%s"' % (pre, su['rate'][lvl]), '%ssupportheal="%s"' % (pre, su['heal'][lvl]),
                  '%ssupportgold="%s"' % (pre, su['gold'][lvl]), '%ssupportgoldpct="%s"' % (pre, su['goldpct'][lvl])]
    return '      <tower %s>%s</tower>\n' % (' '.join(a), t['text'])
for m, names in ROSTER.items():
    with open(os.path.join(DATA, 'maps', m + '.xml', 'towers.xml'), 'w') as f:
        f.write("<?xml version='1.0'?>\n  <towersettings>\n    <towers>\n")
        for n in names:
            f.write('\n' + line(n, TOWERS[n]))
        f.write('\n    </towers>\n  </towersettings>')
    print('wrote', m, len(names), 'towers')
