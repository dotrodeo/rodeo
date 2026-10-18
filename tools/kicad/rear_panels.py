#!/usr/bin/env python3
"""Generate the Rev B rear panels: one FR4 panel per rear daughterboard.

  rear_panels.py                 # writes hardware/panels/<board>-panel/ for every board below
  rear_panels.py rodeo-midi      # just one
  rear_panels.py --force rodeo-power-barrel   # also overwrite a hand-edited panel

Run with KiCad's Python (it needs pcbnew):
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3

Each panel is a KiCad project with no parts: the outline and every opening on
Edge.Cuts, four M3 mounting holes, labels on the front silkscreen. Panels marked
hand_edited have been changed in KiCad and are skipped unless --force. Fab outputs
come from jlc_fab.py like any board. The panel system is described in
hardware/mounting/README.md; this table is the one place the openings are set.

Coordinates: panels are drawn as seen from outside, from behind the synth, so a
board's KiCad left end is on the panel's right. A board at x_b (from its KiCad
left end) lands at x = 5 + (board width - x_b) on its panel, which is the board
width plus 10 mm wide and centred on it. Heights are above the enclosure floor,
which is the panel's bottom edge.
"""
import json
import os
import shutil
import sys

import pcbnew

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'hardware', 'panels')
PROJECT_TEMPLATE = os.path.join(ROOT, 'hardware', 'rodeo-power-switch', 'rodeo-power-switch.kicad_pro')
MOUNTING_HOLE = ('/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/MountingHole.pretty',
                 'MountingHole_3.2mm_M3')

PANEL_H = 40.0        # mounting/rodeo_mount_common.scad: panel_h
HOLE_INSET = 5.0      # panel_hole_inset: from the top and bottom edges, and from each side
MARGIN = 5.0          # the panel overhangs its board by half a rail cell each side
CORNER_R = 0.5        # outline and slot corners (JLC's router)
LABEL_H = 1.5         # label text height
LABEL_GAP = 1.2       # label baseline clearance above an opening

# Openings per board (label_h: this panel's label text height, if not LABEL_H): (kind, x on the board from its KiCad left end, height above
# the floor, size, label). kind: 'circle' (size = diameter) or 'slot' (size =
# (width, height) with r 0.5 corners, or (width, height, corner radius)). Label goes above the opening; 'below:' puts it
# under. Heights and sizes are the mounting README's cut-out table.
BOARDS = {
    'rodeo-power-usbc': dict(width=40, openings=[
        ('slot', 20.0, 17.2, (11.0, 5.5, 1.5), 'USB-C'),
        ('circle', 10.0, 16.1, 2.5, 'below:PWR'),     # D1, green, power good
        ('circle', 30.0, 16.1, 2.5, 'below:CHG'),     # D2, red, charging
    ]),
    # Hand-edited in KiCad by Derek on 2026-10-18 (the KiCad file is the source now;
    # this entry is what it was generated from): the barrel opening is a 9.4 x 11.4 mm
    # rectangle, x 18.31-27.71, y 13.2-24.6 on the panel (the jack body's 9.0 x 11.0
    # face plus 0.2 mm a side); USB-C 11 x 5.5 mm, r 1.5; LED labels both above at
    # 1.0 mm; a drawn centre-positive symbol beside 'DC 7-20 V'.
    'rodeo-power-barrel': dict(width=60, hand_edited=True, openings=[
        ('slot', 15.0, 17.2, (13.5, 7.5), 'USB-C'),
        ('circle', 42.0, 22.1, 11.0, 'DC 7–20 V\nCENTER +'),
        ('circle', 30.14, 16.1, 2.5, 'PWR'),          # D1, green
        ('circle', 34.64, 16.1, 2.5, 'below:CHG'),    # D2, red
    ]),
    'rodeo-power-switch': dict(width=30, openings=[
        ('slot', 15.0, 18.5, (4.0, 3.5), 'POWER'),
    ]),
    'rodeo-midi': dict(width=40, openings=[
        ('circle', 13.0, 18.2, 6.0, 'MIDI IN'),
        ('circle', 27.0, 18.2, 6.0, 'MIDI OUT'),
    ]),
    'rodeo-audio-3.5mm': dict(width=60, label_h=1.3, openings=[
        # For players, not builders: 1/2 is the Daisy Seed's own codec, 3/4 the
        # motherboard's second codec (CS4272). The board's silkscreen keeps SEED/CODEC.
        ('circle', 46.55, 18.2, 6.0, 'AUDIO\nOUT 1/2'),   # J2, SEED OUT
        ('circle', 35.5, 18.2, 6.0, 'AUDIO\nOUT 3/4'),    # J3, CODEC OUT
        ('circle', 24.45, 18.2, 6.0, 'AUDIO\nIN 1/2'),    # J4, SEED IN
        ('circle', 13.4, 18.2, 6.0, 'AUDIO\nIN 3/4'),     # J5, CODEC IN
    ]),
}

ORIGIN = (100.0, 100.0)   # where the panel's top-left corner sits on the KiCad sheet

