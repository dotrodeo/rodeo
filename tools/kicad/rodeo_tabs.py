"""KiKit tabs plugin: tabs at fixed positions along each edge of the source board.

KiKit's own "fixed" and "spacing" tabs spread evenly along an edge, which puts them
on whatever sits at the edge (a connector, a diode). This places them where the
board's panel.json says, in the source board's frame, and follows each copy's
rotation (KiKit's "alternation"), so a copy turned 180 degrees gets its tabs on the
right edges.

In panel.json:

    "tabs": {
        "type": "plugin",
        "code": "rodeo_tabs.EdgeTabs",
        "arg": "{\"ref\": \"H1\", \"left\": [7, 30], \"right\": [9], \"bottom\": [8, 52]}",
        "hwidth": "5mm", "vwidth": "5mm"
    }

Positions are mm along the edge from the source board's outline: from its top for
left/right, from its left for top/bottom. "ref" names a footprint (a mounting
hole) whose orientation shows each copy's rotation, relative to the top-left
copy (KiKit's row alternation never turns the first row). Leave it out for a
panel with no rotated copies. Tab width
is the tabs section's hwidth on left/right edges (the tab runs horizontally) and
vwidth on top/bottom, as in KiKit, or a per-edge "width" in mm in the arg. A tab on an edge that faces
nothing (no neighbour or rail) is dropped by KiKit.

Run by tools/kicad/jlc_fab.py --panel, which puts this folder on KiKit's PYTHONPATH.
"""
import json

from kikit.annotations import TabAnnotation
from kikit.plugin import TabsPlugin
from kikit.units import mm

# Edge -> inward direction in KiCad's frame (y down), and what an edge becomes
# when the copy is turned 180 degrees.
DIRS = {'left': (1, 0), 'right': (-1, 0), 'top': (0, 1), 'bottom': (0, -1)}
OPPOSITE = {'left': 'right', 'right': 'left', 'top': 'bottom', 'bottom': 'top'}


class EdgeTabs(TabsPlugin):
    def buildTabAnnotations(self, panel):
        spec = json.loads(self.userArg)
        ref = spec.get('ref')
        tabs = self.preset['tabs']
        widths = {'left': tabs['hwidth'], 'right': tabs['hwidth'],
                  'top': tabs['vwidth'], 'bottom': tabs['vwidth']}
        for e, w in spec.get('width', {}).items():
            widths[e] = w * mm

        # Each board's copy of the orientation reference, and its rotation
        anchors = [(fp.GetPosition(), fp.GetOrientationDegrees()) for fp in panel.board.GetFootprints()
                   if ref and fp.GetReference() == ref]
        rots = {}
        for i, s in enumerate(panel.substrates):
            minx, miny, maxx, maxy = s.bounds()
            rot = [r for p, r in anchors if minx <= p.x <= maxx and miny <= p.y <= maxy] if ref else [0]
            if len(rot) != 1:
                raise RuntimeError(f'rodeo_tabs: expected one {ref} on each board, found {len(rot)}')
            rots[i] = rot[0]
        first = min(rots, key=lambda i: (round(panel.substrates[i].bounds()[1]), panel.substrates[i].bounds()[0]))

        for i, s in enumerate(panel.substrates):
            minx, miny, maxx, maxy = s.bounds()
            rot = round(rots[i] - rots[first]) % 360
            if rot not in (0, 180):
                raise RuntimeError(f'rodeo_tabs: copies must be at 0 or 180 degrees, not {rot}')
            turned = rot == 180
            for edge in DIRS:
                along = (maxy - miny) if edge in ('left', 'right') else (maxx - minx)
                for pos in spec.get(edge, []):
                    e = OPPOSITE[edge] if turned else edge
                    d = along - pos * mm if turned else pos * mm
                    if e in ('left', 'right'):
                        origin = (minx if e == 'left' else maxx, miny + d)
                    else:
                        origin = (minx + d, miny if e == 'top' else maxy)
                    s.annotations.append(TabAnnotation(None, origin, DIRS[e], widths[edge]))
