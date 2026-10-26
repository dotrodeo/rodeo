"""Studio renders of the Rev B boards on a warm backdrop. Run with Blender:

  blender -b -P tools/render/render.py -- --all
  blender -b -P tools/render/render.py -- rodeo-power-barrel rodeo-midi
  blender -b -P tools/render/render.py -- rodeo-power-usbc --views iso --samples 64 --res 800
  blender -b -P tools/render/render.py -- rodeo-power-barrel --panel
  blender -b -P tools/render/render.py -- rodeo-motherboard --views wide

Writes hardware/<board>/renders/<board>-front.jpg, -back.jpg and -iso.jpg (or
into --out-dir). --views wide makes <board>-wide.jpg, the three-quarter view
framed 16:9 with room around the board, for a page's hero. --panel adds <board>-panel.jpg: the board with its rear panel
stood on its I/O edge, as it sits in the synth. KiCad does the geometry:
kicad-cli exports the board (and panel) as GLB with silkscreen, mask and copper,
and this script places them, replaces the materials and lights the scene.

Panel placement follows hardware/mounting/README.md: the I/O edge (the board's
top edge in KiCad) on the panel's inner face, board top 15.6 mm above the
floor, panel bottom on the floor, the panel 5 mm wider than the board each side.
"""
import glob
import math
import os
import subprocess
import sys
import tempfile

import bpy
from mathutils import Matrix, Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
KICAD_CLI = '/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli'
MM = 0.001
BOARD_UNDERSIDE = 14.0   # mm above the floor: boss 6 + standoff 8
PANEL_MARGIN = 5.0

BACKDROP = (0.68, 0.30, 0.11)       # warm peach under the board, linear
BACKDROP_EDGE = (0.36, 0.14, 0.05)  # darker and more orange toward the frame edges
VIEWS = ('front', 'back', 'iso')   # the default set; 'wide' is asked for by name
WIDE = (1.5, 27 / 32)             # 16:9, 2400 x 1350 at --res 1600


def args():
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    opts = dict(samples=256, res=1600, out_dir=None, panel=False, finish='enig', views=list(VIEWS))
    names = []
    i = 0
    while i < len(a):
        if a[i] in ('--samples', '--res'):
            opts[a[i][2:]] = int(a[i + 1]); i += 2
        elif a[i] in ('--out-dir', '--finish'):
            opts[a[i][2:].replace('-', '_')] = a[i + 1]; i += 2
        elif a[i] == '--views':
            opts['views'] = a[i + 1].split(','); i += 2
        elif a[i] == '--panel':
            opts['panel'] = True; i += 1
        elif a[i] == '--all':
            names += sorted(os.path.basename(os.path.dirname(d))
                            for d in glob.glob(os.path.join(ROOT, 'hardware', 'rodeo-*', '')))
            i += 1
        else:
            names.append(a[i]); i += 1
    if not names or not set(opts['views']) <= set(VIEWS + ('wide',)):
        sys.exit('usage: blender -b -P render.py -- <board>... | --all  [--views front,back,iso,wide] '
                 '[--panel] [--samples N] [--res PX] [--out-dir DIR] [--finish enig|hasl]')
    return names, opts


def pcb_in(d):
    pcbs = glob.glob(os.path.join(d, '*.kicad_pcb'))
    if len(pcbs) != 1:
        sys.exit(f'expected one .kicad_pcb in {d}, found {len(pcbs)}')
    return pcbs[0]


def export_glb(pcb, out):
    # --no-dnp: leave out parts marked Do Not Populate (the satellites' programming
    # headers), which kicad-cli otherwise renders as fitted.
    subprocess.run([KICAD_CLI, 'pcb', 'export', 'glb', '--force', '--subst-models', '--no-dnp',
                    '--include-tracks', '--include-pads', '--include-zones',
                    '--include-silkscreen', '--include-soldermask', '-o', out, pcb],
                   check=True, stdout=subprocess.DEVNULL)


