"""Renders Defense Mania's four soldiers from low-poly Blender models.
Run:  blender -b -P soldiers.py -- <output folder> [kind ...]
Writes <kind>_<frame>_<direction>.png (256 x 256, transparent). frame 0-2 walk, 3-5 strike, 6-8 dying (stagger, tipping, down); direction 0 down (faces the player), 1 left, 2 right, 3 up,
the same order the monster sheets use. tools/make_soldiers.py packs them into 64 px sheets."""
import bpy, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lib

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
OUT = argv[0]
KINDS = argv[1:] or ['vermin', 'giant', 'sky', 'roller']
os.makedirs(OUT, exist_ok=True)
RES = 256
SKIN = (0.91, 0.74, 0.58)
METAL = (0.70, 0.72, 0.76)
LEATHER = (0.30, 0.20, 0.12)

def rad(a): return math.radians(a)

class Rig: pass

def humanoid(c, wide, tunic, legs, boot, hair, scale):
    """a plain figure facing -Y (towards the camera): returns its movable parts"""
    r = Rig()
    r.root = lib.pivot('root')
    r.root.scale = (scale, scale, scale)
    r.fall = lib.pivot('fall', (0, 0, 0), r.root)   # tipping the whole figure over about its feet is how it dies
    M = lambda n, col, rough=0.75: lib.mat(n, col, rough)
    tm, lm, bm, sm, hm = M('tunic', tunic), M('legs', legs), M('boot', boot), M('skin', SKIN, 0.6), M('hair', hair)
    r.legs = []
    for sx in (-1, 1):
        p = lib.pivot('leg', (sx * 0.12, 0, 0.74), r.fall)
        lib.cyl(lm, 0.095, 0.64, (0, 0, -0.32), parent=p, verts=8)
        lib.box(bm, (0.17, 0.30, 0.13), (0, -0.05, -0.70), parent=p, bevel=0.02)
        r.legs.append(p)
    r.torso = lib.pivot('torso', (0, 0, 0.74), r.fall)
    lib.box(tm, (wide + 0.04, 0.32, 0.56), (0, 0, 0.30), parent=r.torso, bevel=0.04)
    lib.box(lib.mat('belt', LEATHER), (wide + 0.02, 0.32, 0.08), (0, 0, 0.05), parent=r.torso)
    lib.ball(sm, 0.215, (0, 0, 0.80), (1, 1, 1.02), parent=r.torso)
    lib.ball(lib.mat('eye', (0.08, 0.06, 0.05)), 0.036, (-0.08, -0.195, 0.82), parent=r.torso, seg=6, rings=4)
    lib.ball(lib.mat('eye', (0.08, 0.06, 0.05)), 0.036, (0.08, -0.195, 0.82), parent=r.torso, seg=6, rings=4)
    r.hair = hm
    r.arms = []
    for sx in (-1, 1):
        p = lib.pivot('arm', (sx * (wide / 2 + 0.09), 0, 0.52), r.torso)
        lib.cyl(tm, 0.08, 0.30, (0, 0, -0.15), parent=p, verts=8)
        lib.cyl(sm, 0.065, 0.26, (0, 0, -0.41), parent=p, verts=8)
        lib.ball(sm, 0.085, (0, 0, -0.56), parent=p, seg=8, rings=6)
        r.arms.append(p)
    r.hand = [pivot_at(p) for p in r.arms]
    return r

def pivot_at(arm):
    return lib.pivot('hand', (0, 0, -0.56), arm)

