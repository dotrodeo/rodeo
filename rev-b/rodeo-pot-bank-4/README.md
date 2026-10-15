# Rodeo satellite: pot bank ×4

Four 9 mm pots in a row on 25 mm centres, read by one CH32V003 on the satellite
chain. The first Rev B satellite and the first multi-slot one: it occupies four
`POT` slots in the chain frame. Wire contract:
[`RODEO_SATELLITE_CHAIN_CONTRACT.md`](../rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md).

Designed 2026-10-14.

## Board

* **Top side (faces the panel): the four pots only**, through-hole, on 25 mm
  centres. The board hangs from the panel on the pots' M9 bushings and nuts; it
  needs no standoffs.
* **Bottom side (faces into the case): everything else**, all SMD: the MCU, the
  IN and OUT connectors (vertical, so the chain cables leave from the back), the
  status LED and the passives. One SMD side keeps JLC assembly single-sided; the
  four pots are through-hole on top.
* **Outline 20 × 100 mm**, shafts at 9, 34, 59 and 84 mm along the board, on its
  centre line. Panel holes Ø9.5 mm (M9 bushings) on the same centres; the Rev A
  panel's Ø7.4 holes were for M7 pots and are too small.
* **Status LED** is a reverse-mount part on the bottom, shining up through a
  Ø2 mm hole in the board, so it's visible through a small panel hole (or a light
  pipe) at the front, and from inside the case.

## MCU: CH32V003F4P6 (C5187096), TSSOP-20

Symbol `MCU_WCH_RiscV:CH32V003FxPx`, footprint
`Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm`. Runs from the chain's 3V3 directly
(2.7–3.6 V is fine), no regulator, internal 24/48 MHz oscillator.

**Closing the ring.** USART1's TX and RX remap together, so the board uses two
mappings:

| Mapping | When | TX | RX |
|---|---|---|---|
| 0 (default) | something is plugged into OUT | PD5 → OUT `DATA_FWD` | PD6 ← IN `DATA_FWD` |
| 3 | last in the chain (OUT `PRESENT` high) | PC0 → `DATA_RET` tap | PC1 ← IN `DATA_FWD` |

IN `DATA_FWD` goes to both PD6 and PC1; whichever isn't the active RX is a plain
input. Mapping 1 (PD0/PD1) is avoided because PD1 is SWIO.

| Pin | Name | Connection |
|---|---|---|
| 9 | VDD | `+3V3`; C4 100 nF and C3 1 µF to `GND` at the pin |
| 7 | VSS | `GND` |
| 3 | PD6 | `SAT_RX`: IN pin 3 |
| 11 | PC1 | `SAT_RX` (same net) |
| 2 | PD5 | R4 33 Ω → OUT pin 3 (`DATA_FWD` out) |
| 10 | PC0 | R2 33 Ω → `DATA_RET` (the IN pin 4 – OUT pin 4 pass-through) |
| 13 | PC3 | `PRESENT`: OUT pin 5 (internal pull-up; low = something downstream) |
| 6 | PA2 (A0) | `POT_1`: RV1 wiper, C1 100 nF to `GND` |
| 19 | PD2 (A3) | `POT_2`: RV2 wiper, C2 100 nF to `GND` |
| 14 | PC4 (A2) | `POT_3`: RV3 wiper, C5 100 nF to `GND` |
| 5 | PA1 (A1) | `POT_4`: RV4 wiper, C6 100 nF to `GND` |
| 20 | PD3 (A4) | `ID`: R3 10 kΩ from PC2, R5 4.99 kΩ (R_ID) to `GND` |
| 12 | PC2 | `ID_DRV`: R3's top, driven high only while reading the ID |
| 16 | PC6 | R1 1.5 kΩ → D1 status LED anode; cathode to `GND` |
| 18 | PD1 | `SWIO`: programming pad |
| 4 | PD7 | `NRST`: programming pad (internal pull-up) |
| 1, 8, 15, 17 | PD4, PD0, PC5, PC7 | no connect (spare) |

## Connectors J1 (IN), J2 (OUT): JST SH 5-pin vertical, BM05B-SRSS-TB (C160391)

Symbol `Connector_Generic:Conn_01x05`, footprint
`Connector_JST:JST_SH_BM05B-SRSS-TB_1x05-1MP_P1.00mm_Vertical`. Bottom side.

| Pin | Signal | J1 IN | J2 OUT |
|---|---|---|---|
| 1 | GND | `GND` | `GND` |
| 2 | 3V3 | `+3V3` | `+3V3` (straight through) |
| 3 | `DATA_FWD` | `SAT_RX` (PD6, PC1) | R4 from PD5 |
| 4 | `DATA_RET` | `DATA_RET` | `DATA_RET` (straight through; R2 from PC0 taps it) |
| 5 | `PRESENT` | `GND` | PC3 |

