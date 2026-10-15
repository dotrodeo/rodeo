# Rodeo satellite: encoder bank ×4

Four 12 mm rotary encoders with push switches, in a row on 25 mm centres, read by
one CH32V003 on the satellite chain. The second Rev B satellite, built like the
[pot bank](../rodeo-pot-bank-4/README.md) so the two sit side by side on one panel
with their knobs at the same height. Wire contract:
[`RODEO_SATELLITE_CHAIN_CONTRACT.md`](../rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md).

Specified 2026-10-14; not drawn yet.

## Board

Same construction as the pot bank:

* **Top side (faces the panel): the four encoders only**, through-hole, on 25 mm
  centres. The board hangs from the panel on the encoders' M7 bushings and nuts.
* **Bottom side: everything else**, all SMD: the MCU, IN and OUT connectors
  (vertical JST SH, cables leave from the back), the reverse-mount status LED and
  the passives. One SMD side; the encoders are through-hole on top.
* **Outline 20 × 100 mm**, matching the pot bank. Put the shafts at 9, 34, 59 and
  84 mm along the board, as on the pot bank, so the two boards' knobs line up when
  they sit next to each other.

## Encoders ENC1–ENC4: Bourns PEC11R-4215F-S0024 (C143790)

12 mm, 24 detents and 24 pulses per revolution (one full quadrature cycle per
detent), momentary push switch, 6 mm flatted shaft, threaded bushing for panel
mounting. 5,622 in stock at JLC on 2026-10-14.

* **Footprint `rodeo_encoder_bank:RotaryEncoder_Bourns_PEC11R-S_Vertical`**: KiCad's
  `RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm` with LCSC's PEC11R model
  attached (KiCad has no PEC11R footprint or model). The PEC11R's pattern matches:
  A, C, B on a 2.5 mm row, the switch pins 14.5 mm across, the shaft 7.5 mm from the
  A–B row. Its lugs are 11.4 mm apart against the footprint's 11.2 mm slots,
  which the 2.8 mm slots absorb. Checked by exporting a test board with the model
  and measuring where its pins and lugs come through.
* **Symbol `Device:RotaryEncoder_Switch_MP`**: pins A, B, C (common), S1, S2, and MP
  for the lugs.
* **Heights** (from the model): mounting face 6.5 mm above the PCB, shaft tip
  21.5 mm, so **15 mm of shaft above the mounting face, the same as the pot bank's
  RK09L**. Both boards hang from the panel at their mounting faces, so their knobs
  end up level.
* **Bushing:** M7 × 0.75 per the PEC11R family drawing (the model shows a 6.5 mm
  threaded section). **Confirm against the Bourns datasheet before drawing the
  panel**: panel hole Ø7.5 mm. Bourns' datasheet PDF wouldn't extract here.

## Pin budget: why the switches share one ADC pin

Four encoders need 12 inputs (A, B and switch each). The CH32V003F4P6 has 16
general-purpose pins once SWIO (PD1) and NRST (PD7) are set aside, and the chain
already takes 6 of them (two RX, two TX, `PRESENT`, the ID). The fix:

* **The four push switches share one ADC pin through a binary-weighted ladder.**
  The pin is pulled up by 10 kΩ; each switch pulls it toward GND through its own
  resistor (10 k, 20 k, 40.2 k, 80.6 k). All 16 combinations land on distinct ADC
  levels, at least 16 counts apart on the 10-bit ADC, so any set of pressed
  switches decodes. With nothing pressed the ladder draws nothing.
* **The ID divider's top resistor goes to `+3V3`** rather than a GPIO, which the
  contract allows (§1.6). It costs 0.2 mA.

That leaves exactly the 10 pins needed for 8 encoder lines, `PRESENT` and the LED.
Firmware polls A/B from a timer (≥2 kHz; a fast hand turn is ≈500 edges/s) with
a quadrature state machine, which also rejects contact bounce. EXTI isn't used:
the CH32V003 shares one interrupt line per pin number across ports.

## MCU: CH32V003F4P6 (C5187096), TSSOP-20

Symbol `MCU_WCH_RiscV:CH32V003FxPx`. Ring closing and connectors exactly as on
the pot bank (USART1 mapping 0 normally, mapping 3 when last; IN `DATA_FWD` on
both PD6 and PC1).

