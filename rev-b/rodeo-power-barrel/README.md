# Rodeo power board: USB-C + barrel jack

Everything the [USB-C board](../rodeo-power-usbc/README.md) does, plus a barrel
jack for a 9–12 V adapter and a separate 5 V output for LED strips. When the
adapter is plugged in it powers the motherboard; USB stays connected for data
and takes over if the adapter is removed. The contract it meets is in
[`rev-b.md`, "Power"](../rodeo-motherboard/docs/rev-b.md#power).

Designed 2026-10-12. Mounting as the USB-C board: jacks and LEDs on one edge,
two M2.5 standoffs.

```
Barrel 7–20 V ─ Q1 reverse-polarity P-FET ─ VBARREL ─┬─ U3 TPS54308 ─ +5V_BRL ─┐
               (TVS D4, bulk)                       │                        ├─ U5 TPS2121 ─ VIN ─ J2 (motherboard)
USB-C VBUS ─────────────────────────────────────────┼────────────────────────┘   barrel first
                                                    └─ U4 TPS54302 ─ +5V_AUX ─ F1 3 A PTC ─ J4 (LED strips)
```

## Adapter

5.5 × 2.1 mm plug, **centre positive**, 7–20 V (9 V or 12 V adapters). Full load
is the motherboard (≤1.5 A at 5 V) plus the aux output (≤3 A at 5 V), about
25 W in: **12 V ≥ 2.5 A or 9 V ≥ 3 A**. Smaller adapters work if the aux output
is lightly loaded.

## Connections

**USB-C section:** identical to the USB-C board (J1, R1/R2 5.1 kΩ CC pull-downs,
U1 USBLC6 flow-through on D+/D−, D1 SMF5.0A and C1 4.7 µF on `VBUS`), except that
`VBUS` goes to the mux (U5 IN2) instead of straight to J2.

**J3, barrel jack: CUI PJ-102AH (C3096093), 5 A, 2.0 mm pin.** Symbol
`Connector:Barrel_Jack_Switch`, footprint
`Connector_BarrelJack:BarrelJack_CUI_PJ-102AH_Horizontal`.

| J3 pin | Net |
|---|---|
| 1 (centre pin) | `BARREL_IN` |
| 2 (sleeve) | `GND` |
| 3 (switch) | no connect |

**Reverse-polarity protection and input filtering.**

| Part | Connection |
|---|---|
| Q1 AO4407A P-FET, 30 V 12 A (C2841482), symbol `rodeo_power:AO4407A`, SOIC-8 | drain (5–8) `BARREL_IN`; source (1–3) `VBARREL`; gate (4) → R5 100 kΩ to `GND` |
| D5 BZT52C12 12 V Zener (C173429) | cathode `VBARREL`, anode Q1 gate: holds V_GS within −12 V |
| D4 SMBJ20A TVS (C364296) | cathode `VBARREL`, anode `GND` (after Q1, so a reversed adapter can't drive it) |
| C2 47 µF 35 V electrolytic (C72522) | `VBARREL` (+) to `GND`: bulk, damps the adapter cable |

With the adapter the right way round, Q1's body diode conducts first, then its
gate is pulled 9–12 V below its source and it turns fully on (≈14 mΩ). Reversed,
the body diode blocks and the gate sits at the source, so Q1 stays off.

**U3, motherboard 5 V: TPS54308 (C168839), 3 A, fixed 350 kHz.** Symbol
`Regulator_Switching:TPS54302` (same pinout; set Value to TPS54308).

| U3 pin | Connection |
|---|---|
| 3 VIN | `VBARREL`, with C3, C4 10 µF 50 V 1206 (C513774) + C5 0.1 µF 50 V 0402 (C307331) to `GND` at the pin |
| 1 GND | `GND` |
| 5 EN | no connect (internal pull-up enables it) |
| 6 BOOT | C6 0.1 µF 50 V (C307331) to SW |
| 2 SW | L1 10 µH (Sunlord MWSA0603S-100MT, C132141) → `+5V_BRL` |
| 4 FB | R6 100 kΩ from `+5V_BRL`, R7 13.3 kΩ to `GND`: 0.596 × (1 + 100/13.3) = 5.08 V |
| output | C7, C8 22 µF 25 V 0805 (C45783) on `+5V_BRL` |

**U4, LED aux 5 V: TPS54302 (C311983), 3 A.** Same circuit and values as U3
(C9–C14, L2, R8 100 kΩ, R9 13.3 kΩ), output `+5V_AUX`. The TPS54302
pulse-skips at light load, which is efficient and doesn't matter for LEDs; the
motherboard supply uses the fixed-frequency TPS54308 instead.

| Part | Connection |
|---|---|
| F1 3 A PTC, 6 A trip, 1812 (TLC-MSMD300, C262035) | `+5V_AUX` → `AUX_OUT` |
| J4 JST VH 2-pin vertical, B2P-VH (C160315), 10 A | pin 1 `AUX_OUT`, pin 2 `GND` |

**U5, source mux: TPS2121 (C485916), barrel first.** Symbol
`rodeo_power:TPS2121RUXR`. Same configuration the motherboard used before the
mux moved here.

| U5 pin | Connection |
|---|---|
| 7 IN1 (priority) | `+5V_BRL`, C15 1 µF (C52923) to `GND` |
| 2 IN2 | `VBUS`, C16 1 µF (C52923) to `GND` |
| 6 PR1 | R10 28 kΩ from `+5V_BRL`, R11 15 kΩ to `GND` |
| 3 CP2 | R12 34 kΩ from `VBUS`, R13 15 kΩ to `GND` |
| 1, 8 OUT | `VIN`, C17 10 µF 25 V 0805 (C15850) to `GND` |
| 10 ILM | R14 61.9 kΩ to `GND` (≈2 A limit) |
| 11 SS | C18 0.1 µF (C1525) to `GND` |
| 4 OV2, 5 OV1, 9 ST, 12 GND | `GND` |

The barrel side wins while `+5V_BRL` is above ≈0.88 × the USB voltage, so with
5.08 V against USB's 5 V the adapter always takes the load. The mux also stops
either source back-feeding the other.

**J2, to the motherboard:** as on the USB-C board, but pins 1 and 6 are `VIN`
(the mux output), not `VBUS`.

**LEDs:** as on the USB-C board, but R3/R4 go to `VIN`, so they light from
either source.

**Mounting:** two M2.5 holes.

## Parts (in addition to the USB-C board's, with J2's pins 1/6 moved to VIN)

| Part | Qty | LCSC |
|---|---|---|
| PJ-102AH barrel jack | 1 | C3096093 |
| AO4407A P-FET | 1 | C2841482 |
| BZT52C12 | 1 | C173429 |
| SMBJ20A | 1 | C364296 |
| 47 µF 35 V electrolytic 6.3×5.4 | 1 | C72522 |
| TPS54308 | 1 | C168839 |
| TPS54302 | 1 | C311983 |
| MWSA0603S-100MT 10 µH | 2 | C132141 |
| 10 µF 50 V 1206 | 4 | C513774 |
| 0.1 µF 50 V 0402 | 4 | C307331 |
| 22 µF 25 V 0805 | 4 | C45783 |
| 100 kΩ 0402 | 3 | C25741 |
| 13.3 kΩ 0402 | 2 | C274352 |
| TLC-MSMD300 3 A PTC | 1 | C262035 |
| B2P-VH | 1 | C160315 |
| TPS2121 | 1 | C485916 |
| 1 µF 25 V 0402 | 2 | C52923 |
| 10 µF 25 V 0805 | 1 | C15850 |
| 28 kΩ 0402 | 1 | C25772 |
| 15 kΩ 0402 | 2 | C25756 |
| 34 kΩ 0402 | 1 | C48939 |
| 61.9 kΩ 0402 | 1 | C25912 |
| 0.1 µF 16 V 0402 | 1 | C1525 |

## Layout

* J1, J3 and the LEDs on the panel edge; J4 (aux) on an inside edge for the LED
  strip cable.
* Each buck: input caps at VIN/GND, SW node and L short, FB divider close to FB
  and away from SW; plenty of GND vias. Keep U3/U4 away from the USB pair.
* `VBARREL`, the buck outputs, `VIN` and `AUX_OUT` are power tracks: 1 mm+ or
  pours. The aux path carries 3 A.