Mounting pads (MP) to `GND`. Keep the `+3V3` and `DATA_RET` pass-throughs
short and direct: every satellite downstream depends on them.

## Pots RV1–RV4: Alps RK09L114001T (C470463), 10 kΩ, vertical, panel mount

Footprint `rodeo_pot_bank:Potentiometer_Alps_RK09L_Single_Vertical` (a copy of
KiCad's, which lists the 114001T, with LCSC's model attached), symbol
`Device:R_Potentiometer_MountingPin`, with the mounting lugs (MP) on `GND` so a
knob or panel zap drains through the bushing. **M9 × 0.75 threaded bushing** (Alps' RK09L drawing and
distributor data; not M7 as first written) and a 6 mm flatted shaft, 20 mm long:
the board hangs from the panel on M9 nuts. **Panel holes Ø9.5 mm.** From the model:
mounting face 10 mm above the PCB, shaft tip 25 mm, so 15 mm of shaft above the
mounting face. The footprint has copper keep-outs on the
top layer under each pot body; keep top-layer tracks and vias out of them. Ends across `+3V3` and `GND`, wiper to its ADC pin. With the
ADC referenced to VDD the reading is ratiometric, so supply sag along a long
chain doesn't move the knob values. Four 10 kΩ pots draw 1.3 mA together.

Pin 3 (the clockwise end on Alps/Alpha pots) goes to `+3V3` and pin 1 to `GND`,
so clockwise reads higher. Check the taper (linear) against the RK09L datasheet
before ordering. (The RK09K1130A6S first specified turned out to have no
bushing: a plain 6 mm shaft straight out of the body.)

## Board-kind ID

**Kind 5, `pot bank ×4`, R_ID = 4.99 kΩ** (contract §1.6). Four slots: each
input slot is `POT`, in board order from the IN end (RV1 next to J1 is slot 1);
output slot 1 is `LED` for D1, slots 2–4 take `NONE`. The ADC channels aren't in
board order (routing): slot 1 PA2, slot 2 PD2, slot 3 PC4, slot 4 PA1. The
firmware's kind-5 table follows that.

## Status LED D1: LTST-C230KRKT (C125107), red, reverse-mount 1206

On the bottom, lens through the footprint's 1.8 × 2.4 mm hole
(`rodeo_pot_bank:LED_1206_3216Metric_ReverseMount_Hole1.8x2.4mm`, KiCad's
footprint with its 1206 LED model flipped into the hole; LCSC has no model). R1 1.5 kΩ from PC6: ≈0.8 mA, within the
6 mA budget alongside the MCU and pots.

## Layout notes (as routed, 2026-10-14)

* **`GND` is a solid pour on the top (panel) side**, which carries nothing but the
  pots' pins and keep-outs. Every SMD ground pad on the bottom has a via to it.
* **`+3V3` is a pour on the bottom.** The chain's supply passes J1 → J2 through two
  large pour islands joined by a 0.5 mm track (≈15 mΩ for the pass-through, against
  the contract's 0.14 Ω per link).
* The first layout had the pours the other way round; the bottom `GND` pour broke
  into 12 islands around the SMD parts, and the chain's 3V3 ran on 0.2 mm track.
* D1's pads sit 0.175 mm from its lens hole (KiCad's footprint); DRC exclusions
  cover those two.

## Programming

Pads on the bottom: `SWIO` (PD1), `+3V3`, `GND`, and `NRST` (PD7), on a 2.54 mm
row so a WCH-LinkE can be held or pogo-pinned against them
(`Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical` with no header
fitted, or test-point pads).

## Parts

| Part | Refs | Qty | LCSC |
|---|---|---|---|
| CH32V003F4P6 | U1 | 1 | C5187096 |
| RK09L114001T 10 kΩ, M9 bushing | RV1–RV4 | 4 | C470463 |
| BM05B-SRSS-TB | J1, J2 | 2 | C160391 |
| LTST-C230KRKT | D1 | 1 | C125107 |
| 100 nF 0402 | C1, C2, C4, C5, C6 | 5 | C1525 |
| 1 µF 0402 | C3 | 1 | C52923 |
| 33 Ω 0603 | R2, R4 | 2 | C23140 |
| 10 kΩ 0603 | R3 | 1 | C25804 |
| 4.99 kΩ 0603 1 % | R5 | 1 | C23046 |
| 1.5 kΩ 0603 | R1 | 1 | C22843 |

All in stock at JLC on 2026-10-14; the resistors and capacitors are basic parts.
J3 (programming pads) has no LCSC number, so it stays off the BOM and isn't fitted.
