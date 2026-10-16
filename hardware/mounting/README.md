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
`instruments/rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md`).

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
openscad -D cells=30 -D segment=0 -o rail_0.stl hardware/mounting/rodeo_rear_rail.scad
```

The console says how many segments the length needs; export each with `segment`.

### Rules for rear-I/O daughterboards

1. The **I/O edge** is one board edge. It sits on the panel's inner face.
   Connectors sit with their front face (or, for jacks with a collar, their
   body) at the edge or up to 0.5 mm past it, and the **panel cut-out must let
   the mating plug seat fully**: the panel is 3 mm thick, so either the opening
   is big enough for the plug's moulding to reach the connector face, or the
   panel is counterbored from the outside (see [Back panel cut-outs](#back-panel-cut-outs)).
   LEDs can sit just behind the edge and shine through a hole.
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

### Back panel cut-outs

The back panel is 3 mm thick and its inner face is the boards' I/O edge, so
every connector face sits 2–3 mm inside the panel. A plug only goes in until its
moulding meets the panel, so each opening has to let the moulding through, or
be counterbored from the outside, or the plug stops short of full insertion
(on a TRS jack that puts the tip on the ring contact). Openings per board, with
**x measured along the I/O edge from the board's left end in KiCad's top view**
(seen from behind the synth, the board's left end is on the right) and **heights
above the enclosure floor** (board top at 15.6 mm: boss 6 + standoff 8 + PCB 1.6):

| Board | Connector | x | Centre height | Opening |
|---|---|---|---|---|
| `rodeo-power-usbc` | J1 USB-C | 20 | ≈17.2 | Through, sized for the plug overmould: ≈13.5 × 7.5 mm, r 2 |
| | D1, D2 LEDs | 10, 30 | ≈16.1 | Ø2.5 through |
| `rodeo-power-barrel` | J1 USB-C | 15 | ≈17.2 | As above |
| | J3 barrel jack | 42 | ≈22.1 | Ø11 through (5.5 × 2.1 plug bodies are ≈9–10 mm) |
| | D1, D2 LEDs | 30.14, 34.64 | ≈16.1 | Ø2.5 through |
| `rodeo-audio-3.5mm` | J2–J5 jacks | 46.55, 35.5, 24.45, 13.4 | ≈18.2 | Ø5.0 for the jack's collar, counterbored Ø9 × 1 mm deep from the outside |
| `rodeo-midi` | J2 IN, J3 OUT | 13, 27 | ≈18.2 | As the audio jacks |

* **3.5 mm jacks:** the PJ-320D's body is flush with the I/O edge and its 2 mm
  collar runs into the panel. The Ø9 counterbore brings the outside surface
  level with the collar tip, so a plug's moulding seats against the collar.
  Equivalently, make the panel 2 mm thick around the jacks.
* **USB-C:** the receptacle face is ≈2.6 mm inside the panel. An opening only
  the size of the receptacle (≈9.6 × 3.8) stops the overmould short and the plug
  won't latch; open it to the overmould size through the full thickness.
* **Barrel jack:** the jack face is ≈2.5 mm inside the panel; the Ø11 opening
  lets the plug body reach it.
* Check heights against the 3D assembly when the panel is drawn; the centre
  heights above come from the parts' models.

## The motherboard frame ([`rodeo_motherboard_frame.scad`](rodeo_motherboard_frame.scad))

Four insert bosses on the Rev B motherboard's hole pattern (**82 × 104 mm**,
M3, settable), braced diagonally, with four countersunk floor lugs. The
motherboard has no rear I/O (its SD card is internal and loaded over USB), so
the frame goes anywhere convenient for the cables.

```
openscad -o motherboard_frame.stl hardware/mounting/rodeo_motherboard_frame.scad
tools/kicad/grid_check.py hardware/rodeo-motherboard/rodeo-motherboard.kicad_pcb --frame
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
| `rodeo-audio-3.5mm` | `--rear top` | **Passes:** 60 × 30 mm, holes at (5, 15) and (55, 15); jack bodies flush with the I/O edge, collars 2 mm into the panel. |
| `rodeo-midi` | `--rear top` | **Passes:** 40 × 25 mm, holes at (5, 15) and (35, 15); jack bodies flush, collars +1.85 mm. |
| `rodeo-power-barrel` | `--rear top` | **Passes:** 60 × 40 mm, holes at (5, 15) and (55, 15) either side of the barrel jack; J1 +0.35 mm, J3 +0.50 mm. |
