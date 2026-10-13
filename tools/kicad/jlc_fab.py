#!/usr/bin/env python3
"""Generate JLCPCB fabrication and assembly files for a KiCad project.

  jlc_fab.py path/to/project.kicad_pro [--skip A1,H*] [--no-check-image]

Writes, next to the project, the same layout as the Rodeo satellite boards:

  jlcpcb/gerber/                      Gerbers (all copper layers), PTH/NPTH drill, drill maps
  jlcpcb/production_files/GERBER-<name>.zip
  jlcpcb/production_files/BOM-<name>.csv   Comment, Designator, Footprint, LCSC, Quantity
  jlcpcb/production_files/CPL-<name>.csv   corrected for JLC's footprints (see below)
  jlcpcb/placement-check.png          JLC's footprints, placed by the CPL, over our pads

Why the CPL needs correcting: JLC places each part with its own EasyEDA footprint
for that LCSC number, origin at the CPL position, rotated by the CPL rotation.
Where EasyEDA and KiCad disagree on origin or orientation (JST connectors, IDC
headers, SOIC/TSSOP/SOT-23, ...), KiCad's raw position export puts parts in the
wrong place. This script downloads each part's EasyEDA footprint (kept in
easyeda-footprints/), finds the 90-degree rotation and offset that puts JLC's pads on
ours with matching pin numbers, and writes that. Pin numbers carry polarity, so
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
import sys
import time
import zipfile

from kicadlib import HERE, Netlist, pcb_footprints, pcb_outline, rotate, run

# JLC's footprints, one JSON per LCSC number. Tracked in git: the EasyEDA API
# rate-limits, and the files are small.
EASYEDA_CACHE = os.path.join(HERE, 'easyeda-footprints')



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
    m = re.match(r'([A-Za-z]+)(\d*)', r)
    return (m.group(1), int(m.group(2) or 0))


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


def ee_local(ee):
    """EasyEDA pads relative to the footprint origin, cartesian, in mm."""
    bx, by = ee['bbox']
    return [dict(q, x=q['x'] - bx, y=-(q['y'] - by)) for q in ee['pads']]


def solve(kpads, ee):
    """Best (R, X, Y, err) placing EasyEDA footprint `ee` on KiCad pads `kpads`."""
    local = ee_local(ee)
    byn = {}
    for p in kpads:
        byn.setdefault(p['num'], []).append((p['X'], p['Y']))
    allk = [(p['X'], p['Y']) for p in kpads]
    best = None
    for R in (0, 90, 180, 270):
        pairs = [(q, *rotate(q['x'], q['y'], R)) for q in local if q['num'] in byn]
        if not pairs:
            continue
        tx = sum(byn[q['num']][0][0] - lx for q, lx, ly in pairs) / len(pairs)
        ty = sum(byn[q['num']][0][1] - ly for q, lx, ly in pairs) / len(pairs)
        err = max(min(math.dist((lx + tx, ly + ty), k) for k in byn[q['num']]) for q, lx, ly in pairs)
        geo = max(min(math.dist((x + tx, y + ty), k) for k in allk)
                  for x, y in (rotate(q['x'], q['y'], R) for q in local))
        cand = (round(err, 2), round(geo, 2), R, tx, ty, err)
        if best is None or cand[:2] < best[:2]:
            best = cand
    if best is None:
        return None
    return dict(R=best[2], X=best[3], Y=best[4], err=max(best[0], best[1]))


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
        for q in ee_local(ee):
            x, y = rotate(q['x'], q['y'], s['R'])
            pad(ax, x + s['X'], y + s['Y'], q['w'], q['h'], s['R'] - q['rot'], q['shape'],
                facecolor='none', edgecolor='red', lw=0.6)
            if q['num'] == '1':
                ax.plot(x + s['X'], y + s['Y'], 'x', color='red', ms=4, mew=1)
        bx, by = ee['bbox']
        for line in ee['silk']:
            v = [float(z) for z in str(line).split()]
            pts = [rotate(v[i] * 0.254 - bx, -(v[i + 1] * 0.254 - by), s['R']) for i in range(0, len(v) - 1, 2)]
            ax.plot([p[0] + s['X'] for p in pts], [p[1] + s['Y'] for p in pts], color='#1f6fd1', lw=0.5)
        ax.text(s['X'], s['Y'] + 0.3, ref, fontsize=5, ha='center')
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
    a = ap.parse_args()

    base = os.path.splitext(os.path.abspath(a.project))[0]
    name = os.path.basename(base)
    pcb, sch = base + '.kicad_pcb', base + '.kicad_sch'
    out = os.path.join(os.path.dirname(base), 'jlcpcb')
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

    # BOM
    nl = Netlist.from_schematic(sch)
    refs = sorted((r for r in nl.parts if not r.startswith('#') and not skip(r)), key=refkey)
    plugin = plugin_lcsc(out)
    groups, nolcsc = {}, []
    for r in refs:
        p = nl.parts[r]
        code = nl.lcsc(r) or plugin.get(r, '')
        if not code:
            nolcsc.append(r)
            continue
        groups.setdefault((p['value'], p['footprint'].split(':')[-1], code), []).append(r)
    with open(os.path.join(pdir, f'BOM-{name}.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC', 'Quantity'])
        for (v, fp, code), rs in groups.items():
            w.writerow([v, ','.join(rs), fp, code, len(rs)])

    # CPL, corrected to JLC's footprints
    fps = pcb_footprints(pcb)
    placed, eefp, poor = {}, {}, []
    for (v, fp, code), rs in groups.items():
        ee = easyeda_footprint(code)
        for r in rs:
            if r not in fps:
                sys.exit(f'{r} is in the schematic but not on the board: update the PCB first')
            if fps[r]['layer'] != 'F.Cu':
                sys.exit(f'{r} is on the bottom side; bottom-side placement is not supported yet')
            s = solve(fps[r]['pads'], ee)
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
            w.writerow([r, nl.parts[r]['value'], fps[r]['lib'].split(':')[-1],
                        f"{s['X']:.4f}", f"{s['Y']:.4f}", f"{s['R'] % 360:.1f}", 'top'])
    if not a.no_check_image:
        check_image(os.path.join(out, 'placement-check.png'), fps, placed, eefp, pcb_outline(pcb))

    corrected = sum(1 for r, s in placed.items()
                    if (s['R'] - fps[r]['rot']) % 360 > 0.5 or
                    math.dist((s['X'], s['Y']), (fps[r]['x'], fps[r]['y'])) > 0.05)
    print(f'{out}')
    print(f'  BOM: {len(groups)} lines, {sum(len(v) for v in groups.values())} parts'
          + (f'; no LCSC number (left out): {" ".join(nolcsc)}' if nolcsc else ''))
    print(f'  CPL: {len(placed)} parts, {corrected} corrected for JLC footprints')
    for r, n, e in poor:
        print(f'  CHECK {r} ({n}): best fit {e:.2f} mm')


if __name__ == '__main__':
    main()