| Pin | Name | Connection |
|---|---|---|
| 9 | VDD | `+3V3`; C1 100 nF and C2 1 µF to `GND` at the pin |
| 7 | VSS | `GND` |
| 3 | PD6 | `SAT_RX`: J1 IN pin 3 |
| 11 | PC1 | `SAT_RX` (same net) |
| 2 | PD5 | R1 33 Ω → J2 OUT pin 3 |
| 10 | PC0 | R2 33 Ω → `DATA_RET` |
| 19 | PD2 | `PRESENT`: J2 OUT pin 5 (internal pull-up) |
| 20 | PD3 (A4) | `ID`: R3 10 kΩ from `+3V3`, R4 6.65 kΩ (R_ID, kind 6) to `GND` |
| 1 | PD4 (A7) | `SW_SENSE`: R5 10 kΩ from `+3V3`, C3 10 nF to `GND`, the switch ladder |
| 5 | PA1 | `ENC1_A` |
| 6 | PA2 | `ENC1_B` |
| 12 | PC2 | `ENC2_A` |
| 13 | PC3 | `ENC2_B` |
| 14 | PC4 | `ENC3_A` |
| 15 | PC5 | `ENC3_B` |
| 16 | PC6 | `ENC4_A` |
| 17 | PC7 | `ENC4_B` |
| 8 | PD0 | R6 1.5 kΩ → D1 status LED anode; cathode to `GND` |
| 18 | PD1 | `SWIO`: programming pad |
| 4 | PD7 | `NRST`: programming pad |

No spare pins.

## Encoder wiring

For each encoder n (1–4):

| Encoder pin | Connection |
|---|---|
| C (common) | `GND` |
| A | `ENCn_A`: 22 kΩ to `+3V3`, 4.7 nF to `GND` at the MCU pin |
| B | `ENCn_B`: 22 kΩ to `+3V3`, 4.7 nF to `GND` at the MCU pin |
| S1 | `GND` |
| S2 | ladder resistor to `SW_SENSE`: ENC1 10.0 kΩ, ENC2 20.0 kΩ, ENC3 40.2 kΩ, ENC4 80.6 kΩ (1 %) |
| MP (lugs) | `GND`: the bushings touch the panel, so ESD from a knob goes straight to ground |

* **22 kΩ pull-ups** draw 0.15 mA per closed contact, so even with all eight
  resting closed the board stays inside its 6 mA budget. With the 4.7 nF caps
  the rise time is ≈0.1 ms, well under the ≈2 ms between edges on a fast turn.
* **Ladder levels** (10 kΩ pull-up, ADC full scale 1023), for the firmware's
  decode table: none 1023; ENC4 910; ENC3 819; ENC3+4 745; ENC2 682; … ENC1 512; … all four
  356 (minimum gap 16 counts). The firmware takes the nearest of the 16 computed levels.

## Connectors, ID, LED, programming

As on the pot bank:

* **J1 IN, J2 OUT**: BM05B-SRSS-TB (C160391), vertical, bottom side; pin table in
  the pot bank README. `+3V3` and `GND` pass straight from J1 to J2: route them
  as 1 mm tracks or solid pours (the pot bank review found 0.2 mm tracks there
  eat most of the chain's voltage budget).
* **Board kind 6, `encoder bank ×4`, R_ID 6.65 kΩ.** Four `ENCODER` slots,
  encoders 1–4 in order; output slot 1 is `LED`, slots 2–4 `NONE`.
* **D1**: LTST-C230KRKT (C125107), reverse-mount through the board, footprint
  `rodeo_encoder_bank:LED_1206_3216Metric_ReverseMount_Hole1.8x2.4mm`.
* **J3**: SWIO, `+3V3`, `GND`, NRST pads, not fitted, excluded from the BOM.

## Power

MCU ≈2–3 mA, LED 0.8 mA, ID divider 0.2 mA, encoder pull-ups 0–1.2 mA, ladder
0–0.5 mA while pressing: within the 6 mA per-satellite budget.

## Parts

| Part | Refs | Qty | LCSC |
|---|---|---|---|
| PEC11R-4215F-S0024 | ENC1–ENC4 | 4 | C143790 |
| CH32V003F4P6 | U1 | 1 | C5187096 |
| BM05B-SRSS-TB | J1, J2 | 2 | C160391 |
| LTST-C230KRKT | D1 | 1 | C125107 |
| 100 nF 0402 | C1 | 1 | C1525 |
| 1 µF 0402 | C2 | 1 | C52923 |
| 10 nF 0402 | C3 | 1 | C15195 |
| 4.7 nF 0402 | A/B filter caps | 8 | C1538 |
| 33 Ω 0603 | R1, R2 | 2 | C23140 |
| 10 kΩ 0603 | R3, R5, ENC1 ladder | 3 | C25804 |
| 6.65 kΩ 0603 1 % | R4 | 1 | C60344 |
| 1.5 kΩ 0603 | R6 | 1 | C22843 |
| 22 kΩ 0603 | A/B pull-ups | 8 | C31850 |
| 20.0 kΩ 0603 1 % | ENC2 ladder | 1 | C4184 |
| 40.2 kΩ 0603 1 % | ENC3 ladder | 1 | C12447 |
| 80.6 kΩ 0603 1 % | ENC4 ladder | 1 | C23249 |

All in stock at JLC on 2026-10-14.
