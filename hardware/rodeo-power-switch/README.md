# Rodeo power switch board

A latching push button on the back panel, cabled to the motherboard's power switch
connector (J3). Pressed in, it enables the motherboard's +5 V converter (U5's EN);
out, the synth is off and the battery still charges. Nothing but the switch and the
connector: R12 (10 kΩ, in series from the battery), R13 (47 kΩ to GND) and the
10 nF filter are on the motherboard (see
[`rev-b.md`, "Power"](../rodeo-motherboard/docs/rev-b.md#power)).

Designed 2026-10-16: drawn, laid out, routed and DRC-clean, fab outputs in
`jlcpcb/`. The plunger goes through the 1.6 mm panel's 4.0 × 3.5 mm slot
([`panels/rodeo-power-switch-panel`](../panels/rodeo-power-switch-panel/)) and is
usable bare; a 3D-printed cap is optional (see [Cap](#cap)).

## Connections

**J1, to the motherboard's J3: JST SH 2-pin vertical, BM02B-SRSS-TB (C160388)**,
symbol `Connector_Generic:Conn_01x02`, footprint
`Connector_JST:JST_SH_BM02B-SRSS-TB_1x02-1MP_P1.00mm_Vertical`. The same part as J3,
so a straight SH cable connects them. Polarity doesn't matter to the circuit, but
keep pin 1 to pin 1.

| J1 pin | Net | Goes to | Motherboard J3 |
|---|---|---|---|
| 1 | `SW_A` | SW1 pin 2 | `BATT_VCC` through R12 |
| 2 | `SW_B` | SW1 pin 4 | U5 EN, R13 to GND |

Mounting pads (MP) unconnected: this board has no ground.

**SW1: TK-6580A-1 (C528770)**, Yuandi, right-angle latching push switch, 5.8 × 5.8 mm
body. Symbol `rodeo_power_switch:TK-6580A-1`, footprint
`rodeo_power_switch:SW-TH_TK-6580A-1`, both from LCSC (the footprint's pads checked
against the datasheet). The switch was on the motherboard until 2026-10-08, so the
symbol, footprint and model are copies of the ones in its library, with two changes:
the pins are passive (LCSC had them unspecified), and the plunger outline is on F.Fab
rather than silkscreen, since it lies past the board edge.

| SW1 pins | Released (out) | Latched (in) |
|---|---|---|
| 1–3 | closed | open |
| 2–4 | open | **closed** |
| 5, 6 | extra terminals of 2 and 4 | |

Wire **pins 2 and 4** to J1 and put no-connect flags on 1, 3, 5 and 6; all six pads
are still soldered and hold the switch down. The table is the datasheet's circuit
diagram read with LCSC's pin names (`OFF_A`/`OFF_B` = 1/3, `ON_A` = 2 and 5,
`ON_B` = 4 and 6); XKB's drop-in equivalent (XKB5858-Z-E) draws the same diagram.
**Check it with a meter on a loose switch before ordering**: pins 2–4 should open
and close with each press, and nothing should connect 2 or 4 to 1 or 3 in either
position.

The contacts are silver-plated and carry 50–75 µA when on; that's why R13 is
47 kΩ and not the 1 MΩ it was.

## Board

* **Outline 30 × 20 mm**, I/O edge on top.
* **SW1 at (15, 3.70), rotation 180°**: plunger pointing up, body front flush with
  the I/O edge (the footprint's body front is 3.70 mm from its origin). The plunger
  then reaches 4.8 mm past the edge: 3.3 mm proud of the 1.6 mm panel when
  out, 1.3 mm when latched (2 mm travel).
* **M3 holes at (5, 15) and (25, 15)**: rail row 1, 20 mm apart. Check with
  `tools/kicad/grid_check.py rodeo-power-switch.kicad_pcb --rear top`.
* **J1** between the holes, behind the switch (around (15, 14)), cable leaving
  away from the panel.
* Two layers, two traces. No pour needed.

**Panel opening:** centred on SW1, **18.5 mm above the floor** (board top 15.6 mm,
plunger centre 2.90 mm above the board, measured from the 3D model): a 4.0 ×
3.5 mm slot, r 0.5, clearing the plunger's 3.4 × 2.9 mm shoulder where it passes
the panel. A cap would need the slot widened to its outline plus 0.3 mm a side.

## Panel (production runs)

`panel.json` is a 2 × 2 KiKit panel, 76 × 42 mm: four boards, the second row
turned 180° so every plunger faces out of the panel and the J1 edges face each
other across a 2 mm gap with no tabs. Each board hangs from two 5 mm mouse-bite
tabs, one on each 20 mm side, clear of the holes and J1; 5 mm rails on the left
and right carry the tooling holes and fiducials. `tools/kicad/jlc_fab.py
rodeo-power-switch.kicad_pro --panel` writes it and its JLC files to
`jlcpcb-panel/`. Fifty boards are 13 panels. The prototype run was ordered as
single boards.

## Cap

Optional since the panels went to 1.6 mm, and not drawn yet. If added: printed,
in OpenSCAD alongside the mounting parts (`hardware/mounting/`). It presses onto the plunger, so it is made to the
plunger's measured shape (datasheet and 3D model agree):

* first 2 mm past the body front: 3.4 mm wide × 2.9 mm tall
* from there to the tip (2.8 mm): 2.5 mm wide × 2.0 mm tall, centred 2.90 mm
  above the board

The cap's length sets how far it stands proud of the panel in both positions. Its
socket fits over the 2.5 × 2.0 mm tip only, so it stops on the step and never
bottoms out on the body.

## Library

Parts not in KiCad's own libraries are in `lib/`, registered in this folder's
`sym-lib-table` and `fp-lib-table` as `rodeo_power_switch`:

| File | What |
|---|---|
| `lib/rodeo_power_switch.kicad_sym` | `TK-6580A-1` symbol (LCSC) |
| `lib/rodeo_power_switch.pretty/SW-TH_TK-6580A-1.kicad_mod` | Footprint (LCSC) |
| `lib/rodeo_power_switch.3dshapes/SW-TH_TK-6580A-1.{step,wrl}` | 3D model (LCSC), referenced as `${KIPRJMOD}/lib/rodeo_power_switch.3dshapes/` |

## Silkscreen

Same scheme as the other daughterboards (see
[`silkscreen.md`](../docs/silkscreen.md)):

* **Back block:** `RODEO POWER SWITCH` / `Rev B1 · 2026-10` /
  `CERN-OHL-W-2.0` / `rodeooh.rodeo/source` / `© DOT RODEO LLC`.
* **Front, at J1:** `TO MOTHERBOARD SWITCH` (the motherboard labels J3 `SWITCH`),
  plus a `1` at pin 1.
* **Full-bleed `RODEO`** on the back, as on every board.

## Parts

| Part | Refs | Qty | LCSC |
|---|---|---|---|
| TK-6580A-1 | SW1 | 1 | C528770 |
| BM02B-SRSS-TB | J1 | 1 | C160388 |

Both in stock at JLC on 2026-10-16 (8,185 and 20,080). SW1 is through-hole; JLC
fits it with its through-hole assembly.
