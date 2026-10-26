# Rodeo satellite: encoder ×1

One 12 mm rotary encoder with a push switch and a status LED, read by a CH32V003 on
the satellite chain: board kind 3, the contract's single `ENCODER` slot. It's the
[encoder bank](../rodeo-encoder-bank-4/README.md) cut down to one encoder, as the
[single pot](../rodeo-pot-1/README.md) is the pot bank cut down to one pot: the same
encoder, MCU, connectors, switch circuit and firmware. Wire contract:
[`RODEO_SATELLITE_CHAIN_CONTRACT.md`](../rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md).

Specified 2026-10-26; schematic and layout by Derek, 2026-10-26.

## Decisions

* **Same encoder as the bank: Bourns PEC11R-4215F-S0024** (24 detents, 24 pulses,
  push switch, 6 mm flatted shaft, threaded bushing). Its mounting face is 6.5 mm
  above the PCB and its shaft rises 15 mm above that, the same as the pots'. Every
  bank's and single board's knob ends up level on a panel.
* **Same MCU, CH32V003F4P6**, for the reasons in the single pot's spec: the 8-pin
  part can't close the ring, and one firmware image and one jig serve every
  satellite.
* **Pin map: the encoder bank's, with only slot 1.** Encoder 1's A/B lines, the
  switch sense pin, `PRESENT`, the LED and the ID divider all sit where the bank has
  them. The firmware's kind-3 map is the bank's cut to one slot.
* **The push switch keeps the bank's ladder circuit, with one rung.** `SW_SENSE` is
  pulled up by 10 kΩ and the switch pulls it toward `GND` through its own 10 kΩ: 1023
  counts released, about 512 pressed. A direct-to-ground switch on a digital pin
  would be one resistor fewer. But the bank's ladder decoder already debounces the
  ADC reading and counts presses, and it handles one switch as it stands (switches
  2–4 given as absent). So the firmware needs no new switch code.
* **The ID divider is fed from 3V3, as on the encoder bank,** so PC2 is never
  driven. Kind 3 reads 204 counts at once, and the firmware's second, driven read
  never happens.
* **An M2 standoff hole from the start.** The single pot needed one so the board
  can't turn around its one bushing; so does this board.

## Board

The single pot's outline and hole positions, so a pot and an encoder are nearly
interchangeable on a panel drawing (only the LED differs, by 1 mm).

* **25 × 25 mm, 2 mm corners.** Coordinates are from the top-left corner seen from
  the front (panel side), y down.
  * Encoder shaft at the centre, (12.5, 12.5). A/C/B row at y = 20 (x = 10, 12.5,
    15), switch pins at y = 5.5 (x = 10, 15), mounting lugs at x = 6.9 and 18.1.
  * LED D1 at (12.5, 2.0), **10.5 mm from the shaft**: 1 mm further than the single
    pot's, to clear the encoder's switch pins (1.3 mm between their pads and the
    lens hole). Knobs up to about Ø16.8 mm leave 0.5 mm of panel between knob and
    LED hole.
  * M2 hole H1 at (22.5, 2.5), as on the single pot.
  * Chain connectors J1 (IN) at (3, 20) and J2 (OUT) at (22, 20); programming pads
    J3 down the right edge, x = 22.85, y = 6.1–13.8.
* **Top side (faces the panel): the encoder only**, through-hole. **Bottom: every SMD
  part**, so SMT is single-sided. Vertical chain connectors on the bottom.
* **Hung on the encoder's bushing and nut plus an M2 standoff at H1.** The mounting
  face is 6.5 mm above the board, so the standoff is 6.5 mm: a 6 mm standoff and a
  0.5 mm washer, since 6.5 mm M2 standoffs are rare.
