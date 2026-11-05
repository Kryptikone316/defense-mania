"""Renders the Bastion, Defense Mania's keep: a tall stone keep with a round turret and a blue banner, 64 x 128 px in the game (two of the towers' tiles tall).
Run:  blender -b -P bastion.py -- <output png>      Renders 256 x 512; tools/make_bastion_art.py shrinks it."""
import bpy, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lib

OUT = sys.argv[sys.argv.index('--') + 1]
lib.reset()
lib.scene((256, 512), 5.2, 0.453)
stone = lib.mat('stone', (0.62, 0.60, 0.56), 0.9)
dark = lib.mat('darkstone', (0.46, 0.44, 0.42), 0.9)
light = lib.mat('lightstone', (0.72, 0.70, 0.65), 0.85)
wood = lib.mat('wood', (0.38, 0.23, 0.11), 0.8)
iron = lib.mat('iron', (0.25, 0.26, 0.29), 0.5, 0.7)
blue = lib.mat('blue', (0.18, 0.34, 0.72), 0.7)
gold = lib.mat('gold', (1.0, 0.78, 0.15), 0.35, 0.8, 0.3)
black = lib.mat('slit', (0.04, 0.04, 0.05), 1.0)
root = lib.pivot('root')
root.scale = (0.78, 0.78, 0.78)
root.location = (0.16, 0, 0)

lib.box(dark, (2.5, 1.5, 0.28), (0, 0, 0.14), parent=root, bevel=0.03)                  # a footing
lib.box(stone, (2.3, 1.3, 1.45), (0, 0, 1.0), parent=root, bevel=0.05)                   # the curtain wall block
for i in range(5):                                                                      # its battlements
    lib.box(light, (0.34, 0.34, 0.3), (-0.9 + i * 0.45, -0.48, 1.88), parent=root, bevel=0.02)
    lib.box(light, (0.34, 0.34, 0.3), (-0.9 + i * 0.45, 0.48, 1.88), parent=root, bevel=0.02)
for k in range(3):                                                                      # courses of stone
    lib.box(dark, (2.34, 1.34, 0.04), (0, 0, 0.55 + k * 0.45), parent=root)
lib.box(stone, (1.35, 1.25, 2.6), (0.35, 0.1, 2.75), parent=root, bevel=0.05)           # the keep rising behind
for k in range(5):
    lib.box(dark, (1.39, 1.29, 0.04), (0.35, 0.1, 1.9 + k * 0.5), parent=root)
for sx in (-0.45, 0.1, 0.65, 1.0):                                                      # its battlements
    lib.box(light, (0.3, 0.3, 0.3), (0.35 + (sx - 0.35) * 0.9, -0.42, 4.2), parent=root, bevel=0.02)
    lib.box(light, (0.3, 0.3, 0.3), (0.35 + (sx - 0.35) * 0.9, 0.62, 4.2), parent=root, bevel=0.02)
lib.box(light, (0.3, 0.3, 0.3), (-0.28, 0.1, 4.2), parent=root); lib.box(light, (0.3, 0.3, 0.3), (0.98, 0.1, 4.2), parent=root)
lib.box(stone, (1.2, 1.1, 0.12), (0.35, 0.1, 4.08), parent=root)
lib.cyl(stone, 0.6, 3.4, (-0.85, -0.2, 1.7), parent=root, verts=14)                      # a round turret at the front left
lib.cyl(dark, 0.64, 0.1, (-0.85, -0.2, 3.4), parent=root, verts=14)
lib.cyl(blue, 0.78, 0.9, (-0.85, -0.2, 3.95), parent=root, verts=14, r2=0.0)             # with a blue roof
lib.ball(gold, 0.09, (-0.85, -0.2, 4.45), parent=root, seg=8, rings=6)
for z in (1.0, 2.4):                                                                    # arrow slits
    lib.box(black, (0.1, 0.1, 0.4), (-0.85, -0.78, z), parent=root)
lib.box(black, (0.1, 0.1, 0.5), (0.35, -0.54, 3.1), parent=root)
lib.box(black, (0.1, 0.1, 0.5), (0.85, -0.54, 3.1), parent=root)
lib.box(wood, (0.78, 0.1, 0.95), (0.3, -0.68, 0.55), parent=root, bevel=0.02)             # the gate
lib.cyl(wood, 0.39, 0.1, (0.3, -0.68, 1.03), (90, 0, 0), parent=root, verts=14)
lib.box(iron, (0.82, 0.12, 0.06), (0.3, -0.7, 0.35), parent=root)
lib.box(iron, (0.82, 0.12, 0.06), (0.3, -0.7, 0.75), parent=root)
lib.box(iron, (0.06, 0.12, 1.4), (0.3, -0.7, 0.6), parent=root)
lib.cyl(lib.mat('pole', (0.4, 0.28, 0.15)), 0.035, 1.5, (0.35, 0.1, 4.95), parent=root, verts=6)   # the flagpole and banner
lib.box(blue, (0.7, 0.03, 0.42), (0.74, 0.1, 5.4), parent=root)
lib.ball(gold, 0.09, (0.74, 0.07, 5.4), parent=root, seg=8, rings=6)
lib.ball(gold, 0.05, (0.35, 0.1, 5.72), parent=root, seg=6, rings=4)
for i in range(2):                                                                      # braziers beside the gate
    lib.cyl(iron, 0.09, 0.4, (-0.15 + i * 1.0 - 0.1, -0.85, 0.2), parent=root, verts=8)
    lib.ball(gold, 0.11, (-0.15 + i * 1.0 - 0.1, -0.85, 0.5), parent=root, seg=8, rings=6)
bpy.context.scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print('rendered the keep')
