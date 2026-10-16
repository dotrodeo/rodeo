#!/usr/bin/env python3
"""Check a board against the Rodeo mounting system (hardware/mounting/README.md).

  grid_check.py board.kicad_pcb --rear top|bottom|left|right [--rows 15,35]
  grid_check.py board.kicad_pcb --frame [--span 80,100]

--rear EDGE   a rear-I/O daughterboard on the rear rail; EDGE is its I/O edge.
              Holes must be M3, at one of the rail's row depths from EDGE, and
              spaced along EDGE in multiples of 10 mm. Also lists every part
              that reaches EDGE and its overhang (connectors: +0.3 to +0.5 mm).
--frame       the motherboard on its frame: four M3 holes on the SPAN rectangle.

Both: no copper within 3.5 mm of a hole centre (M3 screw head, standoff hex).
Coordinates in the report are from the outline's top-left corner, x right, y down.
"""
import argparse
import math
import re
import sys

from kicadlib import pcb_footprints, pcb_outline, rotate

PITCH, HOLE_D, KEEPOUT_R, TOL = 10.0, 3.2, 3.5, 0.05


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('pcb')
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--rear', choices=['top', 'bottom', 'left', 'right'])
    g.add_argument('--frame', action='store_true')
    ap.add_argument('--rows', default='15,35', help='rail row depths, mm (default 15,35)')
    ap.add_argument('--span', default='82,104', help='frame hole spacing, mm (default 82,104)')
    a = ap.parse_args()
    t = open(a.pcb).read()
    ol = pcb_outline(a.pcb)
    xs = [p[0] for k, pts in ol for p in pts]
    ys = [p[1] for k, pts in ol for p in pts]
    x0, x1, ytop, ybot = min(xs), max(xs), max(ys), min(ys)
    w, h = x1 - x0, ytop - ybot
    rel = lambda X, Y: (X - x0, ytop - Y)
    ok = True

    def bad(msg):
        nonlocal ok
        ok = False
        print('  FAIL', msg)

    print(f'outline {w:.2f} x {h:.2f} mm')
    fps = pcb_footprints(a.pcb)
    holes = []
    for f in re.split(r'\n\t\(footprint ', t)[1:]:
        if 'MountingHole' not in f.split('"')[1]:
            continue
        ref = re.search(r'\(property "Reference" "([^"]*)"', f).group(1)
        drill = re.search(r'\(drill ([\d.]+)', f)
        u, v = rel(fps[ref]['x'], fps[ref]['y'])
        holes.append((ref, u, v, float(drill.group(1)) if drill else None, fps[ref]['x'], fps[ref]['y']))
    for ref, u, v, d, X, Y in holes:
        if d is None or abs(d - HOLE_D) > 0.11:
            bad(f'{ref} at ({u:.2f}, {v:.2f}): drill {d}, not M3 ({HOLE_D} mm)')

    if a.rear:
        rows = [float(r) for r in a.rows.split(',')]
        # depth from the I/O edge, and position along it
        dep = {'top': lambda u, v: v, 'bottom': lambda u, v: h - v,
               'left': lambda u, v: u, 'right': lambda u, v: w - u}[a.rear]
        along = {'top': lambda u, v: u, 'bottom': lambda u, v: u,
                 'left': lambda u, v: v, 'right': lambda u, v: v}[a.rear]
        if len(holes) < 2:
            bad('fewer than 2 mounting holes')
        for ref, u, v, d, X, Y in holes:
            dd = dep(u, v)
            row = min(rows, key=lambda r: abs(r - dd))
            if abs(dd - row) > TOL:
                bad(f'{ref} at ({u:.2f}, {v:.2f}): {dd:.2f} mm from the {a.rear} edge, rail rows are {rows}')
            else:
                print(f'  ok   {ref} at ({u:.2f}, {v:.2f}): rail row {row:g} mm')
        if holes:
            base = along(holes[0][1], holes[0][2])
            for ref, u, v, *_ in holes[1:]:
                s = along(u, v) - base
                if abs(s / PITCH - round(s / PITCH)) * PITCH > TOL:
                    bad(f'{ref}: {s:.2f} mm along the edge from {holes[0][0]}, not a multiple of {PITCH:g} mm')
    else:
        sx, sy = (float(s) for s in a.span.split(','))
        if len(holes) != 4:
            bad(f'{len(holes)} mounting holes, expected 4')
        else:
            us, vs = sorted(h_[1] for h_ in holes), sorted(h_[2] for h_ in holes)
            gx, gy = us[-1] - us[0], vs[-1] - vs[0]
            square = all(min(abs(h_[1] - us[0]), abs(h_[1] - us[-1])) < TOL and
                         min(abs(h_[2] - vs[0]), abs(h_[2] - vs[-1])) < TOL for h_ in holes)
            if not square or abs(gx - sx) > TOL or abs(gy - sy) > TOL:
                bad(f'holes span {gx:.2f} x {gy:.2f} mm, frame expects {sx:g} x {sy:g} on a rectangle')
            else:
                print(f'  ok   4 holes on a {sx:g} x {sy:g} mm rectangle, from ({us[0]:.2f}, {vs[0]:.2f})')

    for ref, u, v, d, X, Y in holes:
        hits = []
        for r, f in fps.items():
            if r == ref:
                continue
            for p in f['pads']:
                if math.dist((p['X'], p['Y']), (X, Y)) - max(p['w'], p['h']) / 2 < KEEPOUT_R:
                    hits.append(f'{r}.{p["num"]}')
        for xa, ya, xb, yb, wd in re.findall(r'\(segment\s*\(start ([-\d.]+) ([-\d.]+)\)\s*\(end ([-\d.]+) ([-\d.]+)\)\s*\(width ([\d.]+)\)', t):
            ax_, ay_, bx_, by_ = float(xa), -float(ya), float(xb), -float(yb)
            vx, vy = bx_ - ax_, by_ - ay_
            L = vx * vx + vy * vy
            s = 0 if L == 0 else max(0, min(1, ((X - ax_) * vx + (Y - ay_) * vy) / L))
            if math.dist((ax_ + s * vx, ay_ + s * vy), (X, Y)) - float(wd) / 2 < KEEPOUT_R:
                hits.append('track')
        for vxx, vyy, sz in re.findall(r'\n\t\(via\s*\(at ([-\d.]+) ([-\d.]+)\)\s*\(size ([\d.]+)\)', t):
            if math.dist((float(vxx), -float(vyy)), (X, Y)) - float(sz) / 2 < KEEPOUT_R:
                hits.append('via')
        if hits:
            bad(f'{ref}: copper inside the {KEEPOUT_R} mm keep-out: {", ".join(sorted(set(hits))[:12])}')

    if a.rear:
        print(f'parts reaching the {a.rear} edge (overhang > 0 = past the edge):')
        for r, f in sorted(fps.items()):
            blk = [b for b in re.split(r'\n\t\(footprint ', t)[1:] if f'"Reference" "{r}"' in b][0]
            pts = []
            for layer in ('"F.Fab"', '"F.CrtYd"'):
                for g_ in re.finditer(r'\(fp_(?:line|rect|arc|poly|circle)\b(.*?)\n\t\t\)', blk, re.S):
                    if layer in g_.group(1):
                        for px, py in re.findall(r'\((?:start|end|mid|xy|center) ([-\d.]+) ([-\d.]+)\)', g_.group(1)):
                            dx, dy = rotate(float(px), float(py), -f['rot'])
                            pts.append(rel(f['x'] + dx, f['y'] - dy))
                if pts:
                    break
            if not pts:
                continue
            over = {'top': -min(p[1] for p in pts), 'bottom': max(p[1] for p in pts) - h,
                    'left': -min(p[0] for p in pts), 'right': max(p[0] for p in pts) - w}[a.rear]
            if over > -1.0:
                print(f'       {r:6} {f["lib"].split(":")[-1][:45]:45} overhang {over:+.2f} mm')
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