* **Panel hole Ø7.5 mm** for the M7 × 0.75 bushing (to confirm; see [Open](#open)).
* **2 layers:** `GND` poured on the panel side; the chain's `+3V3` runs as 0.4 mm
  tracks, ≈30 mΩ for the J1 → J2 pass-through against the contract's 0.14 Ω per link.
* **Silkscreen (back):** `RODEO ENCODER / Kind 3 · Rev B1 / 2026-10`,
  `CERN-OHL-W-2.0`, `rodeooh.rodeo/source`, `© 2026 DOT RODEO LLC`, `CHAIN IN` /
  `CHAIN OUT` with pin-1 marks, `S V G N` at J3.
* **DRC:** D1's two pads sit 0.175 mm from its own lens hole, as on every board with
  this LED; exclude them.

## Encoder SW1: Bourns PEC11R-4215F-S0024 (C143790)

Footprint `rodeo_encoder_bank:RotaryEncoder_Bourns_PEC11R-S_Vertical` with Bourns'
model. `lib/` and `fp-lib-table` are copied from the encoder bank, keeping the library
name; they hold this footprint and D1's. Symbol `Device:RotaryEncoder_Switch_MP`.
5,268 in stock at JLC on 2026-10-26.

| Encoder pin | Connection |
|---|---|
| C (common) | `GND` |
| A | `ENC_A`: R7 22 kΩ to `+3V3`, C5 4.7 nF to `GND` at the MCU pin |
| B | `ENC_B`: R6 22 kΩ to `+3V3`, C4 4.7 nF to `GND` at the MCU pin |
| S1 | `GND` |
| S2 | R8 10.0 kΩ (1 %) to `SW_SENSE` |
| MP (lugs) | `GND`: the bushing touches the panel, so a zap at the knob drains to ground |

`SW_SENSE`: R9 10 kΩ from `+3V3`, C6 10 nF to `GND`. The 22 kΩ pull-ups draw 0.15 mA per
closed contact; with the 4.7 nF caps the edges rise in ≈0.1 ms, well inside a fast
turn's ≈2 ms between edges.

## MCU: CH32V003F4P6 (C5187096), TSSOP-20

As on the encoder bank: USART1 mapping 0 normally and mapping 3 when last, IN
`DATA_FWD` on both PD6 and PC1.

| Pin | Name | Connection |
|---|---|---|
| 9 | VDD | `+3V3`; C2 1 µF and C3 100 nF to `GND` at the pin |
| 7 | VSS | `GND` |
| 3 | PD6 | `SAT_RX`: J1 IN pin 3 |
| 11 | PC1 | `SAT_RX` (same net) |
| 2 | PD5 | R4 33 Ω → J2 OUT pin 3 (`DATA_FWD` out) |
| 10 | PC0 | R2 33 Ω → `DATA_RET` |
| 19 | PD2 | `PRESENT`: J2 OUT pin 5 (internal pull-up) |
| 20 | PD3 (A4) | `ID`: R3 10 kΩ from `+3V3`, R5 2.49 kΩ (R_ID, kind 3) to `GND` |
| 1 | PD4 (A7) | `SW_SENSE` |
| 5 | PA1 | `ENC_A` |
| 6 | PA2 | `ENC_B` |
| 8 | PD0 | R1 1.5 kΩ → D1 anode; cathode to `GND` |
| 18 | PD1 | `SWIO`: programming pad |
| 4 | PD7 | `NRST`: programming pad |
| 12–17 | PC2–PC7 | no connect (PC2 stays undriven: the ID divider is fed from 3V3) |

## Connectors J1 (IN), J2 (OUT): JST SH 5-pin vertical, BM05B-SRSS-TB (C160391)

The banks' pinout: 1 `GND`, 2 `+3V3` (straight through), 3 `DATA_FWD` (`SAT_RX` on J1,
the 33 Ω from PD5 on J2), 4 `DATA_RET` (straight through, tapped by the 33 Ω from PC0),
5 `PRESENT` (`GND` on J1, PD2 on J2). Mounting pads unconnected.

## Board-kind ID

**Kind 3, `encoder with push switch`, R_ID = 2.49 kΩ** (contract §1.6):
15 × 2.49 / 12.49 = 2.99. One slot: input `ENCODER` (detent counter, pressed bit,
press counter), output `LED` for D1.

## Status LED D1, programming, silkscreen

* **D1:** LTST-C230KRKT (C125107), reverse-mount through the bank's
  `LED_1206_3216Metric_ReverseMount_Hole1.8x2.4mm`, R1 1.5 kΩ from PD0: ≈0.8 mA.
* **J3:** `SWIO`, `+3V3`, `GND`, `NRST` pads on a 2.54 mm row, not fitted, DNP.
* **Silkscreen:** see [Board](#board).

## Power

MCU ≈2–3 mA, LED 0.8 mA, ID divider 0.26 mA (fed from 3V3, always on), encoder
pull-ups 0–0.3 mA, switch rung 0.17 mA while pressed: ≈4 mA, inside the contract's
6 mA per satellite.

## Firmware

Done in `rodeo-satellite` (2026-10-26):

* **`SAT_BOARD_ENC_1` in `src/boards.h`:** A/B on PA1/PA2, `SW_SENSE` on PD4,
  `PRESENT` on PD2, LED on PD0, ID fed from 3V3 (no feed pin). That's the kind-6
  map's slot 1.
* **`firmware/satellite.c`:** kind 3 takes the encoder bank's path with one encoder
  (the loops run over the encoders a board has) and the ladder set up with one rung,
  `{10000, LADDER_ABSENT, LADDER_ABSENT, LADDER_ABSENT}`. `sat_frame.h` already
  gives kind 3 one slot, reporting `ENCODER`.
* **Tests:** kind 3 is found without driving PC2; the one-rung ladder decodes press
  and release at 1023 / 511 counts, debounces, and never reports switches 2–4.

## Fab outputs

`tools/kicad/jlc_fab.py`: `jlcpcb/` for single boards and `jlcpcb-panel/` for a 3 × 3
panel, 79 × 93 mm, built like the single pot's: rails top and bottom (the side edges
are taken by U1, C5, C6, R3, J1–J3 and H1), one 4 mm tab at 17.4 mm on each board's top
edge, and two on its bottom edge at 9 and 14.5 mm, clear of J1, R4 and J2. The lens
hole leaves 0.8 mm of board above it, so no tab goes near x = 10–15 mm on the top
edge. Check the polarised bottom parts (U1, D1, the JST connectors) in JLC's order
preview. Studio renders are in `renders/`.

## Parts

| Part | Refs | Qty | LCSC |
|---|---|---|---|
| PEC11R-4215F-S0024 | SW1 | 1 | C143790 |
| CH32V003F4P6 | U1 | 1 | C5187096 |
| BM05B-SRSS-TB | J1, J2 | 2 | C160391 |
| LTST-C230KRKT | D1 | 1 | C125107 |
| 22 kΩ 0603 | R6, R7 | 2 | C31850 |
| 4.7 nF 0402 | C4, C5 | 2 | C1538 |
| 10 kΩ 0603 1 % | R3, R8, R9 | 3 | C25804 |
| 10 nF 0402 | C6 | 1 | C15195 |
| 2.49 kΩ 0603 1 % | R5 | 1 | C22908 |
| 33 Ω 0603 | R2, R4 | 2 | C23140 |
| 1.5 kΩ 0603 | R1 | 1 | C22843 |
| 100 nF 0402 | C3 | 1 | C1525 |
| 1 µF 0402 | C2 | 1 | C52923 |
| M2 mounting hole | H1 | 1 | — |

All in stock at JLC on 2026-10-26. J3 (programming pads) is DNP and off the BOM.

## Open

* **Bring-up:** the ID read (204 counts, kind 3), the encoder's direction, the
  switch's two levels, and the LED.
* **The bushing thread (M7 × 0.75):** confirm against Bourns' datasheet before the
  panel is drawn.
