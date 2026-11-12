#!/usr/bin/env python3
"""Generate JLCPCB fabrication and assembly files for a KiCad project.

  jlc_fab.py path/to/project.kicad_pro [--skip A1,H*] [--no-check-image] [--no-step]
  jlc_fab.py path/to/project.kicad_pro --panel

Writes, next to the project, the same layout as the Rodeo satellite boards:

  jlcpcb/gerber/                      Gerbers (all copper layers), PTH/NPTH drill, drill maps
  jlcpcb/production_files/GERBER-<name>.zip
  jlcpcb/production_files/BOM-<name>.csv   Comment, Designator, Footprint, LCSC, Quantity
  jlcpcb/production_files/CPL-<name>.csv   corrected for JLC's footprints (see below)
  jlcpcb/placement-check.png          JLC's footprints, placed by the CPL, over our pads
  <name>.step                         3D model for renders (Fusion etc.): board, pads,
                                      silkscreen, soldermask and every part's model,
                                      origin at the board's top-left corner; not tracked
                                      in git (see .gitignore), regenerated with the rest

A project without a schematic (the rear panels) gets Gerbers and the STEP only.

--panel builds a panel of the board with KiKit from the panel.json next to the
project (see "Panels" in README.md) into panel/, then writes the same files for the
panel into jlcpcb-panel/ (no STEP). The panel keeps the board's references (so its
silkscreen is the board's); the BOM and CPL name each copy's parts <ref>_<n>, copies
numbered in reading order, and the CPL is solved per copy, so rotated copies come
out right.

Why the CPL needs correcting: JLC places each part with its own EasyEDA footprint
for that LCSC number, origin at the CPL position, rotated by the CPL rotation.
Where EasyEDA and KiCad disagree on origin or orientation (JST connectors, IDC
headers, SOIC/TSSOP/SOT-23, ...), KiCad's raw position export puts parts in the
wrong place. This script downloads each part's EasyEDA footprint (kept in
easyeda-footprints/), finds the 90-degree rotation and offset that puts JLC's pads on
ours with matching pin numbers (PAD_ALIASES maps KiCad pad names such as a jack's
T/R/S to JLC's numbers), and writes that. Bottom-side parts are solved against JLC's
footprint flipped left-right, and their CPL rotation is written clockwise as seen
from the top, which is how JLC reads it (worked out from their order preview on
2026-10-16; see cpl_rotation). JLC doesn't document this, so still check polarised
bottom parts in the preview. Pin numbers carry polarity, so
diodes and electrolytics come out right too. Parts whose best fit is worse than
--tolerance mm are listed: check those by eye in placement-check.png.

Needs: kicad-cli; easyeda2kicad and matplotlib (see requirements.txt). The board's
zone fills are plotted as saved: refill and save in KiCad first.
"""
import argparse
import csv
import fnmatch
import glob
import json
import math
import os
import re
import subprocess
import sys
import time
import zipfile

from kicadlib import HERE, Netlist, kicad_python, pcb_footprint_list, pcb_footprints, pcb_outline, rotate, run

# JLC's footprints, one JSON per LCSC number. Tracked in git: the EasyEDA API
# rate-limits, and the files are small.
EASYEDA_CACHE = os.path.join(HERE, 'easyeda-footprints')

# KiCad footprints that name their pads where JLC's number them: KiCad name ->
# EasyEDA pad number, per LCSC part, worked out from the two footprints' geometry.
PAD_ALIASES = {
    'C431535': {'S': '1', 'R2': '2', 'R1': '3', 'T': '4'},   # PJ-320D, Jack_3.5mm_PJ320D_Horizontal
    # LTST-C230 reverse-mount LED: JLC's pad 1 is the anode, KiCad's LED pad 1 is the
    # cathode (JLC's preview showed its + on our cathode pad, 2026-10-16).
    'C125107': {'1': '2', '2': '1'},
}

# The other way round, for parts whose KiCad footprint repeats pad numbers: JLC's pad
# number -> ours. A 6 x 6 mm tact switch is pads 1, 1, 2, 2 in KiCad (each row is one
# contact) but 1-4 in JLC's footprint, with 3 and 4 on one row.
EE_PAD_ALIASES = {
    'C87037': {'3': '1', '4': '1', '1': '2', '2': '2'},   # Omron B3F-1052, SW_PUSH_6mm
    # PEC11R-S on the EC11E-Switch footprint: KiCad names both lugs MP, JLC numbers them 7 and 8.
    'C143790': {'1': 'A', '2': 'C', '3': 'B', '4': 'S2', '5': 'S1', '7': 'MP', '8': 'MP'},
}



