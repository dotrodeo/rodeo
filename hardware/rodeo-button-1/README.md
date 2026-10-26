# Rodeo satellite: button ×1

One momentary push button with a status LED, read by a CH32V003 on the satellite
chain: board kind 2, the contract's single `BUTTON` slot. For a button that doesn't
sit in a row of four. It's one cell of the [button bank](../rodeo-button-bank-4/README.md)
built the way the [single pot](../rodeo-pot-1/README.md) and
[single encoder](../rodeo-encoder-1/README.md) are, so the three single boards share
an outline and a panel drawing. Wire contract:
[`RODEO_SATELLITE_CHAIN_CONTRACT.md`](../rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md).

Specified 2026-10-26; schematic and layout to come (Derek).

## Decisions

* **Same switch as the bank: Omron B3F-1052** on the common 6 × 6 mm through-hole
  footprint, so the switch height is a BOM choice (see [Switch](#switch-sw1-omron-b3f-1052-c87037)).
* **A plain red status LED, not the bank's RGB.** The contract gives kind 2 an `LED`
  output, and the single boards all use the same reverse-mount red LED under a lens
  hole. That keeps every SMD part on the back, so JLC's SMT is single-sided and the
  board stays cheap. It runs from the chain's 3V3 at 0.8 mA, with no aux supply and
  no current cap. The bank needed both sides assembled because its WS2812s face the
  panel. An RGB single button is the [open question](#open) below.
* **Same MCU, CH32V003F4P6,** for the reasons in the single pot's spec: the 8-pin
  part can't close the ring, and one firmware image and one jig serve every
  satellite.
* **Pin map: the button bank's slot 1,** with the single pot's LED pin. As on the
  bank, the switch goes to PA1 with the internal pull-up, `PRESENT` to PC3, and the
  ID divider to PD3, fed from 3V3. The LED goes to PC6, where the single pot and the
  pot bank have theirs (on the button bank PC6 is the WS2812 data line). The
  firmware's kind-2 map is then the bank's cut to one slot, plus an LED.
* **Two M2 standoffs, no bushing.** The board hangs from the top panel like the
  bank: a tact switch has nothing to hang from, and every press pushes the board
  away from the panel, so the standoffs carry the load. The single pot and encoder
  get away with one standoff because their bushing takes the force.

## Board

The single pot's outline, so the three single boards can share a panel drawing;
only their mounting holes differ.

* **25 × 25 mm, 2 mm corners.** Coordinates are from the top-left corner seen from
  the front (panel side), y down.
  * Switch centre at (12.5, 12.5): pins at (9.25, 10.25), (15.75, 10.25),
    (9.25, 14.75), (15.75, 14.75).
  * LED D1 at (12.5, 3.0), 9.5 mm above the switch centre, as on the single pot.
  * M2 holes H1 and H2: suggested (2.5, 2.5) and (22.5, 2.5), either side of the LED.
    Placement is Derek's call; the aim is one each side of the switch, as far apart
    as the connectors allow.
  * Chain connectors J1 (IN) at (3, 20) and J2 (OUT) at (22, 20); programming pads
    J3 down the right edge, as on the single pot.
* **Top side (faces the panel): the switch only**, through-hole. **Bottom: every SMD
  part**, including the reverse-mount LED, which shines through its lens hole.
* **2 layers**, as on the single pot: `GND` poured on the panel side, the chain's
  `+3V3` as 0.4 mm tracks for the J1 → J2 pass-through.
* **Cap size.** The Ø3.2 mm LED hole's edge is 7.9 mm from the switch centre. A
  round cap up to Ø14.8 mm, or a square one up to ≈10.5 mm, leaves 0.5 mm of panel
  between them.
* **Silkscreen (back):** `RODEO BUTTON / Kind 2 · Rev B1 / 2026-10`, the licence and
  source, `© 2026 DOT RODEO LLC`, `CHAIN IN` / `CHAIN OUT` with pin-1 marks, and
  `S V G N` at J3.

## Switch SW1: Omron B3F-1052 (C87037)

6 × 6 mm through-hole tact switch, 7.3 mm to the top of a square stem for Omron's
B32 snap-on caps, 1.5 N, 300,000 operations. 5,052 in stock at JLC on 2026-10-19.

* **Footprint: the bank's, the common 6 × 6 mm pattern, pins 6.5 × 4.5 mm.** Alps
  SKHH and C&K PTS645 switches fit it too. For example, Alps SKHHAPA010 (C139809)
  has a 9.5 mm round stem that can come through the panel without a cap.
* One side to `GND`, the other to PA1 with the MCU's internal pull-up. Firmware
  debounces (5 ms) and counts presses, as on the bank.
* **Standoff length, switch height and cap** are chosen together with the panel,
  as for the bank. Pick them once for both boards, so a single button and a bank
  sit at the same depth.

## MCU: CH32V003F4P6 (C5187096), TSSOP-20

USART1 uses mapping 0 normally and mapping 3 when this satellite is last in the
ring. IN `DATA_FWD` goes to both PD6 and PC1, as on every satellite.

| Pin | Name | Connection |
|---|---|---|
| 9 | VDD | `+3V3`; 1 µF and 100 nF to `GND` at the pin |
| 7 | VSS | `GND` |
| 3 | PD6 | `SAT_RX`: J1 IN pin 3 |
| 11 | PC1 | `SAT_RX` (same net) |
| 2 | PD5 | 33 Ω → J2 OUT pin 3 (`DATA_FWD` out) |
| 10 | PC0 | 33 Ω → `DATA_RET` |
| 13 | PC3 | `PRESENT`: J2 OUT pin 5 (internal pull-up) |
| 20 | PD3 (A4) | `ID`: 10 kΩ from `+3V3`, 1.54 kΩ (R_ID, kind 2) to `GND` |
| 5 | PA1 | `SW`: switch to `GND` (internal pull-up) |
| 16 | PC6 | 1.5 kΩ → D1 anode; cathode to `GND` |
| 18 | PD1 | `SWIO`: programming pad |
| 4 | PD7 | `NRST`: programming pad |
| others | | no connect (PC2 stays undriven: the ID divider is fed from 3V3) |

## Connectors J1 (IN), J2 (OUT): JST SH 5-pin vertical, BM05B-SRSS-TB (C160391)

The satellites' pinout:

1. `GND`
2. `+3V3`, straight through
3. `DATA_FWD`: `SAT_RX` on J1; on J2, the 33 Ω from PD5
4. `DATA_RET`: straight through, tapped by the 33 Ω from PC0
5. `PRESENT`: `GND` on J1, PC3 on J2

Mounting pads unconnected.

## Board-kind ID

**Kind 2, `button`, R_ID = 1.54 kΩ** (contract §1.6): 15 × 1.54 / 11.54 = 2.00, or
≈137 counts. One slot: input `BUTTON` (pressed bit, press counter), output `LED` for
D1.

## Status LED D1, programming

* **D1:** LTST-C230KRKT (C125107), reverse-mount through the singles'
  `LED_1206_3216Metric_ReverseMount_Hole1.8x2.4mm`, 1.5 kΩ from PC6: ≈0.8 mA.
* **J3:** `SWIO`, `+3V3`, `GND`, `NRST` pads on a 2.54 mm row, not fitted, DNP.

## Power

| Load | Current |
|---|---|
| MCU | ≈2–3 mA |
| LED | 0.8 mA |
| ID divider (fed from 3V3, always on) | 0.29 mA |
| Switch pull-up, while pressed | ≈0.1 mA |
| **Total** | **≈4 mA**, inside the contract's 6 mA per satellite |

## Firmware

To do in `rodeo-satellite`, when asked:

* **`SAT_BOARD_BUTTON_1` in `src/boards.h`:** `buttons = {PA1}`, `PRESENT` on PC3,
  LED on PC6, ID fed from 3V3 (no feed pin), no aux sense.
* **`firmware/satellite.c`:** for kind 2, the switch takes the button bank's path
  with one button (debounce and press counter from `src/buttons.*`). The LED takes
  the single boards' path instead of the WS2812s. `sat_frame.h` already gives kind 2
  one slot, reporting `BUTTON`.
* **Tests:** kind 2 is found without driving PC2; one button is debounced and
  counted, with no phantom buttons 2–4.

## Parts

| Part | Refs | Qty | LCSC |
|---|---|---|---|
| B3F-1052 | SW1 | 1 | C87037 |
| CH32V003F4P6 | U1 | 1 | C5187096 |
| BM05B-SRSS-TB | J1, J2 | 2 | C160391 |
| LTST-C230KRKT | D1 | 1 | C125107 |
| 10 kΩ 0603 1 % | ID top | 1 | C25804 |
| 1.54 kΩ 0603 1 % | R_ID | 1 | to source |
| 33 Ω 0603 | DATA_FWD, DATA_RET | 2 | C23140 |
| 1.5 kΩ 0603 | LED | 1 | C22843 |
| 100 nF 0402 | VDD | 1 | C1525 |
| 1 µF 0402 | VDD | 1 | C52923 |
| M2 mounting hole | H1, H2 | 2 | — |

The reused parts' stock was checked between 2026-10-19 and 2026-10-26. Fill the
1.54 kΩ with `tools/kicad/lcsc_fill.py` once the schematic is drawn.

## Open

* **RGB instead of red?** A WS2812B-2020 like the bank's would match the bank's
  colours on the same panel. The costs:
  * both sides assembled, since the LED must face the panel;
  * the contract's `LED` output for kind 2 changed to `RGB`;
  * on chain power, a cap of about 3 mA, so it would be dim without the bank's aux
    5 V circuit.

  The draft keeps red; say if matching RGB matters more.
* **Standoff length, switch height and caps:** chosen with the panel, shared with
  the button bank.
* **Bring-up:** the ID read (≈137 counts, kind 2), the switch's debounce and press
  count, and the LED.