def import_glb(path, name):
    """Import under one empty so the whole board moves as a unit."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    root = bpy.data.objects.new(name, None)
    bpy.context.scene.collection.objects.link(root)
    for o in new:
        if o.parent is None:
            o.parent = root
    bpy.context.view_layer.update()
    return root, [o for o in new if o.type == 'MESH']


def bbox(objs):
    pts = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
    return (Vector([min(p[i] for p in pts) for i in range(3)]),
            Vector([max(p[i] for p in pts) for i in range(3)]))


def board_body(meshes):
    """The FR4 slab: the widest mesh about 1.6 mm thick."""
    def score(o):
        lo, hi = bbox([o])
        t = hi.z - lo.z
        return (hi.x - lo.x) * (hi.y - lo.y) if 1.2 * MM < t < 2.0 * MM else 0
    return max(meshes, key=score)


# ---------------------------------------------------------------- materials

def principled(name, color, metallic=0.0, roughness=0.5, coat=0.0, coat_rough=0.1, spec=0.5):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (*color, 1)
    b.inputs['Metallic'].default_value = metallic
    b.inputs['Roughness'].default_value = roughness
    b.inputs['Coat Weight'].default_value = coat
    b.inputs['Coat Roughness'].default_value = coat_rough
    b.inputs['Specular IOR Level'].default_value = spec
    return m


MATS = {}


def mat(kind):
    if kind not in MATS:
        MATS[kind] = {
            'mask': lambda: principled('mask', (0.006, 0.006, 0.007), roughness=0.32, spec=0.12),
            'silk': lambda: principled('silk', (0.80, 0.80, 0.78), roughness=0.75),
            'fr4': lambda: principled('fr4', (0.55, 0.48, 0.22), roughness=0.55),
            'enig': lambda: principled('enig', (1.0, 0.74, 0.36), metallic=1.0, roughness=0.22),
            'hasl': lambda: principled('hasl', (0.82, 0.82, 0.80), metallic=1.0, roughness=0.18),
            'copper': lambda: principled('copper', (0.95, 0.60, 0.42), metallic=1.0, roughness=0.3),
            'tin': lambda: principled('tin', (0.55, 0.55, 0.54), metallic=1.0, roughness=0.38),   # satin nickel
            'gold': lambda: principled('gold', (1.0, 0.76, 0.33), metallic=1.0, roughness=0.2),
            'black': lambda: principled('black', (0.018, 0.018, 0.018), roughness=0.45),
        }[kind]()
    return MATS[kind]


def plastic(color, roughness=0.42):
    key = ('plastic',) + tuple(round(c, 2) for c in color)
    if key not in MATS:
        MATS[key] = principled('plastic', color, roughness=roughness)
    return MATS[key]


LAYERS = {'_silkscreen': 'silk', '_soldermask': 'mask', '_PCB': 'fr4',
          '_copper': 'copper', '_via': 'copper'}   # '_pad' is the surface finish


def classify_part(m):
    """Part models all come out of KiCad as metallic 1, roughness 1, so sort them by
    the colour KiCad's 3D library gave them."""
    b = m.node_tree.nodes.get('Principled BSDF') if m.use_nodes else None
    if b is None:
        return m
    c = tuple(b.inputs['Base Color'].default_value)[:3]
    hi, lo = max(c), min(c)
    sat = (hi - lo) / hi if hi > 0 else 0
    if hi < 0.1:
        return mat('black')
    if c[0] > 0.6 and 0.55 < c[1] / c[0] < 0.9 and sat > 0.4:   # gold plating
        return mat('gold')
    if sat < 0.2 and 0.35 < hi <= 0.72:                        # pins, shields, cans
        return mat('tin')
    return plastic(c, 0.35 if sat > 0.6 else 0.45)              # incl. white and cream bodies


def restyle(meshes, finish):
    for o in meshes:
        layer = next((k for k in LAYERS if o.data.name.split('.')[0].endswith(k)), None)
        is_pad = o.data.name.split('.')[0].endswith('_pad')
        for s in o.material_slots:
            if not s.material:
                continue
            if is_pad:
                s.material = mat(finish)
            elif layer:
                s.material = mat(LAYERS[layer])
            else:
                s.material = classify_part(s.material)


# ---------------------------------------------------------------- scene

def place_board(root, meshes):
    """Board's left end at x = 0, I/O edge on y = 0, underside BOARD_UNDERSIDE up."""
    lo, hi = bbox([board_body(meshes)])
    root.location = (-lo.x, -hi.y, BOARD_UNDERSIDE * MM - lo.z)
    bpy.context.view_layer.update()
    return (hi.x - lo.x) / MM