def plot_layers(pcb):
    """Every copper layer the board defines, plus paste, silk, mask and outline."""
    inner = re.findall(r'\(\d+ "(In\d+\.Cu)" (?:signal|power|mixed|jumper)', open(pcb).read())
    return ','.join(['F.Cu', *inner, 'B.Cu', 'F.Paste', 'B.Paste', 'F.Silkscreen', 'B.Silkscreen',
                     'F.Mask', 'B.Mask', 'Edge.Cuts'])


def plugin_lcsc(out_dir):
    """LCSC numbers stored by Bouni's KiCad JLCPCB plugin in jlcpcb/project.db, if any."""
    db = os.path.join(out_dir, 'project.db')
    if not os.path.exists(db):
        return {}
    import sqlite3
    con = sqlite3.connect(db)
    try:
        return {r: c for r, c in con.execute('select reference, lcsc from part_info') if c}
    except sqlite3.Error:
        return {}
    finally:
        con.close()


def refkey(r):
    m = re.match(r'([A-Za-z]+)(\d*)(?:_(\d+))?', r)
    return (m.group(1), int(m.group(2) or 0), int(m.group(3) or 0))


# KiCad's own Python, which has pcbnew; KiKit runs on it from tools/kicad/.kikit
KICAD_PYTHON = kicad_python()
KIKIT_DIR = os.path.join(HERE, '.kikit')


def kikit_panel(base):
    """Panelize <base>.kicad_pcb with KiKit per panel.json; returns the panel's .kicad_pcb."""
    d, name = os.path.dirname(base), os.path.basename(base)
    spec = os.path.join(d, 'panel.json')
    if not os.path.exists(spec):
        sys.exit(f'--panel: no panel.json next to {name}.kicad_pro')
    if not os.path.isdir(KIKIT_DIR):
        sys.exit(f'--panel needs KiKit in {KIKIT_DIR}: see "Panels" in tools/kicad/README.md')
    pdir = os.path.join(d, 'panel')
    os.makedirs(pdir, exist_ok=True)
    panel = os.path.join(pdir, f'{name}-panel.kicad_pcb')
    env = dict(os.environ, PYTHONPATH=os.pathsep.join([KIKIT_DIR, HERE]))  # HERE: rodeo_tabs.py
    r = subprocess.run([KICAD_PYTHON, '-m', 'kikit.ui', 'panelize', '-p', spec, base + '.kicad_pcb', panel],
                       env=env, capture_output=True, text=True)
    if r.returncode or not os.path.exists(panel):
        sys.exit(f'KiKit failed:\n{r.stdout}{r.stderr}')
    for f in set(glob.glob(os.path.join(pdir, '*.lck')) + glob.glob(os.path.join(pdir, '~*'))):
        os.remove(f)
    return panel


def panel_copies(src, panel):
    """Name each copy's footprints on a panel <ref>_<n>.

    src: pcb_footprints of the board; panel: pcb_footprint_list of the panel, whose
    references repeat once per copy. Each copy is a rotation and translation of the
    board, found from one part's instances and confirmed by every other part's.
    Copies are numbered top to bottom, then left to right. Returns {name: footprint}.
    """
    def place(theta, c, p):
        a = math.radians(theta)
        return (c[0] + p['x'] * math.cos(a) - p['y'] * math.sin(a),
                c[1] + p['x'] * math.sin(a) + p['y'] * math.cos(a))
    inst = [(r, f) for r, f in panel if r in src]
    anchor = max(src, key=lambda r: (sum(1 for q, _ in inst if q == r), len(src[r]['pads'])))
    copies = []
    for r, f in inst:
        if r == anchor:
            theta = (f['rot'] - src[r]['rot']) % 360
            x, y = place(theta, (0, 0), src[r])
            copies.append((theta, (f['x'] - x, f['y'] - y)))
    copies.sort(key=lambda c: (-round(c[1][1], 1), round(c[1][0], 1)))
    out = {}
    for r, f in inst:
        for n, (theta, c) in enumerate(copies):
            x, y = place(theta, c, src[r])
            if math.dist((x, y), (f['x'], f['y'])) < 0.01 and (f['rot'] - src[r]['rot'] - theta) % 360 < 0.01:
                out[f'{r}_{n}'] = f
                break
        else:
            sys.exit(f'{r} at ({f["x"]:.2f}, {f["y"]:.2f}) on the panel matches no copy of the board')
    return out


