"""Shared helpers for the Blender scripts that render Defense Mania's soldiers and Bastion (Blender 4.0, run in the background:
blender -b -P <script> -- <args>). Low-poly, flat-shaded models, an orthographic camera looking down at about 25 degrees like the monster sheets, a
transparent background. Cycles on the CPU so it works without a display."""
import bpy, math

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)

def mat(name, rgb, rough=0.75, metal=0.0, emit=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (rgb[0] ** 2.2, rgb[1] ** 2.2, rgb[2] ** 2.2, 1)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Metallic'].default_value = metal
    if emit > 0:
        b.inputs['Emission Color'].default_value = (rgb[0] ** 2.2, rgb[1] ** 2.2, rgb[2] ** 2.2, 1)
        b.inputs['Emission Strength'].default_value = emit
    return m

def _finish(o, m, parent, loc, rot, smooth):
    o.data.materials.append(m)
    if parent is not None: o.parent = parent
    o.location = loc
    o.rotation_euler = tuple(math.radians(a) for a in rot)
    if smooth:
        for p in o.data.polygons: p.use_smooth = True
    return o

def box(m, size, loc=(0, 0, 0), rot=(0, 0, 0), parent=None, bevel=0.0):
    bpy.ops.mesh.primitive_cube_add(size=1)
    o = bpy.context.object
    o.scale = size
    bpy.ops.object.transform_apply(scale=True)
    if bevel > 0:
        md = o.modifiers.new('b', 'BEVEL'); md.width = bevel; md.segments = 2
    return _finish(o, m, parent, loc, rot, False)

def cyl(m, radius, length, loc=(0, 0, 0), rot=(0, 0, 0), parent=None, verts=10, smooth=True, r2=None):
    if r2 is None:
        bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=radius, depth=length)
    else:
        bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=radius, radius2=r2, depth=length)
    return _finish(bpy.context.object, m, parent, loc, rot, smooth)

def ball(m, radius, loc=(0, 0, 0), scale=(1, 1, 1), parent=None, seg=12, rings=8, smooth=True):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=seg, ring_count=rings, radius=radius)
    o = bpy.context.object
    o.scale = scale
    bpy.ops.object.transform_apply(scale=True)
    return _finish(o, m, parent, loc, (0, 0, 0), smooth)

def cone(m, radius, length, loc=(0, 0, 0), rot=(0, 0, 0), parent=None, verts=8, smooth=False):
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=radius, radius2=0, depth=length)
    return _finish(bpy.context.object, m, parent, loc, rot, smooth)

def pivot(name, loc=(0, 0, 0), parent=None):
    o = bpy.data.objects.new(name, None)
    bpy.context.scene.collection.objects.link(o)
    if parent is not None: o.parent = parent
    o.location = loc
    return o

def scene(res, ortho, shift_y, tilt=65.0, samples=40):
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.samples = samples
    sc.cycles.use_denoising = False
    sc.render.film_transparent = True
    sc.render.resolution_x, sc.render.resolution_y = (res, res) if isinstance(res, int) else res
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGBA'
    sc.view_settings.view_transform = 'Standard'
    w = bpy.data.worlds.new('w'); sc.world = w; w.use_nodes = True
    bg = w.node_tree.nodes['Background']
    bg.inputs['Color'].default_value = (0.62, 0.66, 0.72, 1); bg.inputs['Strength'].default_value = 0.55
    cam = bpy.data.cameras.new('cam'); cam.type = 'ORTHO'; cam.ortho_scale = ortho; cam.shift_y = shift_y
    co = bpy.data.objects.new('cam', cam); sc.collection.objects.link(co); sc.camera = co
    d = 30.0
    el = math.radians(90 - tilt)   # how far the camera looks down: tilt 65 means 25 degrees below the horizon
    co.location = (0, -d * math.cos(el), d * math.sin(el))
    co.rotation_euler = (math.radians(tilt), 0, 0)
    sun = bpy.data.lights.new('sun', 'SUN'); sun.energy = 3.6; sun.angle = math.radians(18)
    so = bpy.data.objects.new('sun', sun); sc.collection.objects.link(so)
    so.rotation_euler = (math.radians(50), math.radians(8), math.radians(-35))
    fill = bpy.data.lights.new('fill', 'SUN'); fill.energy = 0.8
    fo = bpy.data.objects.new('fill', fill); sc.collection.objects.link(fo)
    fo.rotation_euler = (math.radians(70), 0, math.radians(140))
    return sc