# Black soldermask both sides, white silkscreen: what JLC is asked for, and what
# KiCad's 3D view and the STEP export show. pcbnew can't set stackup colours, so
# this goes into the saved file's setup.
STACKUP = '''		(stackup
			(layer "F.SilkS" (type "Top Silk Screen") (color "White"))
			(layer "F.Paste" (type "Top Solder Paste"))
			(layer "F.Mask" (type "Top Solder Mask") (color "Black") (thickness 0.01))
			(layer "F.Cu" (type "copper") (thickness 0.035))
			(layer "dielectric 1" (type "core") (thickness 1.51) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
			(layer "B.Cu" (type "copper") (thickness 0.035))
			(layer "B.Mask" (type "Bottom Solder Mask") (color "Black") (thickness 0.01))
			(layer "B.Paste" (type "Bottom Solder Paste"))
			(layer "B.SilkS" (type "Bottom Silk Screen") (color "White"))
			(copper_finish "None")
			(dielectric_constraints no)
		)
'''


def mm(x, y):
    return pcbnew.VECTOR2I(pcbnew.FromMM(ORIGIN[0] + x), pcbnew.FromMM(ORIGIN[1] + y))


def edge_line(board, a, b):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(mm(*a)); s.SetEnd(mm(*b))
    s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(pcbnew.FromMM(0.05))
    board.Add(s)


def edge_arc(board, centre, start, end):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
    s.SetCenter(mm(*centre)); s.SetStart(mm(*start)); s.SetEnd(mm(*end))
    s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(pcbnew.FromMM(0.05))
    board.Add(s)


def rounded_rect(board, x0, y0, x1, y1, r):
    """Closed rounded rectangle on Edge.Cuts, corners of radius r."""
    edge_line(board, (x0 + r, y0), (x1 - r, y0))
    edge_line(board, (x1, y0 + r), (x1, y1 - r))
    edge_line(board, (x1 - r, y1), (x0 + r, y1))
    edge_line(board, (x0, y1 - r), (x0, y0 + r))
    edge_arc(board, (x1 - r, y0 + r), (x1 - r, y0), (x1, y0 + r))
    edge_arc(board, (x1 - r, y1 - r), (x1, y1 - r), (x1 - r, y1))
    edge_arc(board, (x0 + r, y1 - r), (x0 + r, y1), (x0, y1 - r))
    edge_arc(board, (x0 + r, y0 + r), (x0, y0 + r), (x0 + r, y0))


def circle(board, x, y, d):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_CIRCLE)
    s.SetCenter(mm(x, y)); s.SetEnd(mm(x + d / 2, y))
    s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(pcbnew.FromMM(0.05))
    board.Add(s)


def text(board, s, x, y, h=LABEL_H, bold=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s); t.SetPosition(mm(x, y)); t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(h), pcbnew.FromMM(h)))
    t.SetTextThickness(pcbnew.FromMM(h * 0.15 if not bold else h * 0.2))
    t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_CENTER); t.SetVertJustify(pcbnew.GR_TEXT_V_ALIGN_CENTER)
    board.Add(t)


def make(name, spec):
    W = spec['width'] + 2 * MARGIN
    proj = f'{name}-panel'
    d = os.path.join(OUT, proj)
    os.makedirs(d, exist_ok=True)
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(2)
    rounded_rect(board, 0, 0, W, PANEL_H, CORNER_R)

    for hx in (HOLE_INSET, W - HOLE_INSET):
        for hy in (HOLE_INSET, PANEL_H - HOLE_INSET):
            fp = pcbnew.FootprintLoad(*MOUNTING_HOLE)
            fp.SetPosition(mm(hx, hy)); fp.Reference().SetVisible(False); fp.Value().SetVisible(False)
            board.Add(fp)
    for i, fp in enumerate(board.GetFootprints(), 1):
        fp.SetReference(f'H{i}')

    for kind, xb, z, size, label in spec['openings']:
        x = MARGIN + spec['width'] - xb
        y = PANEL_H - z
        if kind == 'circle':
            circle(board, x, y, size)
            half = size / 2
        else:
            w, h = size[:2]
            r = size[2] if len(size) > 2 else CORNER_R
            rounded_rect(board, x - w / 2, y - h / 2, x + w / 2, y + h / 2, min(r, h / 2))
            half = h / 2
        below = label.startswith('below:')
        label = label[6:] if below else label
        lh = spec.get('label_h', LABEL_H)
        block = lh * (1 + 1.6 * label.count('\n'))      # KiCad's default line spacing
        ty = y + half + LABEL_GAP + block / 2 if below else y - half - LABEL_GAP - block / 2
        text(board, label, x, ty, h=lh)


    pcb = os.path.join(d, proj + '.kicad_pcb')
    board.Save(pcb)
    text_ = open(pcb).read()
    assert '(stackup' not in text_ and '\t(setup\n' in text_
    open(pcb, 'w').write(text_.replace('\t(setup\n', '\t(setup\n' + STACKUP, 1))
    pro = json.load(open(PROJECT_TEMPLATE))
    pro['meta']['filename'] = proj + '.kicad_pro'
    json.dump(pro, open(os.path.join(d, proj + '.kicad_pro'), 'w'), indent=2)
    print(f'{proj}: {W:.0f} x {PANEL_H:.0f} mm, {len(spec["openings"])} openings')


def main():
    args = sys.argv[1:]
    force = '--force' in args
    names = [a for a in args if a != '--force'] or list(BOARDS)
    for n in names:
        if BOARDS[n].get('hand_edited') and not force:
            print(f'{n}-panel: hand-edited in KiCad, left alone (--force to regenerate)')
            continue
        make(n, BOARDS[n])


if __name__ == '__main__':
    main()
