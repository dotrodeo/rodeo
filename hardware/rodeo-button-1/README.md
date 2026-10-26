# Rodeo satellite: button ×1

One momentary push button with a status LED, read by a CH32V003 on the satellite
chain: board kind 2, the contract's single `BUTTON` slot. For a button that doesn't
sit in a row of four. It uses the [button bank](../rodeo-button-bank-4/README.md)'s
switch on the [single pot](../rodeo-pot-1/README.md)'s board: the same outline, MCU,
connectors, LED and pin map, so the single boards share a panel drawing. Wire
contract:
[`RODEO_SATELLITE_CHAIN_CONTRACT.md`](../rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md).

Specified 2026-10-26. Schematic and layout: a first pass by Claude, 2026-10-26,
derived from Derek's single pot files (see [Layout](#layout)). For Derek's review.

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
* **The single pot's pin map,** with the switch where the pot's wiper was: PA2,
  with the internal pull-up and C1 (now 10 nF) on it. `PRESENT` is PC3, the LED PC6,
  and the ID divider is fed from PC2, all as on the single pot. The board is the
  single pot's with a switch in place of the pot, so most of Derek's routing
  carries over. (The first draft used the button bank's pins; the pot's won,
  because the layout follows it.)
* **Two M2 standoffs, no bushing.** The board hangs from the top panel like the
  bank: a tact switch has nothing to hang from, and every press pushes the board
  away from the panel, so the standoffs carry the load. The single pot and encoder
  get away with one standoff because their bushing takes the force.

## Board

The single pot's outline, so the three single boards can share a panel drawing;
only their mounting holes differ.

* **25 × 25 mm, 2 mm corners.** Coordinates are from the top-left corner seen from
  the front (panel side), y down.
  * Switch SW1 centred on (12.5, 12.5), turned 90° so its pins clear the most
    existing routing. Pin 1 (`SW`) at (10.25, 9.25) and (10.25, 15.75); pin 2 (`GND`)
    at (14.75, 9.25) and (14.75, 15.75).
  * LED D1 at (12.5, 3.0), 9.5 mm above the switch centre, as on the single pot.
  * M2 holes: H1 at (22.5, 2.5), as on the single pot, and H2 at (12.5, 21.0),
    below the switch where the pot's pins were. The line between them passes 4 mm
    from the switch centre, so a press levers the board a little about it.
  * Chain connectors J1 (IN) at (3, 20) and J2 (OUT) at (22, 20); programming pads
    J3 down the right edge, as on the single pot.
* **Top side (faces the panel): the switch only**, through-hole. **Bottom: every SMD
  part**, including the reverse-mount LED, which shines through its lens hole.
* **2 layers**, as on the single pot: `GND` poured on the panel side (the switch's
  pin 2 lands in it), `+3V3` poured on the back.
* **Cap size.** The Ø3.2 mm LED hole's edge is 7.9 mm from the switch centre. A
  round cap up to Ø14.8 mm, or a square one up to ≈10.5 mm, leaves 0.5 mm of panel
  between them.