def place_panel(root, meshes, board_w):
    """KiCad panel: x across as seen from outside, y down from the top edge, z
    through the thickness with the front (silkscreen) side up. Its right end is
    the board's left end, its bottom edge on the floor, its inner face on y = 0."""
    lo, hi = bbox([board_body(meshes)])
    # panel (x, y, z) -> world (board_w + margin - x', z', y' + height), primes
    # relative to the panel's top-left corner on its back face
    to_corner = Matrix.Translation((-lo.x, -hi.y, -lo.z))
    rot = Matrix(((-1, 0, 0, 0), (0, 0, 1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
    height = (hi.y - lo.y)
    root.matrix_world = Matrix.Translation(((board_w + PANEL_MARGIN) * MM, 0, height)) @ rot @ to_corner
    bpy.context.view_layer.update()


def backdrop(centre, radius):
    """A floor that takes shadows but isn't reflected (as in Fusion; otherwise the
    black mask turns the colour of the floor), lighter under the board and
    falling off toward the edges of the frame."""
    bpy.ops.mesh.primitive_plane_add(size=4.0, location=(centre.x, centre.y, 0))
    floor = bpy.context.object
    floor.visible_glossy = False
    m = principled('backdrop', BACKDROP, roughness=0.9)
    nt = m.node_tree
    coords = nt.nodes.new('ShaderNodeTexCoord')
    scale = nt.nodes.new('ShaderNodeMapping')
    scale.inputs['Scale'].default_value = (1 / radius,) * 3
    grad = nt.nodes.new('ShaderNodeTexGradient')
    grad.gradient_type = 'SPHERICAL'
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.interpolation = 'EASE'
    ramp.color_ramp.elements[0].color = (*BACKDROP_EDGE, 1)
    ramp.color_ramp.elements[1].color = (*BACKDROP, 1)
    nt.links.new(coords.outputs['Object'], scale.inputs['Vector'])
    nt.links.new(scale.outputs['Vector'], grad.inputs['Vector'])
    nt.links.new(grad.outputs['Fac'], ramp.inputs['Fac'])
    nt.links.new(ramp.outputs['Color'], nt.nodes['Principled BSDF'].inputs['Base Color'])
    floor.data.materials.append(m)

    w = bpy.context.scene.world = bpy.data.worlds.new('studio')
    w.use_nodes = True
    nt = w.node_tree
    env = nt.nodes.new('ShaderNodeTexEnvironment')
    hdri = glob.glob(os.path.join(os.path.dirname(bpy.app.binary_path), '..', 'Resources', '*',
                                  'datafiles', 'studiolights', 'world', 'studio.exr'))
    bg = nt.nodes['Background']
    if hdri:
        env.image = bpy.data.images.load(hdri[0])
        nt.links.new(env.outputs['Color'], bg.inputs['Color'])
    bg.inputs['Strength'].default_value = 0.15


def area(name, loc, target, size, energy, color=(1, 1, 1)):
    l = bpy.data.lights.new(name, 'AREA')
    l.shape = 'DISK'
    l.size = size
    l.energy = energy
    l.color = color
    o = bpy.data.objects.new(name, l)
    bpy.context.scene.collection.objects.link(o)
    o.location = loc
    d = Vector(target) - Vector(loc)
    o.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    return o


# Light rigs, tuned on a 70 mm subject and scaled with the subject's size. The
# key stays well away from the camera's mirror angle off the board, or the black
# mask reflects it white.
RIGS = {
    # from above: key from the top left, so shadows fall to the lower right
    'top': [('key', (-0.32, 0.30, 0.45), 0.18, 55, (1.0, 0.95, 0.88)),
            ('fill', (0.35, -0.35, 0.30), 0.6, 8, (1.0, 0.97, 0.94))],
    # three-quarter: key high off the board's right end, fill from the camera side,
    # rim across the I/O edge
    'iso': [('key', (-0.45, -0.05, 0.45), 0.18, 55, (1.0, 0.95, 0.88)),
            ('fill', (0.35, 0.40, 0.20), 0.6, 8, (1.0, 0.97, 0.94)),
            ('rim', (-0.30, 0.25, 0.12), 0.25, 6, (1, 1, 1))],
}


def light(rig, centre, size):
    for o in [o for o in bpy.data.objects if o.type == 'LIGHT']:
        bpy.data.objects.remove(o)
    k = size / 0.07
    for name, off, sz, energy, colour in RIGS[rig]:
        area(name, centre + k * Vector(off), centre, sz * k, energy * k * k, colour)


def camera():
    cam = bpy.data.cameras.new('cam')
    cam.sensor_width = 36
    cam.clip_start = 0.001   # the default 0.1 m cuts through the small boards up close
    cam.clip_end = 20
    o = bpy.data.objects.new('cam', cam)
    bpy.context.scene.collection.objects.link(o)
    bpy.context.scene.camera = o
    return o


def aim_top(cam, lo, hi):
    """Orthographic, straight down, KiCad's orientation (y up the image)."""
    c = (lo + hi) / 2
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = max(hi.x - lo.x, hi.y - lo.y) * 1.22
    cam.location = (c.x, c.y, hi.z + 1.0)
    cam.rotation_euler = (0, 0, 0)


def aim_iso(cam, centre, size):
    """Three-quarter view from in front of the I/O edge, off the board's right end."""
    cam.data.type = 'PERSP'
    cam.data.lens = 85
    az, el = math.radians(46), math.radians(31)
    cam.location = centre + size * 3.1 * Vector((math.sin(az) * math.cos(el),
                                                 math.cos(az) * math.cos(el), math.sin(el)))
    cam.rotation_euler = (centre - cam.location).to_track_quat('-Z', 'Y').to_euler()


def flip(root, meshes):
    """Turn the board over left to right (as KiCad's flipped view does, so the back
    silkscreen reads), keeping its parts clear of the floor."""
    lo, hi = bbox(meshes)
    c = (lo + hi) / 2
    root.matrix_world = (Matrix.Translation(c) @ Matrix.Rotation(math.pi, 4, 'Y')
                         @ Matrix.Translation(-c) @ root.matrix_world)
    bpy.context.view_layer.update()
    lo, _ = bbox(meshes)
    if lo.z < 3 * MM:
        root.location.z += 3 * MM - lo.z
        bpy.context.view_layer.update()


def render_settings(samples, res):
    s = bpy.context.scene
    s.render.engine = 'CYCLES'
    prefs = bpy.context.preferences.addons['cycles'].preferences
    try:
        prefs.compute_device_type = 'METAL'
        prefs.get_devices()
        for d in prefs.devices:
            d.use = True
        s.cycles.device = 'GPU'
    except Exception:
        pass
    s.cycles.samples = samples
    s.cycles.use_denoising = True
    s.render.resolution_x = s.render.resolution_y = res
    s.render.film_transparent = False
    s.view_settings.view_transform = 'AgX'
    s.view_settings.look = 'AgX - Medium High Contrast'
    s.view_settings.exposure = -0.5
    s.render.image_settings.file_format = 'JPEG'   # 0.1-0.4 MB a view; PNG was ~2.5 MB
    s.render.image_settings.quality = 90


def render(path):
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print('wrote', path)


def render_board(name, o):
    board_dir = os.path.join(ROOT, 'hardware', name)
    panel_dir = os.path.join(ROOT, 'hardware', 'panels', name + '-panel')
    out_dir = o['out_dir'] or os.path.join(board_dir, 'renders')
    os.makedirs(out_dir, exist_ok=True)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    MATS.clear()
    tmp = tempfile.mkdtemp(prefix='rodeo-render-')
    export_glb(pcb_in(board_dir), os.path.join(tmp, 'board.glb'))
    root, meshes = import_glb(os.path.join(tmp, 'board.glb'), name)
    board_w = place_board(root, meshes)
    restyle(meshes, o['finish'])
    home = root.matrix_world.copy()

    lo, hi = bbox(meshes)
    centre, size = (lo + hi) / 2, max(hi - lo)
    backdrop(centre, size * 1.6)
    cam = camera()
    render_settings(o['samples'], o['res'])

    scene = bpy.context.scene.render
    for view in o['views']:
        root.matrix_world = home
        bpy.context.view_layer.update()
        scene.resolution_x = scene.resolution_y = o['res']
        if view == 'back':
            flip(root, meshes)
        if view == 'iso':
            light('iso', centre, size)
            aim_iso(cam, centre, size)
        elif view == 'wide':
            # The lens keeps its horizontal field, so step back to leave the
            # board about two thirds of the width
            scene.resolution_x, scene.resolution_y = (round(o['res'] * k) for k in WIDE)
            light('iso', centre, size)
            aim_iso(cam, centre, size * 1.5)
        else:
            lo, hi = bbox(meshes)
            light('top', (lo + hi) / 2, size)
            aim_top(cam, lo, hi)
        render(os.path.join(out_dir, f'{name}-{view}.jpg'))

    if o['panel'] and os.path.isdir(panel_dir):
        root.matrix_world = home
        scene.resolution_x = scene.resolution_y = o['res']
        export_glb(pcb_in(panel_dir), os.path.join(tmp, 'panel.glb'))
        proot, pmeshes = import_glb(os.path.join(tmp, 'panel.glb'), name + '-panel')
        place_panel(proot, pmeshes, board_w)
        restyle(pmeshes, o['finish'])
        lo, hi = bbox(meshes + pmeshes)
        c = (lo + hi) / 2
        c.y = lo.y + 0.75 * (hi.y - lo.y)    # aim nearer the panel than the board's far edge
        size = max(hi - lo)
        light('iso', c, size)
        aim_iso(cam, c, size * 1.06)
        render(os.path.join(out_dir, f'{name}-panel.jpg'))


def main():
    names, o = args()
    for name in names:
        render_board(name, o)


main()
