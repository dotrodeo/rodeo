# Rodeo satellite: potentiometer ×1

One 9 mm pot with a status LED, read by a CH32V003 on the satellite chain: board
kind 1, the contract's single `POT` slot. For a knob that doesn't sit in a row of
four. It is the [pot bank](../rodeo-pot-bank-4/README.md) cut down to one pot, with
the same pot, MCU, connectors and firmware, so a single pot and a bank look and
behave the same on a panel. Wire contract:
[`RODEO_SATELLITE_CHAIN_CONTRACT.md`](../rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md).

Specified 2026-10-22. No KiCad files yet; Derek lays it out.

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

* **20 mm wide**, like the banks, so it fits the same panel spacing. The length is
  set by the layout: one pot body on top and the MCU, two chain connectors, the LED
  and the programming pads on the bottom. Expect around 30–35 mm.
* **Top side (faces the panel): the pot only**, through-hole. The board hangs from
  the panel on the pot's M9 bushing and nut. Panel hole Ø9.5 mm.
* **Bottom side: everything else**, all SMD, so JLC assembles one side only. The
  chain connectors are vertical, so the cables leave from the back.
* **The LED** is reverse-mount on the bottom, shining up through a hole in the board
  to a small panel hole or light pipe. Place it far enough from the shaft to clear the
  knob: at least the knob's radius plus about 2 mm (≥ 11 mm for an 18 mm knob).
  Record the shaft and LED positions here once laid out, for the panel drawing.
* **Layers and pours as on the pot bank:** `GND` poured on the top (panel) side,
  which carries only the pot's pins and the footprint's keep-out under the pot body;
  the chain's `+3V3` poured on the bottom, joined J1 → J2 by a 0.5 mm+ track.
  2 layers is enough.

## Pot RV1: Alps RK09L114001T (C470463), 10 kΩ, vertical, panel mount

Footprint `rodeo_pot_bank:Potentiometer_Alps_RK09L_Single_Vertical` (the pot bank's
copy of KiCad's footprint, with LCSC's model attached; copy it into this project's
library), symbol `Device:R_Potentiometer_MountingPin`.

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
| 9 | VDD | `+3V3`; C3 1 µF and C2 100 nF to `GND` at the pin |
| 7 | VSS | `GND` |
| 3 | PD6 | `SAT_RX`: J1 IN pin 3 |
| 11 | PC1 | `SAT_RX` (same net) |
| 2 | PD5 | R2 33 Ω → J2 OUT pin 3 (`DATA_FWD` out) |
| 10 | PC0 | R1 33 Ω → `DATA_RET` (the J1 pin 4 – J2 pin 4 pass-through) |
| 13 | PC3 | `PRESENT`: J2 OUT pin 5 (internal pull-up) |
| 6 | PA2 (A0) | `POT`: RV1 wiper, C1 100 nF to `GND` |
| 20 | PD3 (A4) | `ID`: R3 10 kΩ from PC2, R4 715 Ω (R_ID, kind 1) to `GND` |
| 12 | PC2 | `ID_DRV`: R3's top, driven high only while reading the ID |
| 16 | PC6 | R5 1.5 kΩ → D1 anode; cathode to `GND` |
| 18 | PD1 | `SWIO`: programming pad |
| 4 | PD7 | `NRST`: programming pad (internal pull-up) |
| others | | no connect |

## Connectors J1 (IN), J2 (OUT): JST SH 5-pin vertical, BM05B-SRSS-TB (C160391)

Bottom side, the pot bank's pinout:

| Pin | Signal | J1 IN | J2 OUT |
|---|---|---|---|
| 1 | GND | `GND` | `GND` |
| 2 | 3V3 | `+3V3` | `+3V3` (straight through) |
| 3 | `DATA_FWD` | `SAT_RX` (PD6, PC1) | R2 from PD5 |
| 4 | `DATA_RET` | `DATA_RET` | `DATA_RET` (straight through; R1 from PC0 taps it) |
| 5 | `PRESENT` | `GND` | PC3 |

Keep the `+3V3` and `DATA_RET` pass-throughs short and direct: every satellite
downstream depends on them.

## Board-kind ID

**Kind 1, `potentiometer`, R_ID = 715 Ω** (contract §1.6): 15 × 715 / 10,715 = 1.00.
One slot: input `POT`, output `LED` for D1.

## Status LED D1: LTST-C230KRKT (C125107), red, reverse-mount 1206

The pot bank's footprint (`LED_1206_3216Metric_ReverseMount_Hole1.8x2.4mm`, its
model flipped into the hole), R5 1.5 kΩ from PC6: ≈0.8 mA.

## Programming

J3: `SWIO`, `+3V3`, `GND`, `NRST` pads on a 2.54 mm row on the bottom, no header
fitted, off the BOM, as on the banks.

## Silkscreen

Per [`silkscreen.md`](../docs/silkscreen.md), back side, 0.8–1 mm text:

```
RODEO POTENTIOMETER
Kind 1 · Rev B1 · 2026-10
CERN-OHL-W-2.0 rodeooh.rodeo/source
© 2026 DOT RODEO LLC
```

plus `CHAIN IN` / `CHAIN OUT` at J1/J2 and `S V G N` with a pin-1 mark at J3.

## Power

MCU ≈2–3 mA, pot 0.33 mA, LED 0.8 mA, ID divider only while reading: ≈4 mA, inside
the contract's 6 mA per satellite.

## Firmware

* **A kind-1 map in `rodeo-satellite/src/boards.h`:** one pot on PA2, LED on PC6,
  `PRESENT` on PC3, ID divider driven from PC2. That's the kind-5 map's slot 1.
  `sat_frame.h` already gives kind 1 one slot, reporting `POT`.
* Nothing new in the SDK: a kind-1 satellite reports a `POT` slot like each of a
  bank's four.

## Parts

| Part | Refs | Qty | LCSC |
|---|---|---|---|
| RK09L114001T 10 kΩ, M9 bushing | RV1 | 1 | C470463 |
| CH32V003F4P6 | U1 | 1 | C5187096 |
| BM05B-SRSS-TB | J1, J2 | 2 | C160391 |
| LTST-C230KRKT | D1 | 1 | C125107 |
| 100 nF 0402 | C1, C2 | 2 | C1525 |
| 1 µF 0402 | C3 | 1 | C52923 |
| 33 Ω 0603 | R1, R2 | 2 | C23140 |
| 10 kΩ 0603 | R3 | 1 | C25804 |
| 715 Ω 0603 1 % | R4 | 1 | C2998136 |
| 1.5 kΩ 0603 | R5 | 1 | C22843 |

All in stock at JLC on 2026-10-22; everything but the pot, MCU, connectors, LED and
R4 is a basic part.

## Open

* **Does one bushing hold the board still?** The banks hang on four bushings; this
  board hangs on one, so tightening the nut or forcing the knob past its end stop
  can swing the board around the shaft, pulling on the chain cables. Check the RK09L
  drawing for an anti-rotation tab that the panel could key into. If it has none,
  add an M2 hole for a standoff (or a tab that rests against the panel's frame).
* **Name:** `rodeo-pot-1` follows the banks' `-4`; rename before the first files if
  you'd rather have `rodeo-potentiometer`, the Rev A board's name.
* **LED position and outline length**, set at layout and recorded above.