* **Silkscreen.** The switch fills the middle of the back, where the single pot's
  title and licence sat, and H2 takes the copyright's place, so:
  * **Back:** `RODEO / BUTTON` above the switch's right-hand pins, `Kind 2 / Rev B1 /
    2026-10` between them, beside J3's `S V G N`; `CHAIN IN` / `CHAIN OUT` with
    pin-1 marks, as on the single pot.
  * **Front, around H2:** `rodeooh.rodeo/source` above it, `CERN-OHL-W-2.0` to its
    left, `© 2026 DOT RODEO LLC` to its right. Hidden by the panel once fitted.

## Layout

`rodeo-button-1.kicad_sch` and `.kicad_pcb` are the single pot's with these changes,
made by script rather than by hand:

* **Schematic:** RV1 out, SW1 (`Switch:SW_Push`, the bank's symbol) in, with pin 1 on
  the old wiper net (renamed `POT_1` → `SW`) and pin 2 on the pot's `GND`. C1 100 nF
  → 10 nF, R5 715 Ω → 1.54 kΩ, H2 added. ERC: no errors or warnings.
* **PCB:** RV1 out, SW1 and H2 in, values and LCSC numbers updated. The switch's pins
  landed on tracks of four nets (`PRESENT`, `NRST`, `DATA_RET` and PC0's 33 Ω), so
  those were ripped up and re-routed, along with `SW`'s run to the old wiper pin,
  by a grid router: 0.2 mm tracks, 0.2 mm clearance, B.Cu preferred, 0.6/0.3 mm vias.
  Everything else is Derek's routing, untouched.
* **DRC, with schematic parity:** the same three edge-clearance errors as the single
  pot, all at D1's lens hole (expected, as its README says); no unconnected items,
  no footprint or parity errors.
* **Worth a look in KiCad:** the re-routed tracks are correct but not pretty. The
  router works on a 0.1 mm grid in 45° steps and doesn't tidy up after itself, so a
  few runs take long ways round and add vias (`PRESENT` and PC0's have two each).
  Tidy them as you see fit.

## Switch SW1: Omron B3F-1052 (C87037)

6 × 6 mm through-hole tact switch, 7.3 mm to the top of a square stem for Omron's
B32 snap-on caps, 1.5 N, 300,000 operations. 4,722 in stock at JLC on 2026-10-26.

* **Footprint: KiCad's `Button_Switch_THT:SW_PUSH_6mm` with its 3D model,** the
  bank's. Alps SKHH and C&K PTS645 switches fit it too. For example, Alps SKHHAPA010
  (C139809) has a 9.5 mm round stem that can come through the panel without a cap.
* Pin 1 to `SW` (PA2, internal pull-up, C1 10 nF to `GND`), pin 2 to `GND`. Both pin-1
  legs are tracked together, since KiCad doesn't count the switch's internal link.
  Firmware debounces (5 ms) and counts presses, as on the bank. With the ≈40 kΩ
  pull-up, C1 slows a release to ≈0.4 ms, well inside the debounce.
* **Standoff length, switch height and cap** are chosen together with the panel,
  as for the bank. Pick them once for both boards, so a single button and a bank
  sit at the same depth.

## MCU: CH32V003F4P6 (C5187096), TSSOP-20

USART1 uses mapping 0 normally and mapping 3 when this satellite is last in the
ring. IN `DATA_FWD` goes to both PD6 and PC1, as on every satellite.

| Pin | Name | Connection |
|---|---|---|
| 9 | VDD | `+3V3`; C2 1 µF and C3 100 nF to `GND` at the pin |
| 7 | VSS | `GND` |
| 3 | PD6 | `SAT_RX`: J1 IN pin 3 |
| 11 | PC1 | `SAT_RX` (same net) |
| 2 | PD5 | R4 33 Ω → J2 OUT pin 3 (`DATA_FWD` out) |
| 10 | PC0 | R2 33 Ω → `DATA_RET` |
| 13 | PC3 | `PRESENT`: J2 OUT pin 5 (internal pull-up) |
| 12 | PC2 | `ID_DRV`: R3's top, driven high only while reading the ID |
| 20 | PD3 (A4) | `ID`: R3 10 kΩ from PC2, R5 1.54 kΩ (R_ID, kind 2) to `GND` |
| 6 | PA2 | `SW`: SW1 pin 1, C1 10 nF to `GND` (internal pull-up) |
| 16 | PC6 | R1 1.5 kΩ → D1 anode; cathode to `GND` |
| 18 | PD1 | `SWIO`: programming pad |
| 4 | PD7 | `NRST`: programming pad |
| others | | no connect |

## Connectors J1 (IN), J2 (OUT): JST SH 5-pin vertical, BM05B-SRSS-TB (C160391)

The satellites' pinout:

1. `GND`
2. `+3V3`, straight through
3. `DATA_FWD`: `SAT_RX` on J1; on J2, R4 from PD5
4. `DATA_RET`: straight through, tapped by R2 from PC0
5. `PRESENT`: `GND` on J1, PC3 on J2

Mounting pads unconnected.

## Board-kind ID

**Kind 2, `button`, R_ID = 1.54 kΩ** (contract §1.6): 15 × 1.54 / 11.54 = 2.00, or
≈137 counts. One slot: input `BUTTON` (pressed bit, press counter), output `LED` for
D1.

## Status LED D1, programming

* **D1:** LTST-C230KRKT (C125107), reverse-mount through the singles'
  `LED_1206_3216Metric_ReverseMount_Hole1.8x2.4mm` (in `lib/`, copied from the single
  pot with its model), R1 1.5 kΩ from PC6: ≈0.8 mA.
* **J3:** `SWIO`, `+3V3`, `GND`, `NRST` pads on a 2.54 mm row, not fitted, DNP.

## Power

| Load | Current |
|---|---|
| MCU | ≈2–3 mA |
| LED | 0.8 mA |
| Switch pull-up, while pressed | ≈0.1 mA |
| ID divider (only during the boot read) | — |
| **Total** | **≈4 mA**, inside the contract's 6 mA per satellite |

## Firmware

To do in `rodeo-satellite`, when asked:

* **`SAT_BOARD_BUTTON_1` in `src/boards.h`:** `buttons = {PA2}`, `PRESENT` on PC3,
  LED on PC6, ID fed from PC2: the single pot's map with a button where its pot is.
* **`firmware/satellite.c`:** for kind 2, the switch takes the button bank's path
  with one button (debounce and press counter from `src/buttons.*`). The LED takes
  the single boards' path instead of the WS2812s. `sat_frame.h` already gives kind 2
  one slot, reporting `BUTTON`.
* **Tests:** kind 2 is found with PC2 driving the divider; one button is debounced
  and counted, with no phantom buttons 2–4.

## Parts

| Part | Refs | Qty | LCSC |
|---|---|---|---|
| B3F-1052 | SW1 | 1 | C87037 |
| CH32V003F4P6 | U1 | 1 | C5187096 |
| BM05B-SRSS-TB | J1, J2 | 2 | C160391 |
| LTST-C230KRKT | D1 | 1 | C125107 |
| 1.54 kΩ 0603 1 % | R5 | 1 | C2933149 |
| 10 kΩ 0603 1 % | R3 | 1 | C25804 |
| 33 Ω 0603 | R2, R4 | 2 | C23140 |
| 1.5 kΩ 0603 | R1 | 1 | C22843 |
| 10 nF 0402 | C1 | 1 | C15195 |
| 100 nF 0402 | C3 | 1 | C1525 |
| 1 µF 0402 | C2 | 1 | C52923 |
| M2 mounting hole | H1, H2 | 2 | — |

All in stock at JLC on 2026-10-26; the 1.54 kΩ has 15,285. J3 (programming pads) is
DNP and off the BOM.

## Open

* **Derek's review of the layout,** above all the re-routed tracks.
* **RGB instead of red?** A WS2812B-2020 like the bank's would match the bank's
  colours on the same panel. The costs:
  * both sides assembled, since the LED must face the panel;
  * the contract's `LED` output for kind 2 changed to `RGB`;
  * on chain power, a cap of about 3 mA, so it would be dim without the bank's aux
    5 V circuit.

  The draft keeps red; say if matching RGB matters more.
* **Standoff length, switch height and caps:** chosen with the panel, shared with
  the button bank.
* **Not yet made:** JLC outputs, the panel and renders, once the layout is settled.
* **Bring-up:** the ID read (≈137 counts, kind 2), the switch's debounce and press
  count, and the LED.
