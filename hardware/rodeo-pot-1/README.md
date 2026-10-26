# Rodeo satellite: potentiometer ×1

One 9 mm pot with a status LED, read by a CH32V003 on the satellite chain: board
kind 1, the contract's single `POT` slot. For a knob that doesn't sit in a row of
four. It is the [pot bank](../rodeo-pot-bank-4/README.md) cut down to one pot, with
the same pot, MCU, connectors and firmware, so a single pot and a bank look and
behave the same on a panel. Wire contract:
[`RODEO_SATELLITE_CHAIN_CONTRACT.md`](../rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md).

Specified 2026-10-22; schematic and layout by Derek, 2026-10-22 to 2026-10-25.

## Decisions

* **Same pot as the bank: Alps RK09L114001T** (10 kΩ linear, M9 × 0.75 bushing,
  6 mm flatted shaft). Its mounting face is 10 mm above the PCB and its shaft rises
  15 mm above that, as on the pot and encoder banks, so all their knobs end up level
  on a panel.
* **Same MCU: CH32V003F4P6 (TSSOP-20), not the 8-pin CH32V003J4M6.** The J4M6
  saves board space and a little cost, but it has no PC0. Without PC0, USART1 can't
  move its TX to the `DATA_RET` tap (mapping 3), so the board couldn't close the ring
  when it's last (contract §1.3, §1.5). Its pin 8 also combines SWIO with PD5, the
  chain's normal TX. Staying on the F4P6 keeps one firmware image and one
  programming jig for every satellite.
* **The status LED is fitted.** The contract lists kind 1's `LED` output as
  optional, but it's the same reverse-mount part the banks use: it gives the
  board's output slot something to drive and lets `IDENTIFY` point out which knob is
  which.
* **Pin map: the pot bank's, with only slot 1.** The wiper goes where the bank's
  pot 1 goes (PA2), and the chain, ID, LED and programming pins are the same. The
  firmware's kind-1 map is then the bank's map cut to one slot.

## Board

* **Outline 25 × 25 mm, 2 mm corner radius.** Coordinates are from the board's
  top-left corner as seen from the front (panel side), y down; the LED end is the top.
  * Pot shaft at the centre, (12.5, 12.5). Panel hole Ø9.5 mm.
  * LED D1 at (12.5, 3.0), 9.5 mm above the shaft.
  * M2 mounting hole H1 (`MountingHole_2.2mm_M2`) at (22.5, 2.5), 14.1 mm from the
    shaft.
  * Chain connectors J1 (IN) at (3, 20) and J2 (OUT) at (22, 20); programming pads
    J3 down the right edge, x = 23, y = 6–13.6.
* **Top side (faces the panel): the pot only**, through-hole.
* **Bottom side: everything else**, all SMD, so JLC's SMT is single-sided (the pot is
  soldered in the separate through-hole step). The chain connectors are vertical, so
  the cables leave from the back.
* **Hung from the panel on the pot's M9 bushing and nut, plus an M2 standoff at
  H1.** The standoff stops the board turning around the shaft when the nut is
  tightened or the knob is forced past its end stop. The pot's mounting face is
  10 mm above the PCB, so the standoff is 10 mm long (panel underside to board top).
