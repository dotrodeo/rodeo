---
name: review-kicad-board
description: Review a Rodeo KiCad schematic or board: connectivity against the design's connection table, LCSC numbers and JLC stock, mounting-grid fit, and the project's board-family conventions. Use when Derek asks to review, check, or audit a schematic, a board, or a BOM.
---

# Review a Rodeo board

Use the tools in `tools/kicad/` (they export a fresh netlist each run, so they see the
**saved** files: ask Derek to save first). Report findings as a list ranked by
consequence: wrong net or wrong part first, cosmetic last. Don't edit his schematic or
layout unless asked; when asked to set fields, `lcsc_fill.py` needs the schematic
**closed** in KiCad or KiCad overwrites the change on its next save.

## 1. Know what it is supposed to be

- The board's README and the design doc it cites (connection tables, pinouts).
- Which family it belongs to, and its pattern:
  - **Rear daughterboards** (MIDI, audio, power…): rear-rail M3 holes, I/O on one edge.
  - **Satellites** (pot bank, encoder bank, button bank): 20 mm strips hanging from the
    top panel, SMD on the bottom only, the satellite ring connector. **Satellites follow
    the satellite pattern, not the daughterboards'.** Derek lays out the button bank
    himself; review it, don't redesign it.
  - **Motherboard:** the frame's hole pattern.
- The main checkout is often on a feature branch ahead of main; check the branch before
  trusting a doc against the files.

## 2. Connectivity, sheet by sheet

```bash
cd tools/kicad
./netlist.py parts ../../hardware/<b>/<b>.kicad_sch --sheet "/<Sheet>/"
./netlist.py nets  ../../hardware/<b>/<b>.kicad_sch --sheet "/<Sheet>/"
./netlist.py pin   ../../hardware/<b>/<b>.kicad_sch U1
```

Check each connector's pins against its connection table, and the usual traps: single-
pin nets (a label that names nothing), unconnected pins that should be tied, power
pins on the right rail, pull-ups present and on the right side, and the ring/PRESENT
pin on satellite connectors.

## 3. Parts

```bash
./netlist.py parts ../../hardware/<b>/<b>.kicad_sch          # value, footprint, LCSC
./jlc_parts.py stock C25744 C157915 ...
./jlc_parts.py search "BM05B-SRSS-TB"
```

- Every assembled part has an `LCSC Part` (or the satellites' `jlcpcb/project.db`).
- **JLC's** stock, not LCSC's: only JLC's counts for assembly. Flag low stock and
  extended (vs basic) parts, since those add a fee per design.
- Value and footprint agree with the LCSC part (package, pitch, pin count).

## 4. Board

- DRC clean, or every violation explained.
- Mounting: `./grid_check.py ../../hardware/<b>/<b>.kicad_pcb --rear top|bottom|left|right`
  for a rear board (its I/O edge), `--frame` for the motherboard.
- Connectors overhang the edge by +0.3 to +0.5 mm, as `grid_check` lists.
- For anything going to JLC next, the `jlc-production-files` skill.
