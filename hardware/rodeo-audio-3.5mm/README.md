# Rodeo audio jack board: 3.5 mm

Four stereo 3.5 mm TRS jacks for the motherboard's eight audio signals, on the
2×10 audio ribbon (motherboard J4). Passive apart from protection: every input
gets a series resistor and a clamp, every jack contact an ESD diode. The ribbon
contract is in [`rev-b.md`, "Audio interface and jack board"](../rodeo-motherboard/docs/rev-b.md#audio-interface-and-jack-board).

Designed 2026-10-14. A 1/4" variant will share this circuit with different
jacks. JLC stocks no right-angle 1/4" PCB jacks, so that board's jacks will be
hand-soldered.

```
            ┌──────────────── jack board ─────────────────┐
 ribbon     │                                             │   panel
 pin 2  ────┼── 100 Ω ──┬─────────────────── J2 tip       │   SEED OUT
 pin 4  ────┼── 100 Ω ──┼─┬───────────────── J2 ring      │
            │          TVS TVS                            │
 pin 6  ────┼───────────┬─────────────────── J3 tip       │   CODEC OUT
 pin 8  ────┼───────────┼─┬───────────────── J3 ring      │
            │          TVS TVS                            │
 pin 12 ────┼──┬────────── 470 Ω ─────────── J4 tip       │   SEED IN
 pin 14 ────┼──┼─┬──────── 470 Ω ─────────── J4 ring      │
            │ TVS TVS                                     │
 pin 16 ────┼──┬────────── 470 Ω ─────────── J5 tip       │   CODEC IN
 pin 18 ────┼──┼─┬──────── 470 Ω ─────────── J5 ring      │
            │ TVS TVS                                     │
 odd pins ──┼── GND ─────────────────────── all sleeves   │
            └─────────────────────────────────────────────┘
```

## Signals and levels

All eight signals are single-ended, about 4 Vpp at the jack, AC-coupled on the
motherboard side. Tip is left (channel 1 / A), ring is right (channel 2 / B).

A mono plug in a stereo jack grounds the ring: on an input the right channel
reads 0 V (firmware can copy left to right if it wants mono-to-both); on an
output it shorts the right output to ground, which the series resistors make
harmless (100 Ω here on the Seed outputs, 560 Ω on the motherboard for the codec).

## Connections

**J1, ribbon: 2×10 shrouded box header, DC3-2.54-20PAS (C2977593)**, the same
part as the motherboard's J4. Symbol `Connector_Generic:Conn_02x10_Odd_Even`,
footprint `Connector_IDC:IDC-Header_2x10_P2.54mm_Vertical`.

| Pin | Net | Pin | Net |
|---|---|---|---|
| 1, 3 … 19 | `GND` (all odd pins) | | |
| 2 | `AUDIO_OUT_1` | 12 | `JACK_IN_1` |
| 4 | `AUDIO_OUT_2` | 14 | `JACK_IN_2` |
| 6 | `CODEC2_OUT_A` | 16 | `CODEC2_IN_A` |
| 8 | `CODEC2_OUT_B` | 18 | `CODEC2_IN_B` |
| 10 | `+5V_A`: not used here, no connect | 20 | no connect |

**Jacks J2–J5: PJ-320D (C431535)**, the 3.5 mm stereo jack Rev A used. Symbol
`Connector_Audio:AudioJack4`, footprint
`Connector_Audio:Jack_3.5mm_PJ320D_Horizontal`. Pin R2 (the ring's switch
contact) is unused: no connect.

| Jack | Panel label | T (left) | R1 (right) | S |
|---|---|---|---|---|
| J2 | SEED OUT | R1 100 Ω from `AUDIO_OUT_1` | R2 100 Ω from `AUDIO_OUT_2` | `GND` |
| J3 | CODEC OUT | `CODEC2_OUT_A` direct | `CODEC2_OUT_B` direct | `GND` |
| J4 | SEED IN | R4 470 Ω to `JACK_IN_1` | R3 470 Ω to `JACK_IN_2` | `GND` |
| J5 | CODEC IN | R6 470 Ω to `CODEC2_IN_A` | R5 470 Ω to `CODEC2_IN_B` | `GND` |

**Protection: D1–D8, PESD3V3S1BA (C7472904)**, bidirectional 3.3 V ESD diodes,
SOD-323, 18 pF. Symbol `Device:D_TVS` (bidirectional, so orientation doesn't
matter), footprint `Diode_SMD:D_SOD-323`. Each to `GND`:

| Diode | Node | Diode | Node |
|---|---|---|---|
| D1 | J2 tip (at the jack) | D5 | `JACK_IN_2` (ribbon side of R3) |
| D2 | J2 ring (at the jack) | D6 | `JACK_IN_1` (ribbon side of R4) |
| D3 | J3 tip (at the jack) | D7 | `CODEC2_IN_B` (ribbon side of R5) |
| D4 | J3 ring (at the jack) | D8 | `CODEC2_IN_A` (ribbon side of R6) |

* **Inputs: resistor first, then the clamp.** ESD and any sustained
  out-of-range signal (a ±12 V Eurorack output) go through the 470 Ω before the
  diode clamps at about ±5.6 V, so the diode never sees more than
  (12 − 5.6) / 470 ≈ 14 mA and the motherboard side never more than ±5.6 V.
  R3–R6 are **0603** for the ESD pulse and the ≈90 mW of that worst case.
* **470 Ω on the inputs** costs ≈0.5 dB on the codec inputs (7.5 kΩ input
  resistor on the motherboard). The Seed's input impedance hasn't been measured,
  but it is several kΩ at least, so the loss there is similar or smaller.
* **Outputs:** the 3.3 V diode doesn't conduct below ±5 V, above the 4 Vpp
  (±2 V) full scale. 18 pF against the 100 Ω/560 Ω source is a pole in the MHz.
* **100 Ω on the Seed outputs** (rev-b.md "Filtering on audio I/O") protects
  them against a mono plug or a shorted cable and isolates cable capacitance.
  The codec outputs already have 560 Ω on the motherboard.

## Parts

| Part | Refs | Qty | LCSC |
|---|---|---|---|
| DC3-2.54-20PAS 2×10 box header | J1 | 1 | C2977593 |
| PJ-320D 3.5 mm stereo jack | J2–J5 | 4 | C431535 |
| PESD3V3S1BA | D1–D8 | 8 | C7472904 |
| 100 Ω 0402 | R1, R2 | 2 | C25076 |
| 470 Ω 0603 | R3–R6 | 4 | C23179 |

All in stock at JLC on 2026-10-14.

## Layout

* **J1 has the same orientation as the motherboard's J4 (rotation −90°):**
  the GND row and the key face the jacks, the signal row faces the front of the
  synth, and pin 1 is at the right-hand end in KiCad's top view. Both boards
  have their top edge toward the back panel, so identical orientations let a
  standard ribbon (both sockets crimped the same way) run flat between them with
  no twist. Turned 180°, the ribbon would need a half twist to keep pin 1 to
  pin 1. The keyed headers stop a wrong plug-in either way.
* **Panel order follows the ribbon:** in KiCad's top view, left to right CODEC
  IN, SEED IN, CODEC OUT, SEED OUT (J5, J4, J3, J2), so with pin 1 on the right
  every signal runs straight from header to jack. Seen from behind the synth
  this reads SEED OUT, CODEC OUT, SEED IN, CODEC IN, left to right.
* **Outline 60 × 30 mm**, jacks on 11.05 mm centres (x = 13.4, 24.45, 35.5,
  46.55) along the I/O edge, the box header behind them, M3 holes at (5, 15) and
  (55, 15) just outside the jack row. Check with
  `tools/kicad/grid_check.py rodeo-audio-3-5mm.kicad_pcb --rear top`.
* **Jack depth:** each PJ-320D's body is flush with the I/O edge (footprint at
  y = 6.375) and its 2 mm collar runs into the panel. Grid check reports that
  collar as ≈+1.85 mm of overhang, which is right for these jacks. The collar is
  Ø5.5 and goes through the 1.6 mm panel's Ø6 holes, so plugs seat on it; see
  [`panels/rodeo-audio-3.5mm-panel`](../panels/rodeo-audio-3.5mm-panel/) and the
  mounting README's [Back panel cut-outs](../mounting/README.md#back-panel-cut-outs).
* **3D model:** KiCad has none for the PJ-320D, so `lib/rodeo_audio.pretty`
  holds a copy of KiCad's footprint with LCSC's model (C431535) aligned to it.
* **Ground:** a solid `GND` pour on both layers, tied to every odd ribbon pin and
  every sleeve. This board has no other ground connection: the motherboard joins
  it to its plane at J4 only, the single audio ground point. The PJ-320D is a
  plastic jack with no bushing, so a metal panel doesn't touch the sleeves.
* Output diodes (D1–D4) right at the jack contacts; input resistors right at
  the jacks with their diodes (D5–D8) just behind them. Keep each input's track
  away from the outputs'.
