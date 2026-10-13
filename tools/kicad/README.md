# Rodeo KiCad tools

Command-line helpers used while designing the Rodeo boards. They work on saved
files, so save in KiCad first; the ones that write schematics need the
schematic closed in KiCad, or KiCad overwrites the change on its next save.

| Tool | What it does |
|---|---|
| `netlist.py` | List parts (value, footprint, LCSC number), nets, or one part's pins, for a whole project or one sheet. The fastest way to review a schematic against a connection table. |
| `lcsc_fill.py` | Set the `LCSC Part` field on symbols in a `.kicad_sch`: `lcsc_fill.py power.kicad_sch R12=C25744 J2=C157915`. |
| `jlc_parts.py` | JLC stock and part search: `jlc_parts.py stock C25744`, `jlc_parts.py search "BM05B-SRSS-TB"`. JLC's stock, not LCSC's; they differ, and only JLC's counts for assembly. |
| `jlc_fab.py` | Everything JLCPCB needs, into `jlcpcb/` next to the project: Gerbers and drill (zipped), BOM, and a CPL **corrected to JLC's own footprints**, plus `placement-check.png` to confirm it by eye. |

## Setup

```bash
python3 -m venv tools/kicad/.venv
tools/kicad/.venv/bin/pip install -r tools/kicad/requirements.txt
```

`netlist.py`, `lcsc_fill.py` and `jlc_parts.py` need only the standard library.
`jlc_fab.py` needs the venv (easyeda2kicad, matplotlib). All of them find
`kicad-cli` on `PATH`, in `$KICAD_CLI`, or in the macOS app bundle.

## Examples

```bash
cd tools/kicad
B=../../rev-b/rodeo-motherboard

./netlist.py parts $B/rodeo-motherboard.kicad_sch --sheet "/MIDI/"
./netlist.py nets  $B/rodeo-motherboard.kicad_sch --sheet "/SD Slot/"
./netlist.py pin   $B/rodeo-motherboard.kicad_sch A1

./jlc_parts.py stock C160391 C14663

.venv/bin/python jlc_fab.py $B/rodeo-motherboard.kicad_pro
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