def easyeda_footprint(code):
    """{name, bbox, pads, silk} for an LCSC part, from cache or EasyEDA."""
    path = os.path.join(EASYEDA_CACHE, f'{code}.json')
    if os.path.exists(path):
        return json.load(open(path))
    from easyeda2kicad.easyeda.easyeda_api import EasyedaApi
    from easyeda2kicad.easyeda.easyeda_importer import EasyedaFootprintImporter
    for attempt in range(4):
        try:
            data = EasyedaApi().get_cad_data_of_component(lcsc_id=code)
            fp = EasyedaFootprintImporter(easyeda_cp_cad_data=data).get_footprint()
            break
        except Exception as e:          # the API rate-limits with HTTP 403
            if attempt == 3:
                raise SystemExit(f'could not fetch EasyEDA footprint for {code}: {e}')
            time.sleep(5 * (attempt + 1))
    out = dict(code=code, name=fp.info.name, bbox=[fp.bbox.x, fp.bbox.y],
               pads=[dict(num=p.number, x=p.center_x, y=p.center_y, w=p.width, h=p.height,
                          rot=p.rotation, shape=p.shape) for p in fp.pads],
               silk=[t.points for t in fp.tracks if str(t.layer_id) == '3'])
    os.makedirs(os.path.dirname(path), exist_ok=True)
    json.dump(out, open(path, 'w'))
    return out


def ee_local(ee, mirror=False):
    """EasyEDA pads relative to the footprint origin, cartesian, in mm.

    mirror: the footprint as seen from the top when it sits on the bottom side,
    flipped left-right (x -> -x).
    """
    bx, by = ee['bbox']
    s = -1 if mirror else 1
    return [dict(q, x=s * (q['x'] - bx), y=-(q['y'] - by)) for q in ee['pads']]


def solve(kpads, ee, mirror=False):
    """Best (R, X, Y, err) placing EasyEDA footprint `ee` on KiCad pads `kpads`.

    For a bottom-side part (mirror=True) the footprint is flipped left-right and R
    is counter-clockwise as seen from the top, like every other R here. JLC reads a
    bottom-side CPL rotation the other way (clockwise from the top), so the CPL
    writes -R for those parts; see cpl_rotation().
    """
    local = ee_local(ee, mirror)
    byn = {}
    for p in kpads:
        byn.setdefault(p['num'], []).append((p['X'], p['Y']))
    allk = [(p['X'], p['Y']) for p in kpads]
    best = None
    for R in (0, 90, 180, 270):
        pairs = [(q, *rotate(q['x'], q['y'], R)) for q in local if q['num'] in byn]
        if not pairs:
            continue
        # Per pad number, the centre of our pads with it; with unique numbers, that pad.
        cen = {n: (sum(x for x, _ in v) / len(v), sum(y for _, y in v) / len(v)) for n, v in byn.items()}
        tx = sum(cen[q['num']][0] - lx for q, lx, ly in pairs) / len(pairs)
        ty = sum(cen[q['num']][1] - ly for q, lx, ly in pairs) / len(pairs)
        err = max(min(math.dist((lx + tx, ly + ty), k) for k in byn[q['num']]) for q, lx, ly in pairs)
        geo = max(min(math.dist((x + tx, y + ty), k) for k in allk)
                  for x, y in (rotate(q['x'], q['y'], R) for q in local))
        cand = (round(err, 2), round(geo, 2), R, tx, ty, err)
        if best is None or cand[:2] < best[:2]:
            best = cand
    if best is None:
        return None
    return dict(R=best[2], X=best[3], Y=best[4], err=max(best[0], best[1]), mirror=mirror)


def cpl_rotation(s):
    """The CPL Rotation for a solved placement.

    Bottom side: JLC flips the footprint left-right and turns it clockwise as seen
    from the top. Established on the first satellite order (2026-10-16) from two
    preview checks: R as written put U1 (TSSOP-20) 180 degrees out; flipping
    top-to-bottom instead fixed U1 but put the JST SH connectors 180 degrees out.
    Negating R satisfies both.
    """
    return (-s['R'] if s['mirror'] else s['R']) % 360


