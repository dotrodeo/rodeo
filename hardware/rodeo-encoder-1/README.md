# Rodeo satellite: encoder ×1

One 12 mm rotary encoder with a push switch and a status LED, read by a CH32V003 on
the satellite chain: board kind 3, the contract's single `ENCODER` slot. It's the
[encoder bank](../rodeo-encoder-bank-4/README.md) cut down to one encoder, as the
[single pot](../rodeo-pot-1/README.md) is the pot bank cut down to one pot: the same
encoder, MCU, connectors, switch circuit and firmware. Wire contract:
[`RODEO_SATELLITE_CHAIN_CONTRACT.md`](../rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md).

Specified 2026-10-26. No KiCad files yet; Derek lays it out.

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

Ideally the single pot's outline, so a pot and an encoder are interchangeable on a
panel drawing.

* **25 × 25 mm, 2 mm corners, shaft at the centre (12.5, 12.5).** Coordinates are from
  the top-left corner seen from the front, y down.
  * With the A/C/B row facing the bottom edge, as the pot's pins do: A/C/B at
    y = 20 (x = 10, 12.5, 15), switch pins at y = 5.5 (x = 10 and 15), mounting lugs
    at x = 6.9 and 18.1. The footprint's courtyard spans x 5.4–19.6, y 4.0–21.5
    around the shaft (17.5 × 14.2 mm before turning it).
  * **The single pot's LED spot (12.5, 3.0) doesn't work here:** the switch pins
    leave 0.3 mm between their pads and D1's lens hole. Put D1 on the lug axis,
    e.g. (3.0, 12.5), 9.5 mm from the shaft and 2 mm clear of the lug pads, or in a
    corner. Keep the shaft-to-LED distance at 9.5 mm or more, for the same
    ≈Ø14.8 mm knob limit as the single pot.
  * M2 hole H1 at (22.5, 2.5), the single pot's spot: clear of the encoder's
    courtyard.
* **Top side (faces the panel): the encoder only**, through-hole. **Bottom: every SMD
  part**, so SMT stays single-sided. Vertical chain connectors on the bottom.
* **Hung on the encoder's bushing and nut plus an M2 standoff at H1.** The mounting
  face is 6.5 mm above the board, so the standoff is 6.5 mm: a 6 mm standoff and a
  0.5 mm washer, since 6.5 mm M2 standoffs are rare.
* **Panel hole Ø7.5 mm** for the M7 × 0.75 bushing. The encoder bank's README still
  asks for that thread to be confirmed against Bourns' datasheet before a panel is
  drawn; it applies here too.
* **2 layers**, as on the single pot: `GND` poured on the panel side around the
  encoder's pins, the chain's `+3V3` on the back.

## Encoder SW1: Bourns PEC11R-4215F-S0024 (C143790)

Footprint `rodeo_encoder_bank:RotaryEncoder_Bourns_PEC11R-S_Vertical` with Bourns'
model: copy the encoder bank's `lib/` and `fp-lib-table`, keeping the library name, as
the single pot does with the pot bank's. Symbol `Device:RotaryEncoder_Switch_MP`.
5,268 in stock at JLC on 2026-10-26.

| Encoder pin | Connection |
|---|---|
| C (common) | `GND` |
| A | `ENC_A`: 22 kΩ to `+3V3`, 4.7 nF to `GND` at the MCU pin |
| B | `ENC_B`: 22 kΩ to `+3V3`, 4.7 nF to `GND` at the MCU pin |
| S1 | `GND` |
| S2 | 10.0 kΩ (1 %) to `SW_SENSE` |
| MP (lugs) | `GND`: the bushing touches the panel, so a zap at the knob drains to ground |

`SW_SENSE`: 10 kΩ from `+3V3`, 10 nF to `GND`. The 22 kΩ pull-ups draw 0.15 mA per
closed contact; with the 4.7 nF caps the edges rise in ≈0.1 ms, well inside a fast
turn's ≈2 ms between edges.

## MCU: CH32V003F4P6 (C5187096), TSSOP-20

As on the encoder bank: USART1 mapping 0 normally and mapping 3 when last, IN
`DATA_FWD` on both PD6 and PC1.