def build(kind):
    if kind == 'vermin':
        r = humanoid(None, 0.40, (0.36, 0.54, 0.26), (0.34, 0.25, 0.16), (0.22, 0.15, 0.09), (0.22, 0.38, 0.17), 0.92)
        g = lib.mat('hood', (0.24, 0.42, 0.18))
        lib.ball(g, 0.25, (0, 0.03, 0.86), (1, 1.05, 0.88), parent=r.torso)           # a hood over the head
        lib.cone(g, 0.14, 0.30, (0, 0.13, 1.06), (-25, 0, 0), parent=r.torso)
        lib.box(lib.mat('pelt', (0.45, 0.33, 0.2)), (0.50, 0.36, 0.1), (0, 0, 0.56), parent=r.torso)   # a fur collar
        m = lib.mat('steel', METAL, 0.35, 0.8)
        w = r.hand[1]                                                                 # a cleaver in the right hand
        lib.cyl(lib.mat('grip', LEATHER), 0.025, 0.28, (0, 0, 0.06), parent=w, verts=6)
        lib.box(m, (0.02, 0.14, 0.24), (0, -0.01, 0.30), parent=w, bevel=0.005)
        w2 = r.hand[0]                                                                # and a short knife in the left
        lib.cyl(lib.mat('grip', LEATHER), 0.022, 0.20, (0, 0, 0.03), parent=w2, verts=6)
        lib.cone(m, 0.04, 0.24, (0, 0, 0.24), parent=w2, verts=4)
        r.weapon, r.weapon2 = w, w2
    elif kind == 'giant':
        r = humanoid(None, 0.50, (0.66, 0.20, 0.17), (0.28, 0.22, 0.2), (0.15, 0.12, 0.1), (0.2, 0.2, 0.22), 1.08)
        st = lib.mat('steel', METAL, 0.35, 0.8)
        lib.box(lib.mat('stripe', (0.92, 0.88, 0.78)), (0.14, 0.34, 0.54), (0, -0.005, 0.30), parent=r.torso)   # a pale stripe down the tabard
        lib.ball(st, 0.25, (0, 0, 0.86), (1, 1.05, 0.95), parent=r.torso)                  # an iron helm
        lib.box(st, (0.06, 0.06, 0.18), (0, -0.23, 0.80), parent=r.torso)                  # nose guard
        lib.cone(st, 0.06, 0.16, (0, 0, 1.12), (0, 0, 0), parent=r.torso, verts=6)
        for sx in (-1, 1):                                                                 # pauldrons
            lib.ball(st, 0.17, (sx * 0.38, 0, 0.56), (1, 1, 0.7), parent=r.torso, seg=10, rings=6)
        w = r.hand[1]                                                                      # a pike
        lib.cyl(lib.mat('shaft', (0.5, 0.36, 0.2)), 0.028, 1.30, (0, 0, 0.25), parent=w, verts=6)
        lib.cone(st, 0.07, 0.30, (0, 0, 1.05), parent=w, verts=4)
        lib.box(st, (0.2, 0.03, 0.03), (0, 0, 0.88), parent=w)
        sh = r.arms[0]                                                                     # a round shield on the left arm
        lib.cyl(lib.mat('wood', (0.52, 0.34, 0.18)), 0.30, 0.07, (-0.10, -0.12, -0.42), (90, 0, 0), parent=sh, verts=14)
        lib.cyl(st, 0.31, 0.03, (-0.10, -0.16, -0.42), (90, 0, 0), parent=sh, verts=14)
        lib.ball(st, 0.09, (-0.10, -0.20, -0.42), (1, 0.6, 1), parent=sh, seg=8, rings=6)
        r.weapon, r.weapon2 = w, None
    elif kind == 'sky':
        r = humanoid(None, 0.38, (0.27, 0.42, 0.70), (0.26, 0.26, 0.34), (0.18, 0.14, 0.1), (0.18, 0.28, 0.5), 0.96)
        b = lib.mat('hood', (0.20, 0.32, 0.58))
        lib.ball(b, 0.25, (0, 0.03, 0.87), (1, 1.05, 0.9), parent=r.torso)
        lib.cone(b, 0.15, 0.36, (0, 0.14, 1.08), (-35, 0, 0), parent=r.torso)               # a pointed hood
        lib.box(b, (0.44, 0.08, 0.7), (0, 0.17, 0.2), (8, 0, 0), parent=r.torso)            # a cloak down the back
        lib.box(lib.mat('pouch', LEATHER), (0.14, 0.08, 0.14), (0.2, -0.17, 0.12), parent=r.torso)
        w = r.hand[1]                                                                      # a sling: cord and a stone
        lib.cyl(lib.mat('cord', (0.7, 0.6, 0.4)), 0.012, 0.50, (0, 0, 0.25), parent=w, verts=5)
        lib.ball(lib.mat('stone', (0.55, 0.55, 0.58)), 0.065, (0, 0, 0.54), parent=w, seg=8, rings=6)
        r.weapon, r.weapon2 = w, None
    else:   # roller
        r = humanoid(None, 0.56, (0.40, 0.28, 0.17), (0.34, 0.28, 0.2), (0.2, 0.15, 0.1), (0.28, 0.2, 0.12), 1.06)
        lib.box(lib.mat('band', (0.7, 0.16, 0.12)), (0.45, 0.45, 0.08), (0, 0, 0.93), parent=r.torso)   # a headband
        lib.ball(lib.mat('hair', (0.28, 0.2, 0.12)), 0.22, (0, 0.05, 0.86), (1, 1, 0.8), parent=r.torso)
        for sx in (-1, 1):
            lib.ball(lib.mat('skin', SKIN, 0.6), 0.13, (sx * 0.37, 0, 0.56), parent=r.torso, seg=8, rings=6)
        r.weapon, r.weapon2 = None, None
    return r

