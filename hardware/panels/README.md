# Rodeo rear panels

One FR4 panel per Rev B rear board, screwed to the rear rail and top rail with
four M3 screws, Eurorack-style. The system (sizes, rails, where panels go) is in
[`../mounting/README.md`](../mounting/README.md#back-panels).

| Panel | For | Size | Openings |
|---|---|---|---|
| [`rodeo-power-usbc-panel`](rodeo-power-usbc-panel/) | [`rodeo-power-usbc`](../rodeo-power-usbc/) | 50 × 40 mm | USB-C, PWR and CHG LEDs |
| [`rodeo-power-barrel-panel`](rodeo-power-barrel-panel/) | [`rodeo-power-barrel`](../rodeo-power-barrel/) | 70 × 40 mm | DC barrel (7–20 V, centre +), USB-C, PWR and CHG LEDs |
| [`rodeo-power-switch-panel`](rodeo-power-switch-panel/) | [`rodeo-power-switch`](../rodeo-power-switch/) | 40 × 40 mm | POWER button |
| [`rodeo-midi-panel`](rodeo-midi-panel/) | [`rodeo-midi`](../rodeo-midi/) | 50 × 40 mm | MIDI IN, MIDI OUT |
| [`rodeo-audio-3.5mm-panel`](rodeo-audio-3.5mm-panel/) | [`rodeo-audio-3.5mm`](../rodeo-audio-3.5mm/) | 70 × 40 mm | SEED OUT, CODEC OUT, SEED IN, CODEC IN |

**Generated, not drawn.** Every panel comes from
[`tools/kicad/rear_panels.py`](../../tools/kicad/rear_panels.py), which holds the
opening table (positions from each board's KiCad layout, sizes from the mounting
README). To change a panel, edit the table and rerun it with KiCad's Python:

```
/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 tools/kicad/rear_panels.py
tools/kicad/.venv/bin/python tools/kicad/jlc_fab.py hardware/panels/<panel>/<panel>.kicad_pro
```

Edits made in KiCad are overwritten by the next run.

**Ordering at JLC:** Gerbers only (`jlcpcb/production_files/GERBER-*.zip`), 1.6 mm
FR4, **black soldermask, white silkscreen**, no assembly. The panels have no copper, and the mounting holes are unplated.

Generated 2026-10-17 and checked in the 3D assembly with every board: each opening
sits on its connector and each hole has a rail insert behind it.