def check_image(path, fps, placed, eefp, outline):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.patches import Ellipse, Rectangle
    from matplotlib.transforms import Affine2D

    def pad(ax, x, y, w, h, ang, shape, **kw):
        round_ = str(shape).lower() in ('circle', 'ellipse', 'oval') and abs(w - h) < 1e-3
        p = Ellipse((x, y), w, h, **kw) if round_ else Rectangle((x - w / 2, y - h / 2), w, h, **kw)
        p.set_transform(Affine2D().rotate_deg_around(x, y, ang) + ax.transData)
        ax.add_patch(p)

    fig, ax = plt.subplots(figsize=(13, 17), dpi=150)
    for kind, pts in outline:
        if kind == 'rect':
            (x1, y1), (x2, y2) = pts[:2]
            ax.add_patch(Rectangle((min(x1, x2), min(y1, y2)), abs(x2 - x1), abs(y2 - y1), fill=False, lw=0.8))
        else:
            ax.plot([p[0] for p in pts], [p[1] for p in pts], 'k-', lw=0.8)
    for ref, s in placed.items():
        for p in fps[ref]['pads']:
            pad(ax, p['X'], p['Y'], p['w'], p['h'], p['rot'], p['shape'], facecolor='#c9a227', edgecolor='none')
            if p['num'] == '1':
                ax.plot(p['X'], p['Y'], 'o', color='black', ms=2.2)
        ee = eefp[ref]
        for q in ee_local(ee, s.get('mirror', False)):
            x, y = rotate(q['x'], q['y'], s['R'])
            qrot = -q['rot'] if s.get('mirror') else q['rot']
            pad(ax, x + s['X'], y + s['Y'], q['w'], q['h'], s['R'] - qrot, q['shape'],
                facecolor='none', edgecolor='red', lw=0.6)
            if q['num'] == '1':
                ax.plot(x + s['X'], y + s['Y'], 'x', color='red', ms=4, mew=1)
        bx, by = ee['bbox']
        sx = -1 if s.get('mirror') else 1
        for line in ee['silk']:
            v = [float(z) for z in str(line).split()]
            pts = [rotate(sx * (v[i] * 0.254 - bx), -(v[i + 1] * 0.254 - by), s['R']) for i in range(0, len(v) - 1, 2)]
            ax.plot([p[0] + s['X'] for p in pts], [p[1] + s['Y'] for p in pts], color='#1f6fd1', lw=0.5)
        ax.text(s['X'], s['Y'] + 0.3, ref + (' (bottom)' if s.get('mirror') else ''), fontsize=5, ha='center')
    ax.set_aspect('equal')
    ax.autoscale()
    ax.set_title('JLC footprints (red pads, blue silk) placed by the CPL over KiCad pads (gold). '
                 'Black dot = KiCad pin 1, red x = JLC pin 1', fontsize=9)
    fig.savefig(path, bbox_inches='tight')
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('project', help='the .kicad_pro file')
    ap.add_argument('--skip', default='A1,H*', help='refs left out of BOM/CPL (globs); default A1,H*')
    ap.add_argument('--tolerance', type=float, default=0.3, help='mm; worse fits are reported')
    ap.add_argument('--no-check-image', action='store_true')
    ap.add_argument('--no-step', action='store_true', help="don't write <name>.step")
    ap.add_argument('--panel', action='store_true', help='panelize per panel.json, outputs in jlcpcb-panel/')
    a = ap.parse_args()

    base = os.path.splitext(os.path.abspath(a.project))[0]
    name = os.path.basename(base)
    pcb, sch = base + '.kicad_pcb', base + '.kicad_sch'
    out = os.path.join(os.path.dirname(base), 'jlcpcb')
    if a.panel:
        pcb, name, out = kikit_panel(base), name + '-panel', out + '-panel'
        a.no_step = True
    gdir, pdir = os.path.join(out, 'gerber'), os.path.join(out, 'production_files')
    os.makedirs(gdir, exist_ok=True)
    os.makedirs(pdir, exist_ok=True)
    for f in glob.glob(os.path.join(gdir, '*')):
        os.remove(f)
    skip = lambda r: any(fnmatch.fnmatch(r, g) for g in a.skip.split(','))

    # Gerbers, drill, zip
    run('pcb', 'export', 'gerbers', '--layers', plot_layers(pcb), '--subtract-soldermask', '-o', gdir + '/', pcb)
    run('pcb', 'export', 'drill', '--format', 'excellon', '--excellon-separate-th', '--excellon-units', 'mm',
        '--generate-map', '--map-format', 'pdf', '-o', gdir + '/', pcb)
    zpath = os.path.join(pdir, f'GERBER-{name}.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        for f in sorted(glob.glob(os.path.join(gdir, '*'))):
            if not f.endswith('.pdf'):
                z.write(f, os.path.basename(f))

    # STEP for renders, origin at the outline's top-left corner
    if not a.no_step:
        pts = [p for _, ps in pcb_outline(pcb) for p in ps]
        x0, y0 = min(x for x, _ in pts), -max(y for _, y in pts)
        run('pcb', 'export', 'step', '-f', '--subst-models', '--include-pads', '--include-silkscreen',
            '--include-soldermask', '--user-origin', f'{x0:g}x{y0:g}mm', '-o', base + '.step', pcb)
        print(f'  STEP: {name}.step')

    if not os.path.exists(sch):
        print('  no schematic: Gerbers and STEP only')
        return

    # BOM
    nl = Netlist.from_schematic(sch)
    refs = sorted((r for r in nl.parts if not r.startswith('#') and not skip(r)), key=refkey)
    plugin = plugin_lcsc(os.path.join(os.path.dirname(base), 'jlcpcb'))
    fps = pcb_footprints(pcb)
    # Each schematic part's footprints on the board: itself, or a panel's copies
    if a.panel:
        fps = panel_copies(pcb_footprints(base + '.kicad_pcb'), pcb_footprint_list(pcb))
        copies = {r: sorted((p for p in fps if re.fullmatch(re.escape(r) + r'_\d+', p)), key=refkey) for r in refs}
    else:
        copies = {r: [r] for r in refs}
    groups, nolcsc = {}, []
    for r in refs:
        p = nl.parts[r]
        code = nl.lcsc(r) or plugin.get(r, '')
        if not code:
            nolcsc.append(r)
            continue
        groups.setdefault((p['value'], p['footprint'].split(':')[-1], code), []).extend(copies[r] or [r])
    with open(os.path.join(pdir, f'BOM-{name}.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC', 'Quantity'])
        for (v, fp, code), rs in groups.items():
            w.writerow([v, ','.join(rs), fp, code, len(rs)])

    # CPL, corrected to JLC's footprints
    value = {c: nl.parts[r]['value'] for r in refs for c in copies[r]}
    placed, eefp, poor = {}, {}, []
    for (v, fp, code), rs in groups.items():
        ee = easyeda_footprint(code)
        ee_alias = EE_PAD_ALIASES.get(code)
        if ee_alias:
            ee = dict(ee, pads=[dict(q, num=ee_alias.get(q['num'], q['num'])) for q in ee['pads']])
        for r in rs:
            if r not in fps:
                sys.exit(f'{r} is in the schematic but not on the board: update the PCB first')
            alias = PAD_ALIASES.get(code, {})
            s = solve([dict(p, num=alias.get(p['num'], p['num'])) for p in fps[r]['pads']], ee,
                      mirror=fps[r]['layer'] == 'B.Cu')
            if s is None:
                sys.exit(f'{r} ({code}, {ee["name"]}): no pad numbers in common with the KiCad footprint')
            placed[r], eefp[r] = s, ee
            if s['err'] > a.tolerance:
                poor.append((r, ee['name'], s['err']))
    with open(os.path.join(pdir, f'CPL-{name}.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['Designator', 'Val', 'Package', 'Mid X', 'Mid Y', 'Rotation', 'Layer'])
        for r in sorted(placed, key=refkey):
            s = placed[r]
            w.writerow([r, value[r], fps[r]['lib'].split(':')[-1],
                        f"{s['X']:.4f}", f"{s['Y']:.4f}", f"{cpl_rotation(s):.1f}",
                        'bottom' if s['mirror'] else 'top'])
    if not a.no_check_image:
        check_image(os.path.join(out, 'placement-check.png'), fps, placed, eefp, pcb_outline(pcb))

    corrected = sum(1 for r, s in placed.items()
                    if (s['R'] - fps[r]['rot']) % 360 > 0.5 or
                    math.dist((s['X'], s['Y']), (fps[r]['x'], fps[r]['y'])) > 0.05)
    print(f'{out}')
    print(f'  BOM: {len(groups)} lines, {sum(len(v) for v in groups.values())} parts'
          + (f'; no LCSC number (left out): {" ".join(nolcsc)}' if nolcsc else ''))
    print(f'  CPL: {len(placed)} parts, {corrected} corrected for JLC footprints')
    bottom = sorted((r for r, s in placed.items() if s['mirror']), key=refkey)
    if bottom:
        print(f'  bottom side: {" ".join(bottom)}. JLC\'s bottom-side rotation convention has changed '
              'over time: check every polarised bottom part in their order preview.')
    for r, n, e in poor:
        print(f'  CHECK {r} ({n}): best fit {e:.2f} mm')


if __name__ == '__main__':
    main()
