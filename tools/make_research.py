#!/usr/bin/env python3
"""Writes <data>/research.xml: the Bastion's research lines (see DEFENSEMANIA-BASTION-DESIGN in the notes). One table, edited here.
Each line has three tiers; a tier is (name, cost in gold, waves it takes, text shown, effects). Effects (all by name):
  towers:  damage rate reach splash (percent), chill (points more extra damage on slowed creatures), slow (taken off the slow factor, 0.1 = 10 % slower),
           slowtime (percent), ramp (added to the damage ramp), suprange (percent), supheal (added percent of health per second), supgold (added gold per kill)
  traps:   trapdamage trapcharges trapduration traphealth (percent), trapcap (more allowed), trapslow (taken off the slow factor)
  keep:    lives (extra lives); specials: cooldown (seconds) power time (seconds)
  garrison: soldiers sent from the keep: count (how many), health and damage (percent more), time (seconds they stay), cooldown (seconds), zone (pixels of the path before the keep they guard)
Doctrines: every path (towers, traps, garrison, command) pays a bonus for the tiers finished in it, however they are spread over its lines: see Map::doctrineLevel
Usage: make_research.py <the game's data folder>"""
import os, sys
DATA = sys.argv[1]
C = (100, 200, 350)
W = (1, 2, 3)
def T(name, text, **eff): return (name, text, eff)
LINES = [
 # (id, group, target, title, [tiers])
 ('arrow', 'tower', 'Arrow Tower', 'Arrow Tower', [T('Sharpened Heads', '+12% damage', damage=12), T('Fletching', '+12% fire rate', rate=12), T('Longbows', '+15% reach', reach=15)]),
 ('catapult', 'tower', 'Catapult Tower', 'Catapult Tower', [T('Heavier Stones', '+15% splash radius', splash=15), T('Counterweights', '+12% damage', damage=12), T('Long Arm', '+15% reach', reach=15)]),
 ('freeze', 'tower', 'Freeze Tower', 'Freeze Tower', [T('Brittle Frost', 'Slowed creatures take 5 points more damage', chill=5), T('Deep Chill', 'Slow is 10% stronger', slow=0.1), T('Lingering Cold', 'Slow lasts 40% longer', slowtime=40)]),
 ('canon', 'tower', 'Canon Tower', 'Canon Tower', [T('Better Powder', '+12% damage', damage=12), T('Quick Match', '+12% fire rate', rate=12), T('Grapeshot', '+20% splash radius', splash=20)]),
 ('ballista', 'tower', 'Ballista Tower', 'Ballista Tower', [T('Torsion Springs', 'Damage ramps up faster', ramp=0.15), T('Iron Bolts', '+15% damage', damage=15), T('Siege Sights', '+15% reach', reach=15)]),
 ('depot', 'tower', 'Resupply Depot', 'Resupply Depot', [T('Wider Camp', '+20% area', suprange=20), T('Field Surgeons', 'Heals 0.5% more per second', supheal=0.5), T('Quartermaster', '+1 gold per kill in its area', supgold=1)]),
 ('spikes', 'trap', 'spikes', 'Spikes', [T('Barbed Iron', '+25% damage', trapdamage=25), T('Spare Stakes', 'One more allowed on the map', trapcap=1), T('Hardened Steel', '+40% charges', trapcharges=40)]),
 ('glue', 'trap', 'glue', 'Glue', [T('Sticky Pitch', 'Slows 10% harder', trapslow=0.1), T('Spare Barrels', 'One more allowed on the map', trapcap=1), T('Slow Drying', 'Lasts 40% longer', trapduration=40)]),
 ('boulder', 'trap', 'boulder', 'Boulder', [T('Granite', '+30% health', traphealth=30), T('Spare Rocks', 'One more allowed on the map', trapcap=1), T('Mountain Stone', '+40% health', traphealth=40)]),
 ('fortify', 'keep', 'fortify', 'Fortify', [T('Stout Gate', '+1 extra life', lives=1), T('Iron Portcullis', '+1 more extra life', lives=1), T('Hold the Line', '+1 more extra life', lives=1)]),
 ('volley', 'keep', 'volley', 'Volley', [T('Archers on the Wall', 'Every creature near the keep takes 12% of its health, every 90 s', cooldown=90, power=12), T('Massed Volley', '20%, every 70 s', cooldown=70, power=20), T('Rain of Arrows', '30%, every 50 s', cooldown=50, power=30)]),
 ('rally', 'keep', 'rally', 'Rally', [T('Battle Cry', 'Towers fire 25% faster for 10 s, every 80 s', cooldown=80, power=25, time=10), T('War Drums', '35%, every 60 s', cooldown=60, power=35, time=10), T('Banner of Kings', '50%, every 45 s', cooldown=45, power=50, time=10)]),
 ('mend', 'keep', 'mend', 'Mend', [T('Field Repairs', 'Heals every tower by 40%, every 90 s', cooldown=90, power=40), T('Master Masons', '70%, every 70 s', cooldown=70, power=70), T('Rebuilt Overnight', '100%, every 50 s', cooldown=50, power=100)]),
 ('frost', 'keep', 'frost', 'Frost Horn', [T('Horn of Winter', 'Slows every creature to 60% for 6 s, every 100 s', cooldown=100, power=60, time=6), T('Howling Gale', '50% for 8 s, every 80 s', cooldown=80, power=50, time=8), T('Wintertide', '40% for 10 s, every 60 s', cooldown=60, power=40, time=10)]),
 ('vermin', 'garrison', 'vermin', 'Vermin Hunters', [T('Rat Catchers', '2 hunters guard 350 px of path for 35 s, every 100 s', count=2, time=35, cooldown=100, zone=350), T('Hunters\' Guild', '3 hunters, 25% tougher, 450 px, 45 s, every 80 s', count=3, health=25, time=45, cooldown=80, zone=450), T('Exterminators', '4 hunters, 50% tougher, 20% harder, 550 px, 55 s, every 65 s', count=4, health=50, damage=20, time=55, cooldown=65, zone=550)]),
 ('giant', 'garrison', 'giant', 'Giant Slayers', [T('Pike Squad', '2 slayers guard 350 px of path for 35 s, every 100 s', count=2, time=35, cooldown=100, zone=350), T('Giantbane Company', '3 slayers, 25% tougher, 450 px, 45 s, every 80 s', count=3, health=25, time=45, cooldown=80, zone=450), T('Titan Breakers', '4 slayers, 50% tougher, 20% harder, 550 px, 55 s, every 65 s', count=4, health=50, damage=20, time=55, cooldown=65, zone=550)]),
 ('sky', 'garrison', 'sky', 'Skyguard', [T('Slingers', '2 skyguards guard 350 px of path for 35 s, every 100 s', count=2, time=35, cooldown=100, zone=350), T('Longbow Wing', '3 skyguards, 25% tougher, 450 px, 45 s, every 80 s', count=3, health=25, time=45, cooldown=80, zone=450), T('Skyguard Elite', '4 skyguards, 50% tougher, 20% harder, 550 px, 55 s, every 65 s', count=4, health=50, damage=20, time=55, cooldown=65, zone=550)]),
 ('roller', 'garrison', 'roller', 'Boulder Rollers', [T('Rolling Stone', '1 boulder, every 110 s', count=1, cooldown=110), T('Heavy Boulder', '1 boulder, 40% sturdier, every 90 s', count=1, health=40, cooldown=90), T('Twin Boulders', '2 boulders, 60% sturdier, every 75 s', count=2, health=60, cooldown=75)]),
]
def esc(s): return s.replace('&', '&amp;').replace('"', '&quot;').replace('<', '&lt;')
out = ["<?xml version='1.0'?>\n  <research>\n"]
for lid, group, target, title, tiers in LINES:
    out.append('    <line id="%s" group="%s" target="%s" title="%s">\n' % (lid, group, esc(target), esc(title)))
    for i, (name, text, eff) in enumerate(tiers):
        attrs = ' '.join('%s="%s"' % (k, v) for k, v in sorted(eff.items()))
        out.append('      <tier name="%s" cost="%d" waves="%d" %s>%s</tier>\n' % (esc(name), C[i], W[i], attrs, esc(text)))
    out.append('    </line>\n')
out.append('  </research>\n')
open(os.path.join(DATA, 'research.xml'), 'w').write(''.join(out))
print('research.xml:', len(LINES), 'lines')
