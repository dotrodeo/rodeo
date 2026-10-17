# Rodeo power board: USB-C

The default Rev B power input: a USB-C jack on a small board behind the panel,
cabled to the motherboard's power input (J2). It carries 5 V, USB data for the
Seed (USB-MIDI, audio, DFU flashing) and the charger's status LEDs. The
contract it meets is in [`rev-b.md`, "Power"](../rodeo-motherboard/docs/rev-b.md#power).

Designed 2026-10-12. Mounting: the jack and the LEDs sit on the I/O edge and come
through panel cut-outs; the 40 × 20 mm board sits on the
[rear rail](../mounting/README.md) on two M3 × 8 mm standoffs.

## Connections

**J1, USB-C receptacle (HRO TYPE-C-31-M-12, C165948).** Symbol
`Connector:USB_C_Receptacle_USB2.0_16P`, footprint
`Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12`.

| J1 pin | Net | Notes |
|---|---|---|
| A4, A9, B4, B9 (VBUS) | `VBUS` | |
| A1, A12, B1, B12 (GND), SH (shield) | `GND` | |
| A5 (CC1) | R1 5.1 kΩ to `GND` | marks the board as a sink: 5 V only, no USB-PD |
| B5 (CC2) | R2 5.1 kΩ to `GND` | |
| A6, B6 (D+) | `USB_DP_JACK` | both rows tied, for either plug orientation |
| A7, B7 (D−) | `USB_DN_JACK` | |
| A8, B8 (SBU) | no connect | |

**Protection, at the jack.**

| Part | Connection |
|---|---|
| U1 USBLC6-2SC6 (C7519), flow-through | pin 1 ← `USB_DN_JACK`, pin 6 → `USB_DN`; pin 3 ← `USB_DP_JACK`, pin 4 → `USB_DP`; pin 5 `VBUS`; pin 2 `GND` |
| D1 SMF5.0A TVS (C193402) | cathode `VBUS`, anode `GND` |
| C1 4.7 µF 25 V 0603 (C69335) | `VBUS` to `GND` |

**J2, to the motherboard: JST PH 8-pin vertical, B8B-PH-K-S (C157974).**
Same pin numbers as the motherboard's J2, so a standard PH cable (pin 1 to
pin 1) connects them. Pin 1 is at the right as seen in KiCad (I/O edge at the
top). The motherboard's J2 faces the front of the case, so the cable leaves it
forward and folds back to this board; with pin 1 here on that side, the cable
arrives without a twist.

| J2 pin | Net |
|---|---|
| 1, 6 | `VBUS` |
| 2, 5 | `GND` |
| 3 | `USB_DN` |
| 4 | `USB_DP` |
| 7 | `~PGOOD` |
| 8 | `~CHG` |

**Status LEDs: side-view LiteOn LTST-S220, at the panel edge.** Symbol
`Device:LED`, footprint `rodeo_power:LED_LiteOn_LTST-S220_SideView`. The lens is
on the side the silkscreen bracket wraps (the courtyard bulges there too); put
that side at the board edge.

| Part | Anode | Cathode |
|---|---|---|
| D1 green LTST-S220KGKT (C160469), power good | R3 1 kΩ to `VBUS` | `~PGOOD` (J2 pin 7) |
| D2 red LTST-S220KRKT (C284935), charging | R4 1 kΩ to `VBUS` | `~CHG` (J2 pin 8) |

≈3 mA each. Both light only with USB plugged in: power good when the
motherboard's charger accepts the input, red while the cell charges.

**Mounting:** two M3 holes (`MountingHole:MountingHole_3.2mm_M3`) at (5, 15)
and (35, 15) mm from the I/O edge's left end: rail row 1, 30 mm apart.

## Parts

| Part | Qty | LCSC |
|---|---|---|
| TYPE-C-31-M-12 USB-C | 1 | C165948 |
| USBLC6-2SC6 | 1 | C7519 |
| SMF5.0A | 1 | C193402 |
| 4.7 µF 25 V 0603 | 1 | C69335 |
| 5.1 kΩ 0402 | 2 | C25905 |
| 1 kΩ 0402 | 2 | C11702 |
| LTST-S220KGKT (green) | 1 | C160469 |
| LTST-S220KRKT (red) | 1 | C284935 |
| B8B-PH-K-S | 1 | C157974 |

## Layout

* J1 and both LEDs on the panel edge; J1's shell flush with the edge.
* U1 and D1 within a few mm of J1, D+/D− routed as a pair (90 Ω differential),
  short, to J2.
* The cable to the motherboard: PH 8-pin both ends, 24–26 AWG, under ≈30 cm, with
  D+/D− adjacent (they sit between the two grounds).