* **The LED limits the knob to about Ø14.8 mm.** The Ø3.2 mm LED hole's edge is 7.9 mm
  from the shaft; a knob up to Ø14.8 mm leaves 0.5 mm of panel between them. (The
  banks' LEDs are 11.1 mm from the nearest shaft.)
* **2 layers, as on the pot bank:** `GND` poured on the top (panel) side around the
  pot's pins and its footprint's keep-outs; the chain's `+3V3` poured on the bottom,
  with a 0.4 mm track as well for the J1 → J2 pass-through.
* **Silkscreen (back):** `RODEO POTENTIOMETER / Kind 1 · Rev B1 / 2026-10`,
  `CERN-OHL-W-2.0 rodeooh.rodeo/source`, `© 2026 DOT RODEO LLC`, `CHAIN IN` /
  `CHAIN OUT` with pin-1 marks, and `S V G N` at J3.
* **DRC:** three edge-clearance errors around D1's lens hole, all expected. D1's
  own pads are 0.175 mm from the hole, as on the pot bank. A `GND` track passes
  0.45 mm from it, against a 0.5 mm rule; that's fine for the fab, but nudge it or
  exclude it.

## Pot RV1: Alps RK09L114001T (C470463), 10 kΩ, vertical, panel mount

Footprint `rodeo_pot_bank:Potentiometer_Alps_RK09L_Single_Vertical` (a copy of
KiCad's footprint with LCSC's model attached), symbol
`Device:R_Potentiometer_MountingPin`. `lib/` and `fp-lib-table` are copied from the
pot bank, keeping the library name `rodeo_pot_bank`; they hold this footprint and
D1's.

* Pin 3 (the clockwise end) to `+3V3`, pin 1 to `GND`, so clockwise reads higher.
  Wiper to PA2 (A0) with C1 100 nF to `GND`. The ADC is referenced to VDD, so the
  reading is ratiometric and supply sag along the chain doesn't move it.
* Mounting lugs (MP) to `GND`, so a zap at the knob drains through the bushing.
* **1,022 in stock at JLC on 2026-10-22**, shared with the pot bank's four per board.
  Check the count against a run before ordering.

## MCU: CH32V003F4P6 (C5187096), TSSOP-20

As on the pot bank: runs from the chain's 3V3 directly, internal oscillator, USART1
mapping 0 normally and mapping 3 when last, IN `DATA_FWD` on both PD6 and PC1.

| Pin | Name | Connection |
|---|---|---|
| 9 | VDD | `+3V3`; C2 1 µF and C3 100 nF to `GND` at the pin |
| 7 | VSS | `GND` |
| 3 | PD6 | `SAT_RX`: J1 IN pin 3 |
| 11 | PC1 | `SAT_RX` (same net) |
| 2 | PD5 | R4 33 Ω → J2 OUT pin 3 (`DATA_FWD` out) |
| 10 | PC0 | R2 33 Ω → `DATA_RET` (the J1 pin 4 – J2 pin 4 pass-through) |
| 13 | PC3 | `PRESENT`: J2 OUT pin 5 (internal pull-up) |
| 6 | PA2 (A0) | `POT`: RV1 wiper, C1 100 nF to `GND` |
| 20 | PD3 (A4) | `ID`: R3 10 kΩ from PC2, R5 715 Ω (R_ID, kind 1) to `GND` |
| 12 | PC2 | `ID_DRV`: R3's top, driven high only while reading the ID |
| 16 | PC6 | R1 1.5 kΩ → D1 anode; cathode to `GND` |
| 18 | PD1 | `SWIO`: programming pad |
| 4 | PD7 | `NRST`: programming pad (internal pull-up) |
| others | | no connect |

## Connectors J1 (IN), J2 (OUT): JST SH 5-pin vertical, BM05B-SRSS-TB (C160391)

Bottom side, the pot bank's pinout:

| Pin | Signal | J1 IN | J2 OUT |
|---|---|---|---|
| 1 | GND | `GND` | `GND` |
| 2 | 3V3 | `+3V3` | `+3V3` (straight through) |
| 3 | `DATA_FWD` | `SAT_RX` (PD6, PC1) | R4 from PD5 |
| 4 | `DATA_RET` | `DATA_RET` | `DATA_RET` (straight through; R2 from PC0 taps it) |
| 5 | `PRESENT` | `GND` | PC3 |

Keep the `+3V3` and `DATA_RET` pass-throughs short and direct: every satellite
downstream depends on them.

## Board-kind ID

**Kind 1, `potentiometer`, R_ID = 715 Ω** (contract §1.6): 15 × 715 / 10,715 = 1.00.
One slot: input `POT`, output `LED` for D1.

## Status LED D1: LTST-C230KRKT (C125107), red, reverse-mount 1206

The pot bank's footprint (`LED_1206_3216Metric_ReverseMount_Hole1.8x2.4mm`, its
model flipped into the hole), R1 1.5 kΩ from PC6: ≈0.8 mA.

## Programming

J3: `SWIO`, `+3V3`, `GND`, `NRST` pads on a 2.54 mm row along the right edge, no
header fitted, off the BOM and marked DNP, as on the banks.

## Silkscreen

As built (see [Board](#board)), following [`silkscreen.md`](../docs/silkscreen.md).

## Power

MCU ≈2–3 mA, pot 0.33 mA, LED 0.8 mA, ID divider only while reading: ≈4 mA, inside
the contract's 6 mA per satellite.

## Firmware

* **Kind 1 is in `rodeo-satellite`** (`SAT_BOARD_POT_1` in `src/boards.h`, set up in
  `firmware/satellite.c`, 2026-10-25): one pot on PA2, LED on PC6, `PRESENT` on PC3,
  the ID divider driven from PC2. That's the kind-5 map's slot 1. `sat_frame.h`
  already gives kind 1 one slot, reporting `POT`.
* Nothing new in the SDK: a kind-1 satellite reports a `POT` slot like each of a
  bank's four.

## Fab outputs

`tools/kicad/jlc_fab.py`: `jlcpcb/` for single boards and `jlcpcb-panel/` for a 3 × 3
panel, 79 × 93 mm. JLC's Standard assembly needs a panel; this board alone is 25 mm
square. The rails run top and bottom because the left and right edges are taken by
U1, J1, J2, J3 and H1. Each board has one 4 mm tab at 17.4 mm on its top edge, clear
of D1 and H1, and two on its bottom edge at 9 and 16 mm, between J1 and J2, so each
column hangs between the rails. Check the polarised bottom parts (U1, D1, the JST
connectors) in JLC's order preview. Studio renders are in `renders/`.

## Parts

| Part | Refs | Qty | LCSC |
|---|---|---|---|
| RK09L114001T 10 kΩ, M9 bushing | RV1 | 1 | C470463 |
| CH32V003F4P6 | U1 | 1 | C5187096 |
| BM05B-SRSS-TB | J1, J2 | 2 | C160391 |
| LTST-C230KRKT | D1 | 1 | C125107 |
| 100 nF 0402 | C1, C3 | 2 | C1525 |
| 1 µF 0402 | C2 | 1 | C52923 |
| 33 Ω 0603 | R2, R4 | 2 | C23140 |
| 10 kΩ 0603 | R3 | 1 | C25804 |
| 715 Ω 0603 1 % | R5 | 1 | C2998136 |
| 1.5 kΩ 0603 | R1 | 1 | C22843 |
| M2 mounting hole | H1 | 1 | — |

All in stock at JLC on 2026-10-22; everything but the pot, MCU, connectors, LED and
R5 is a basic part.

## Open

* Nothing. Bring-up: the ID read (kind 1 reads lowest of any kind, about 68 of
  1023 counts with PC2 driven), the pot's direction, and the LED.
