---
name: jlc-production-files
description: Generate and check JLCPCB fabrication and assembly files (Gerbers, drill, BOM, corrected CPL, panels) and the STEP for renders, for any Rodeo board or rear panel. Use when Derek asks for production files, fab outputs, Gerbers, a panel, or a STEP/model for renders.
---

# Production files for JLC

All of it comes from `tools/kicad/jlc_fab.py` (see `tools/kicad/README.md` for flags).
Don't write one-off export scripts; if the tool lacks something, extend it in the repo.

## Before running

- Derek saves in KiCad first, **with zones refilled**: fills are plotted as saved.
- Run DRC and ERC on the board and say what they report; don't hide violations.
- The venv exists (`tools/kicad/.venv`, see README "Setup"); `--panel` also needs KiKit
  installed for KiCad's Python (`tools/kicad/.kikit`).
- Every part to be assembled has an LCSC number with JLC stock: the
  `review-kicad-board` skill's parts step.

## Run

```bash
cd tools/kicad
.venv/bin/python jlc_fab.py ../../hardware/<board>/<board>.kicad_pro            # one board
.venv/bin/python jlc_fab.py ../../hardware/<board>/<board>.kicad_pro --panel    # JLC panel
```

Production runs of the small assembled boards go as **panels** (`panel.json` next to the
project; table and house rules in the README). After `--panel`, open `panel/` in KiCad
and run DRC on it: no `hole_clearance` violation may name a `KiKit_MB` hole.

Rear FR4 panels: change the openings table in `rear_panels.py` (the one place they are
set), rerun it with KiCad's Python, then `jlc_fab.py` on each changed panel (Gerbers +
STEP only, no schematic):

```bash
/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 tools/kicad/rear_panels.py [board]
```

Panels marked `hand_edited` are skipped unless `--force`; don't force without asking.

## Check the CPL, every time

1. Read the tool's report: any part over `--tolerance` is a placement it couldn't fit.
2. Look at `jlcpcb/placement-check.png` (or `jlcpcb-panel/`): JLC's footprints, placed
   by our CPL, over our pads. Every pin 1 and every polarised part on its pads.
3. The cases that went wrong before:
   - **Bottom-side parts.** JLC flips a bottom footprint left-right and reads its
     rotation clockwise from the top; the tool writes that. Only a polarised part at
     90° proves it: 0° and 180° parts look right either way.
   - **Named pads** (jack T/R/S and the like) need a `PAD_ALIASES` entry
     (KiCad name to EasyEDA number), or they won't match.
   - **Footprints whose pads JLC numbers differently** (LTST-C230 LED, C125107, has its
     pads swapped): also `PAD_ALIASES`.
   - EasyEDA rate-limits (HTTP 403): footprints are cached in `easyeda-footprints/`
     (tracked; commit new ones). Delete a file to re-fetch it.
4. Tell Derek to confirm the placement in **JLC's own preview** after upload; that is
   the final word, and he does it.

## Outputs and git

- `jlcpcb/` outputs: check what the repo already tracks for that board and follow it.
- `<name>.step` beside the `.kicad_pcb` is **gitignored**: it's for Derek's Fusion renders
  (pads, silk, mask, models; origin at the board's top-left). `--no-step` skips it.
- Commit only on main with a pathspec; never sweep in Derek's uncommitted KiCad files.

## Saying what happened

Generated files are "ready to upload". After Derek uploads they are "uploaded to JLC,
not yet ordered". A board is ordered or at the fab **only when Derek says the order is
placed**: the website states hardware status publicly, and an inferred "at the fab"
once had to be reverted.