| Pin | Name | Connection |
|---|---|---|
| 9 | VDD | `+3V3`; 1 µF and 100 nF to `GND` at the pin |
| 7 | VSS | `GND` |
| 3 | PD6 | `SAT_RX`: J1 IN pin 3 |
| 11 | PC1 | `SAT_RX` (same net) |
| 2 | PD5 | 33 Ω → J2 OUT pin 3 (`DATA_FWD` out) |
| 10 | PC0 | 33 Ω → `DATA_RET` |
| 19 | PD2 | `PRESENT`: J2 OUT pin 5 (internal pull-up) |
| 20 | PD3 (A4) | `ID`: 10 kΩ from `+3V3`, 2.49 kΩ (R_ID, kind 3) to `GND` |
| 1 | PD4 (A7) | `SW_SENSE` |
| 5 | PA1 | `ENC_A` |
| 6 | PA2 | `ENC_B` |
| 8 | PD0 | 1.5 kΩ → D1 anode; cathode to `GND` |
| 18 | PD1 | `SWIO`: programming pad |
| 4 | PD7 | `NRST`: programming pad |
| others | | no connect |

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
  `LED_1206_3216Metric_ReverseMount_Hole1.8x2.4mm`, 1.5 kΩ from PD0: ≈0.8 mA. Expect
  the same two pad-to-lens-hole DRC errors as the other boards; exclude them.
* **J3:** `SWIO`, `+3V3`, `GND`, `NRST` pads on a 2.54 mm row, not fitted, DNP.
* **Silkscreen (back):** `RODEO ENCODER / Kind 3 · Rev B1 / 2026-10`,
  `CERN-OHL-W-2.0 rodeooh.rodeo/source`, `© 2026 DOT RODEO LLC`, `CHAIN IN` /
  `CHAIN OUT` with pin-1 marks, `S V G N` at J3.

## Power

MCU ≈2–3 mA, LED 0.8 mA, ID divider 0.26 mA (fed from 3V3, always on), encoder
pull-ups 0–0.3 mA, switch rung 0.17 mA while pressed: ≈4 mA, inside the contract's
6 mA per satellite.

## Firmware

* **A kind-3 map in `rodeo-satellite/src/boards.h`:** A/B on PA1/PA2, `SW_SENSE` on
  PD4, `PRESENT` on PD2, LED on PD0, ID fed from 3V3 (no feed pin). That's the
  kind-6 map's slot 1.
* **`satellite.c`:** kind 3 takes the encoder bank's path with one encoder, and the
  ladder initialised with one rung (`r_switch = {10000, absent, absent, absent}`).
  `sat_frame.h` already gives kind 3 one slot, reporting `ENCODER`.
* **Tests:** kind 3 found without driving PC2; the one-rung ladder decodes press and
  release, and debounces.

## Parts

| Part | Qty | LCSC |
|---|---|---|
| PEC11R-4215F-S0024 | 1 | C143790 |
| CH32V003F4P6 | 1 | C5187096 |
| BM05B-SRSS-TB | 2 | C160391 |
| LTST-C230KRKT | 1 | C125107 |
| 22 kΩ 0603 (A/B pull-ups) | 2 | C31850 |
| 4.7 nF 0402 (A/B) | 2 | C1538 |
| 10 kΩ 0603 1 % (ID top, `SW_SENSE` pull-up, switch rung) | 3 | C25804 |
| 10 nF 0402 (`SW_SENSE`) | 1 | C15195 |
| 2.49 kΩ 0603 1 % (R_ID) | 1 | C2930072 |
| 33 Ω 0603 | 2 | C23140 |
| 1.5 kΩ 0603 | 1 | C22843 |
| 100 nF 0402 | 1 | C1525 |
| 1 µF 0402 | 1 | C52923 |
| M2 mounting hole | 1 | — |

All in stock at JLC on 2026-10-26. Reference designators follow the schematic once
it's annotated.

## Open

* **LED position:** see [Board](#board); record it here once laid out.
* **The bushing thread (M7 × 0.75):** confirm against Bourns' datasheet before the
  panel is drawn.