WALK = [(-26, 26, 28, -28), (0, 0, 0, 0), (26, -26, -28, 28)]   # legL, legR, armL, armR swing for the three walking frames

def die_pose(kind, r, f, side=False):
    """f 0-2 of the fall: stagger, tipping over, down. The figure falls backwards (away from the way it faces)."""
    t, z = ((-14, 0.0), (-62, 0.10), (-90, 0.17))[f]
    r.fall.rotation_euler = (0, rad(-t), 0) if side else (rad(t), 0, 0)   # facing the camera or away from it, a figure goes over sideways, so it reads as lying down
    r.fall.location = (0, 0, z)
    legs = ((10, -6), (22, 12), (8, -4))[f]
    arms = ((-30, 40), (60, 75), (30, 50))[f]
    r.legs[0].rotation_euler = (rad(legs[0]), 0, 0); r.legs[1].rotation_euler = (rad(legs[1]), 0, 0)
    r.arms[0].rotation_euler = (rad(arms[0]), 0, 0); r.arms[1].rotation_euler = (rad(arms[1]), 0, 0)
    r.torso.rotation_euler = (rad((-8, -4, 0)[f]), 0, 0)
    drop = (110, 140, 100)[f]   # weapons fall loose
    if r.weapon is not None: r.weapon.rotation_euler = (rad(drop - arms[1]), 0, 0)
    if r.weapon2 is not None: r.weapon2.rotation_euler = (rad(drop - arms[0]), 0, 0)

def pose(kind, r, f, d=1):
    """f 0-2 walk, 3-5 strike.  Angles in degrees about X: negative swings a limb forward (towards the camera)."""
    if f >= 6:
        die_pose(kind, r, f - 6, d in (0, 3))
        return
    r.fall.rotation_euler = (0, 0, 0); r.fall.location = (0, 0, 0)
    legL, legR, armL, armR, lean = 0, 0, 0, 0, 0
    wt, wt2 = 0, 0   # where the weapon points: 0 straight up, 90 forward, 180 down
    if f < 3:
        legL, legR, armL, armR = WALK[f]
    sw = (1, 0, -1)[f] if f < 3 else 0
    if kind == 'vermin':
        if f < 3: armR, wt, armL, wt2 = -55 + sw * 8, 25, -35 - sw * 8, 20
        else: armR, wt, armL, wt2 = (-150, -20, -60)[f - 3], (-30, 115, 60)[f - 3], -50, 30
    elif kind == 'giant':
        if f < 3: armR, wt, armL = -40 + sw * 6, 20 - sw * 4, -30
        else: armR, wt, armL = (-15, -85, -60)[f - 3], (75, 88, 80)[f - 3], -35
    elif kind == 'sky':
        if f < 3: armR, wt, armL = -25 + sw * 6, 175, armL
        else: armR, wt, armL = (-150, -165, -105)[f - 3], (-60, 0, 80)[f - 3], -20
    else:
        lean = 12
        armL = armR = (-78 + sw * 6) if f < 3 else (-100, -62, -80)[f - 3]
        if f >= 3: legL, legR = (14, -14, 0)[f - 3], (-14, 14, 0)[f - 3]
    r.legs[0].rotation_euler = (rad(legL), 0, 0)
    r.legs[1].rotation_euler = (rad(legR), 0, 0)
    r.arms[0].rotation_euler = (rad(armL), 0, 0)
    r.arms[1].rotation_euler = (rad(armR), 0, 0)
    r.torso.rotation_euler = (rad(lean), 0, 0)
    # the weapon pivot is a child of the arm: its own angle is what is wanted minus the arm's, so the weapon points where wt says
    if r.weapon is not None: r.weapon.rotation_euler = (rad(wt - armR - lean), 0, 0)
    if r.weapon2 is not None: r.weapon2.rotation_euler = (rad(wt2 - armL - lean), 0, 0)

DIRS = [0, -90, 90, 180]
for kind in KINDS:
    lib.reset()
    lib.scene(RES, 2.6, 0.3125)
    r = build(kind)
    for f in range(9):
        for d in range(4):
            pose(kind, r, f, d)
            r.root.rotation_euler = (0, 0, rad(DIRS[d]))
            bpy.context.scene.render.filepath = os.path.join(OUT, '%s_%d_%d.png' % (kind, f, d))
            bpy.ops.render.render(write_still=True)
    print('rendered', kind)
