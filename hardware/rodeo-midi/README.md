# Rodeo MIDI jack board

Two 3.5 mm TRS jacks, MIDI IN and MIDI OUT (TRS Type A), on the back panel,
cabled to the motherboard's J6. Passive: the optocoupler, its 220 Ω and reverse
diode, and the OUT jack's 100 Ω resistors all stay on the motherboard (see
[`rev-b.md`, "MIDI moves to a daughterboard"](../rodeo-motherboard/docs/rev-b.md#midi-moves-to-a-daughterboard)).
This board carries the jacks, the connector and ESD protection on OUT.

Designed 2026-10-15.

## Connections

**J1, to the motherboard's J6: JST SH 5-pin vertical, BM05B-SRSS-TB (C160391)**,
symbol `Connector_Generic:Conn_01x05`, footprint
`Connector_JST:JST_SH_BM05B-SRSS-TB_1x05-1MP_P1.00mm_Vertical`. Pin for pin with
J6, so a straight SH cable connects them.

| J1 pin | Net | Goes to |
|---|---|---|
| 1 | `GND` | J3 (OUT) sleeve only |
| 2 | `MIDI_IN_R` | J2 (IN) ring |
| 3 | `MIDI_IN_T` | J2 (IN) tip |
| 4 | `MIDI_OUT_R` | J3 (OUT) ring |
| 5 | `MIDI_OUT_T` | J3 (OUT) tip |

Mounting pads (MP) to `GND`.

**J2 (MIDI IN), J3 (MIDI OUT): PJ-320D (C431535)**, the audio board's jack.
Symbol `Connector_Audio:AudioJack4`, footprint
`rodeo_midi:Jack_3.5mm_PJ320D_Horizontal` (KiCad's footprint with LCSC's model,
copied from the audio board's library). R2 (the ring switch contact) unused on
both.

| Jack | T | R1 | S |
|---|---|---|---|
| J2 IN | `MIDI_IN_T` | `MIDI_IN_R` | **no connect** |
| J3 OUT | `MIDI_OUT_T` | `MIDI_OUT_R` | `GND` |

* **The IN sleeve must stay unconnected.** MIDI IN is opto-isolated: its current
  loop runs tip → opto LED → ring and never touches this board's ground. Grounding
  the sleeve closes a ground loop through the cable (Rev A did; a suspect for the
  line-out hum, Issue 4). Keep the IN jack's copper isolated from `GND` too: no
  ground pour around its pads closer than normal clearance, and no ESD diode to
  ground on IN.
* **ESD on OUT: D1, D2, PESD3V3S1BA (C7472904)**, bidirectional, SOD-323, symbol
  `Device:D_TVS`, footprint `Diode_SMD:D_SOD-323`: one from J3's tip and one from
  its ring to `GND`, at the jack. The same part as the audio board. OUT swings
  0–3.3 V, under the diode's 3.3 V working voltage; the motherboard's 100 Ω
  resistors limit the current if it clamps.

## Board

* **Outline 40 × 25 mm**, I/O edge on top. As laid out: J2 (IN) at x = 13 and
  J3 (OUT) at x = 27, IN on the left in KiCad's top view, so seen from behind the
  synth OUT is on the left and IN on the right. Footprints at y = 6.375, rotation
  −90°, as on the audio board: body flush with the I/O edge, collar 1.85 mm into
  the panel.
* **M3 holes at (5, 15) and (35, 15)**: rail row 1, 30 mm apart, clear of the
  jacks. Check with `tools/kicad/grid_check.py rodeo-midi.kicad_pcb --rear top`.
* **J1** behind the jacks, near the back edge, cable leaving upward.
* **Panel:** [`panels/rodeo-midi-panel`](../panels/rodeo-midi-panel/), Ø6 mm
  through for each jack's Ø5.5 collar, centre ≈18.2 mm above the floor (mounting
  README's cut-out table).
* Two layers, `GND` pour on the bottom around OUT and J1 only; keep it away from
  J2's pads and the IN traces.

## Parts

| Part | Refs | Qty | LCSC |
|---|---|---|---|
| BM05B-SRSS-TB | J1 | 1 | C160391 |
| PJ-320D | J2, J3 | 2 | C431535 |
| PESD3V3S1BA | D1, D2 | 2 | C7472904 |

All in stock at JLC on 2026-10-15.
