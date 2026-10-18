# Rodeo KiCad tools

Command-line helpers used while designing the Rodeo boards. They work on saved
files, so save in KiCad first; the ones that write schematics need the
schematic closed in KiCad, or KiCad overwrites the change on its next save.

| Tool | What it does |
|---|---|
| `netlist.py` | List parts (value, footprint, LCSC number), nets, or one part's pins, for a whole project or one sheet. The fastest way to review a schematic against a connection table. |
| `lcsc_fill.py` | Set the `LCSC Part` field on symbols in a `.kicad_sch`: `lcsc_fill.py power.kicad_sch R12=C25744 J2=C157915`. |
| `jlc_parts.py` | JLC stock and part search: `jlc_parts.py stock C25744`, `jlc_parts.py search "BM05B-SRSS-TB"`. JLC's stock, not LCSC's; they differ, and only JLC's counts for assembly. |
| `grid_check.py` | Check a board against the mounting system (`hardware/mounting/README.md`): `--rear EDGE` for rear-I/O daughterboards (holes on the rail rows, 10 mm spacing, keep-outs, connector overhang), `--frame` for the motherboard's hole pattern. |
| `rear_panels.py` | Generate the Rev B back panels (`hardware/panels/`), one KiCad project per rear board, from its table of openings: outline and openings on Edge.Cuts, four M3 holes, silkscreen labels. Run with KiCad's Python; rerun after changing an opening, then `jlc_fab.py` for Gerbers. |
| `rodeo_tabs.py` | KiKit tabs plugin for `panel.json`: tabs at given positions along each board edge (see Panels). |
| `jlc_fab.py` | Everything JLCPCB needs, into `jlcpcb/` next to the project: Gerbers and drill (zipped), BOM, and a CPL **corrected to JLC's own footprints**, plus `placement-check.png` to confirm it by eye. With `--panel`, the same for a KiKit panel of the board, into `jlcpcb-panel/` (see Panels). |

## Setup

```bash
python3 -m venv tools/kicad/.venv
tools/kicad/.venv/bin/pip install -r tools/kicad/requirements.txt
```

`netlist.py`, `lcsc_fill.py` and `jlc_parts.py` need only the standard library.
`jlc_fab.py` needs the venv (easyeda2kicad, matplotlib). All of them find
`kicad-cli` on `PATH`, in `$KICAD_CLI`, or in the macOS app bundle.

## Panels

For production runs, the small assembled boards go to JLC as panels of copies of
the same board (never mixed designs). A board's `panel.json`, next to its
`.kicad_pro`, is a [KiKit](https://github.com/yaqwsx/KiKit) panelize config;
`jlc_fab.py --panel` runs KiKit on it into `panel/` (ignored by git; open it in
KiCad to inspect) and writes the panel's Gerbers, BOM, CPL and placement check
into `jlcpcb-panel/`. The panel keeps the board's references, so its silkscreen
is the board's; the BOM and CPL name each copy's parts `<ref>_<n>` (copies
numbered top to bottom, left to right), and the CPL is solved per copy, so
rotated copies place correctly.

| Board | Panel | Size |
|---|---|---|
| power switch | 2 × 2, second row turned 180° | 76 × 42 mm |
| MIDI | 2 × 2, second row turned | 96 × 52 mm |
| USB-C power | 2 × 2, second row turned | 96 × 42 mm |
| audio 3.5 mm | 2 × 2, second row turned | 136 × 62 mm |
| power barrel | 2 × 2, second row turned | 136 × 82 mm |
| pot bank, encoder bank | 4 side by side | 100 × 100 mm |

The rear boards' I/O edges (jacks and switches past the edge) face out of the
panel with no rail there, and their cable-connector edges face each other.

House rules:

* Mouse bites from JLC's guide: 0.6 mm holes, 0.95 mm pitch, 5 mm tabs, at least
  two tabs per board placed symmetrically, none on a connector, a part at the
  edge or a curved corner. Rails 5 mm on two sides with three tooling holes and
  three fiducials. Order with "panel by customer" and edge rails/fiducials added
  by customer.
* **The holes sit outside the board** (`offset` -0.3 mm, -0.45 mm on the banks).
  The boards' ground pours run 0.5 mm (rear boards) or 0.17 mm (banks) from the
  edge, with signal traces close to the edge too, so holes inside the outline cut
  copper. Outside, they leave a nub of about 0.3 mm at each tab: file it if it
  matters. Check with DRC on the panel: no `hole_clearance` violation may name a
  `KiKit_MB` hole.
* `rodeo_tabs.py` is a KiKit tabs plugin that puts tabs at given positions along
  each edge of the source board (KiKit's own tabs spread evenly and land on
  parts), following each copy's rotation. Its docstring has the format.

KiKit needs pcbnew, so it runs on KiCad's own Python (`$KICAD_PYTHON`, default the
macOS bundle's) from its own folder:

```bash
/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 \
    -m pip install --target tools/kicad/.kikit kikit
```

## Examples

```bash
cd tools/kicad
B=../../hardware/rodeo-motherboard

./netlist.py parts $B/rodeo-motherboard.kicad_sch --sheet "/MIDI/"
./netlist.py nets  $B/rodeo-motherboard.kicad_sch --sheet "/SD Slot/"
./netlist.py pin   $B/rodeo-motherboard.kicad_sch A1

./jlc_parts.py stock C160391 C14663

.venv/bin/python jlc_fab.py $B/rodeo-motherboard.kicad_pro
.venv/bin/python jlc_fab.py ../../hardware/rodeo-power-switch/rodeo-power-switch.kicad_pro --panel
```

## About the CPL correction

JLC places every part using the EasyEDA footprint for its LCSC number: that
footprint's origin goes at the CPL position, rotated by the CPL rotation. KiCad's
footprints often disagree on origin or orientation (JST and IDC connectors,
SOIC/TSSOP/SOT-23 and more), so KiCad's raw position export puts those parts in
the wrong place. `jlc_fab.py` fetches each part's EasyEDA footprint, finds the
rotation and offset that lands JLC's pads on ours with matching pin numbers (so
pin 1 and polarity are right), and writes that. Parts whose best fit is worse than
`--tolerance` are listed; look at them in `placement-check.png`.

JLC's footprints are kept in `easyeda-footprints/` (one JSON per LCSC number,
tracked in git) because the EasyEDA API rate-limits. Delete a file to re-fetch it.

LCSC numbers come from the symbol field `LCSC Part` (or `LCSC`, `JLCPCB Part #`,
`Supplier Part`), falling back to `jlcpcb/project.db` written by Bouni's KiCad
JLCPCB plugin, which the satellite boards use.

Limits: top-side parts only, rotations in 90° steps, and the board's zone fills
are plotted as saved (refill and save in KiCad first).
