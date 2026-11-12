---
name: align-3d-models
description: Fetch and align a 3D model for a part KiCad's library lacks (from LCSC/EasyEDA), and verify the alignment by measurement. Use when a Rodeo board has a part with no model, a model in the wrong place, or before a STEP goes to Derek for renders.
---

# Align a 3D model

KiCad lacks models for some Rodeo parts (PJ-102AH, B2P-VH and PJ-320D so far). LCSC
has them, but their STEP origins are arbitrary (often the body's centre), so a fetched
model is never right as it comes.

## 1. Fetch

```bash
tools/kicad/.venv/bin/easyeda2kicad --lcsc_id=C<number> --3d --output <scratch dir>/<part>
```

## 2. Measure the STEP, then place it

- Find the pins/pegs in the STEP's own coordinates (load it with a STEP reader, or read
  the vertices), not by eye.
- Copy the footprint into the **project's `lib/`** (never edit KiCad's stock library),
  put the model next to it, and set offset and rotation so the pins sit on the pads.
- KiCad's model `rotate` z: **−90 turns the model counter-clockwise as seen in the
  footprint editor's top view (y down), +90 clockwise.** It is easy to get backwards.

## 3. Verify by measurement

A render can look right and be wrong (a pot model once sat 180° out). Export the board
as an STL with the board's top-left as origin and locate the model's pins below the
board against the pad centres:

```bash
kicad-cli pcb export stl --user-origin <x>x<y>mm -o <scratch>/board.stl hardware/<b>/<b>.kicad_pcb
```

Pins below the board within a few tenths of a mm of their pads, on the right pads
(pin 1, polarity, a jack's T/R/S), is done. Then regenerate the STEP with
`jlc_fab.py` (see `jlc-production-files`) for Derek's renders.
