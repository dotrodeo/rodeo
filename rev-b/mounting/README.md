# Rodeo mounting system

Defined 2026-10-13. Inside a Rodeo, two printed parts sit on the enclosure's
solid floor and carry every board that isn't on the top panel:

* **The rear rail** holds the back panel and every rear-I/O daughterboard
  (power, power switch, audio jack board, MIDI jacks).
* **The motherboard frame** holds the motherboard, anywhere on the floor.

Both are open rib frames (the floor underneath is solid), screwed to the floor
through countersunk lugs. Boards mount on M3 heat-set inserts in the parts'
bosses, on M3 × 8 mm standoffs. Control satellites (pots, encoders, buttons) are
not part of this: they hang from the top panel on their own bushings (see
`rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md`).

## The rear rail ([`rodeo_rear_rail.scad`](rodeo_rear_rail.scad))

```
        back panel (in the groove)
  ════════════════════════════════════════  ← panel inner face, y = 0
   ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢    ← row 1, y = 15 mm
   ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢ ▢    ← row 2, y = 35 mm
   bosses every 10 mm along the panel
```

* A groove along the back takes the back panel's bottom edge. **The groove's
  inner wall ends at the panel's inner face: y = 0**, the line everything is
  measured from.
* **Two rows of insert bosses, at 15 mm and 35 mm from the panel**, one every
  10 mm along it (both settable).
* Length in 10 mm steps. A rail longer than the print bed prints in segments;
  each is screwed to the floor at both ends, and the panel in the groove keeps
  them in line.
* Renders in about a minute per 150 mm segment in OpenSCAD 2021.01.

```
openscad -D cells=30 -D segment=0 -o rail_0.stl rev-b/mounting/rodeo_rear_rail.scad
```

The console says how many segments the length needs; export each with `segment`.

### Rules for rear-I/O daughterboards

1. The **I/O edge** is one board edge. It sits on the panel's inner face, and
   the connectors overhang it by **0.3–0.5 mm** into the panel cut-outs. LEDs
   can sit just behind it and shine through a hole.
2. **M3 holes (`MountingHole_3.2mm_M3`) at 15 mm or 35 mm from the I/O edge**,
   spaced along the edge in multiples of 10 mm. At least two. Small boards use
   row 1; a deep connector (barrel jack, 1/4" jack) either sits between the
   row 1 holes or pushes them to row 2. A board deeper than the rail just overhangs it.
3. **No copper within 3.5 mm of a hole centre** on either side (screw head,
   standoff hex).
4. The outline is otherwise free.

```
tools/kicad/grid_check.py board.kicad_pcb --rear top      # top = the I/O edge
```

## The motherboard frame ([`rodeo_motherboard_frame.scad`](rodeo_motherboard_frame.scad))

Four insert bosses on the Rev B motherboard's hole pattern (**82 × 104 mm**,
M3, settable), braced diagonally, with four countersunk floor lugs. The
motherboard has no rear I/O (its SD card is internal and loaded over USB), so
the frame goes anywhere convenient for the cables.

```
openscad -o motherboard_frame.stl rev-b/mounting/rodeo_motherboard_frame.scad
tools/kicad/grid_check.py rev-b/rodeo-motherboard/rodeo-motherboard.kicad_pcb --frame
```

## Heights

| Z (mm, from the floor) | What |
|---|---|
| 0 | enclosure floor (the rail and frame sit on it) |
| 4 | rib tops, floor-lug tops |
| **6** | **boss tops: the datum.** The groove walls also stop here, clear of the row-1 standoffs. |
| 14 | board underside (M3 × 8 mm standoffs) |
| 15.6 | board top (1.6 mm boards) |

A rear connector's centre height above the floor is **15.6 + its centre height
above the PCB**: one rule for every board, so panel cut-outs line up.

## Hardware

| Part | Notes |
|---|---|
| M3 heat-set inserts | Ø4.0 mm hole, ≤ 5 mm long (e.g. M3 × 4 short). Only in the bosses a board uses. Other sizes: `insert_d`, `insert_depth`. |
| M3 × 8 mm male-female hex standoffs | screw into the inserts |
| M3 × 6 mm screws | board to standoff |
| M3 countersunk screws | rail and frame to the floor, length to suit the floor |

## Board status (2026-10-13)

| Board | Check | Status |
|---|---|---|
| `rodeo-motherboard` | `--frame` | **Passes** as drawn (holes 82 × 104 mm, keep-outs clear). |
| `rodeo-power-usbc` | `--rear top` | **Passes:** 40 × 20 mm, holes at (5, 15) and (35, 15), J1 overhang +0.35 mm. |
| `rodeo-power-barrel` | `--rear top` | **Passes:** 60 × 40 mm, holes at (5, 15) and (55, 15) either side of the barrel jack; J1 +0.35 mm, J3 +0.50 mm. |
