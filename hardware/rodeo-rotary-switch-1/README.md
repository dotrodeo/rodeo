# Rodeo satellite: rotary switch ×1

One 4-position rotary selector switch with a status LED, read by a CH32V003 on the
satellite chain: board kind 4, the contract's single `SWITCH` slot. For a mode or
range selector whose position must be absolute and survive a power cycle, which an
encoder's can't. It's built like the [single pot](../rodeo-pot-1/README.md): the same
mounting, MCU, connectors and pin map, with the switch read on the pot's ADC pin
through a resistor ladder that has room for 8 positions. The switch's footprint is
bigger than the pot's, so the board is 25 × 37.5 mm, half a grid slot taller: two
of them fill three 25 mm slots (see [Board](#board)). Wire contract:
[`RODEO_SATELLITE_CHAIN_CONTRACT.md`](../rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md).

Specified 2026-10-26. Schematic and layout: a first pass by Claude, 2026-10-26, from
Derek's single pot files (see [Layout](#layout)). For Derek's review.

## Decisions

* **Alps SRBV141404, 4 positions** (see [Switch](#switch-sw1-alps-srbv141404-c209777)).
  It's the one rotary switch JLC stocks in quantity that has a bushing to hang the
  board from and end stops. Its M9 bushing is the single pot's, so the panel hole and
  nut are the same.
* **One ADC pin, not one GPIO per position.** The switch's positions tap a resistor
  ladder, and its common goes to the single pot's wiper pin (PA2). The board is then
  the single pot with a different part on top.
* **The ladder has 8 taps though the switch has 4.** A switch with fewer positions
  just never reaches the upper taps, so the same ladder and the same kind-4 decoder
  would serve an 8-position switch (the SRBV181004, wired the same) with no
  firmware change. The four extra 0603 resistors cost almost nothing.
* **Taps at 1/9 to 8/9 of 3V3, never at the rails.** Nine equal resistors make eight
  taps 114 counts apart. A 1 MΩ pull-down on the common means a reading near 0 V is
  "between positions", never position 1. The ladder runs from the same 3V3 as the
  ADC's reference, so the reading is ratiometric: a sagging supply at the end of a
  long chain doesn't move it.
* **The common's 100 nF holds the last position through the switch's gap.** The
  SRBV's contacts are non-shorting, so the common is open for a moment at each step.
  Through 1 MΩ the cap's voltage barely moves in that time (τ = 0.1 s), so the
  reading goes from one tap straight to the next. Firmware also waits for a stable
  reading before reporting a new position.
* **Same MCU, CH32V003F4P6, and the single pot's pin map** (`PRESENT` PC3, LED PC6, ID
  on PD3 fed from PC2), for the reasons in its spec.
* **Hung on the switch's M9 bushing and nut, plus an M2 standoff at H1,** as the
  single pot is. The standoff stops the board turning when the knob is forced
  against an end stop.

## Switch SW1: Alps SRBV141404 (C209777)

1 pole, 4 positions, 30° steps with end stops, non-shorting. M9 threaded bushing,
7 mm long; Ø6 mm shaft with a 4.5 mm flat, 15 mm from the mounting face to the tip
(Derek, from Alps' drawing). Body 16.2 × 18.5 × 7.5 mm. Turning torque 30 ± 15 mN·m,
lighter than a pot's drag, so it will feel light. Rated 0.3 A at 16 V; life 10,000
steps. 434 in stock at JLC on 2026-10-26, so it is assembled with the board, like
the pots.

* **The 8-position sibling, SRBV181004 (C390927),** has the same body and footprint,
  and the board wires all eight positions, so it takes either as a BOM choice. Only 40 in stock at JLC
  on 2026-10-26.
* **Footprint and 3D model: in `lib/`,** from EasyEDA (C209777) through
  easyeda2kicad. `rodeo_rotary_switch:SW_Alps_SRBV` has 10 terminals in two rows of
  five, 2.54 mm pitch, the rows 18.3 mm apart (y = ±9.15 from the shaft), and two
  Ø2 mm mounting lugs 16 mm apart (x = ±8, y = −1.1). EasyEDA's SRBV181004 footprint
  is identical, so the 4- and 8-position parts are a BOM swap. Alps' own STEP model
  (made in Creo) sits on it correctly: terminals in their holes, lugs in theirs.
* **Symbol: `lib/rodeo_rotary_switch.kicad_sym`,** EasyEDA's, with its footprint and
  LCSC fields set, and its pins named from Alps' standard circuit diagram (1 pole):
  * **Terminals 1 and 10 are the common, `C`,** one in each row. Wire both to
    `SW_POS`.
  * **Terminals 2–9 are positions `P1`–`P8`,** in that order around the rotor. On the
    SRBV141404, 6–9 are dummy terminals with no contact behind them; positions 1–4
    are terminals 2–5.
  * **Pads 11 and 12 are the lugs, `MP`:** tie them to `GND`.
  * The diagram's terminal layout (6–10 along the top, 5–1 along the bottom, left to
    right) matches the footprint seen from the top, which faces the panel, so it's
    drawn from the knob side. Read that way, 2 → 5 runs clockwise, so position 1 is
    the counter-clockwise end. Check the direction at bring-up.
* **Considered and set aside:** the Alps SRRM1C6200 (C278333), JLC's only
  12-position switch: 24.9 × 31 mm, too big for the board, it turns endlessly,
  shorting contacts, and Alps lists it as not recommended for new designs. The
  Taiwan Alpha SR1712F would allow up to 8 positions with an M9 bushing, but JLC
  doesn't stock it.

## Board

* **Outline 25 × 37.5 mm, 2 mm corners.** 25 mm wide like the other single boards,
  and 1.5 grid slots tall, so two rotary switches stack into three 25 mm slots.
  Coordinates are from the board's top-left corner as seen from the front (panel
  side), y down; the LED end is the top, as on the single pot.
  * **Switch SW1 at the board's centre, (12.5, 18.75), in the footprint's own
    orientation:** terminal rows across the board at y = 9.6 and 27.9 (x = 7.4–17.6),
    lugs at (4.5, 17.65) and (20.5, 17.65). Alps' model puts the shaft on the
    footprint's origin. Two boards stacked in 75 mm put their shafts 37.5 mm apart,
    symmetric in the three slots. Panel hole Ø9.5 mm for the M9 bushing, as for the
    pots.
  * **LED D1 at (12.5, 3.0) with R1 beside it, H1 at (22.5, 2.5) and J3 at x = 23,
    y = 6–13.6:** the single pot's positions.
  * **Chain connectors J1 (IN) at (3, 32.5) and J2 (OUT) at (22, 32.5):** 5 mm above
    the bottom edge, as on the single pot, either side of the bottom terminal row.
  * **U1 at the centre of the back, (12.5, 18.75),** between the terminal rows, under
    the switch body. C2 and C3 (VDD) to its left, C1 and R15 (`SW_POS`) to its right.
  * **The ladder along the terminal rows:** R6–R9 in two staggered rows under
    terminals 6–10, R11–R14 above terminals 1–5, each between the two terminals it
    joins (the ends, R6 from `+3V3` and R14 to `GND`, beside terminals 9 and 2), and
    R10 (T5–T4) at the left edge, joining the two rows.
  * **The ID divider R3, R5** top left (front view), above the left lug; **R2, R4**
    (33 Ω) on the right, above J2.
* **The LED is 15.75 mm from the shaft,** so a knob up to about Ø27 mm clears its
  hole (the single pot's limit is Ø14.8 mm).
* **Top side (faces the panel): the switch only**, through-hole. **Bottom: every SMD
  part**, the ladder included, so JLC's SMT is single-sided.
* **Knobs level with the pots' for free.** Both hang from the panel's underside by
  the mounting face, and both shafts rise 15 mm from it, so a rotary switch's knob
  sits at the pots' height. The shaft is Ø6 mm with a 4.5 mm flat, like the pots',
  so the same knobs should fit; check one on the first boards.
* **Standoff about 7.5 mm, not the single pot's 10 mm:** the mounting face is the top
  of the 7.5 mm body, sitting on the board. M2 standoffs are commonly 6 or 8 mm, so
  measure the seated height on the first board and pick the length (or a washer)
  from that.
* **The 7 mm bushing** takes the panel and the nut with room to spare.
* **2 layers,** as on the single pot.
* **Silkscreen.** Back: `RODEO / ROTARY / SWITCH` top right (seen from the back),
  `Kind 4 / Rev B1 / 2026-10` between the connectors, `CHAIN IN` / `CHAIN OUT` with
  pin-1 marks, `S V G N` at J3, and position numbers `1`–`4` under terminals 2–5.
  Front: the licence, source and `© 2026 DOT RODEO LLC` below the switch, hidden
  by the panel once fitted (there's no room left on the back).

## The ladder

```
+3V3 ─ R ─ T8 ─ R ─ T7 ─ R ─ T6 ─ … ─ T2 ─ R ─ T1 ─ R ─ GND      (9 × R)

Tn ── terminal n+1 (position n)  common (terminals 1, 10) ── SW_POS ── PA2
                                 SW_POS: 1 MΩ to GND, 100 nF to GND
```

* **R = 4.7 kΩ (1 %), nine of them:** 42.3 kΩ in all, 78 µA. Tap n sits at
  n/9 × 3V3: 114, 227, 341, 455, 568, 682, 796 and 909 counts for positions 1–8.
* **Tap n goes to terminal n + 1,** all eight. The SRBV141404 uses taps 1–4 on
  terminals 2–5; taps 5–8 land on its dummy terminals 6–9, which connect to nothing
  inside, so the same board takes the 8-position SRBV181004 as a BOM swap.
* The worst-case source impedance at a tap is under 11 kΩ. The 1 MΩ load pulls a tap
  down by about 1 % at most, 6 counts, against 57 counts to the next decision
  point.
* **Decoding:** position = round(9 × reading / 1023), valid when it's 1–8 and the
  reading is within 0.3 of a step of the tap (the ID resistor's rule). A reading
  near 0 V, or between taps, keeps the last position. A new position is reported
  after it has read the same for 10 ms.
* **`SWITCH` slot:** `D0` = position − 1, so 0–3 here (0–7 with an 8-position
  switch), as the contract's position index. Until the first valid reading after
  power-up, the slot is `EMPTY`.

## Layout

`rodeo-rotary-switch-1.kicad_sch` and `.kicad_pcb` start from the single pot's,
changed by script rather than by hand:

* **Schematic:** RV1 out, SW1 (`rodeo_rotary_switch:SRBV141404`) in, with terminal 1
  on the old wiper net (renamed `POT_1` → `SW_POS`) and terminal 10 on a `SW_POS`
  label; the lugs to `GND`. The ladder R6–R14 is a column of nine 4.7 kΩ with labels
  `T1`–`T8` at its joints and on terminals 2–9; R15 1 MΩ from `SW_POS` to `GND`; R5
  715 Ω → 3.65 kΩ. ERC: no errors or warnings.
* **PCB:** the outline, zones and panel guides stretched to 25 × 37.5 mm; RV1 out;
  SW1, R6–R15 in; every part but D1, R1, J3 and H1 placed afresh (above). The single
  pot's tracks didn't survive the move, so the whole board was routed by a grid
  router: 0.1 mm grid, 45° steps with a cost per turn, 0.2 mm signal tracks, 0.3 mm
  `+3V3` and `GND`, 0.2 mm clearance, 0.6/0.3 mm vias; signals on B.Cu where they can,
  `GND` on F.Cu under its pour, U1's decoupling routed first.
* **DRC, with schematic parity:** two edge-clearance errors, both D1's own pads at its
  lens hole, as on the single pot; no unconnected items, no footprint or parity
  errors.
* **Worth a look in KiCad:**
  * The routing is correct, not pretty: 38 vias (11 of them `GND` to the front
    pour), and some runs go the long way round. Re-route as you see fit.
  * `+3V3` runs partly on F.Cu, cutting the front `GND` pour, and so does `SW_POS`.
  * F.Cu tracks pass under the switch body, under solder mask. Check Alps' drawing for
    anything metal on the body's underside.
  * J1 pin 2 and R6 pin 1 connect solidly to the `+3V3` pour (a local override): with
    the tracks beside them, a thermal relief got only one spoke.
  * References R2, R5, R9, R12, R14, R15 and C1 are on B.Fab; there was no room on
    the back silk clear of pads.

## MCU: CH32V003F4P6 (C5187096), TSSOP-20

The single pot's map, with `SW_POS` where its wiper goes.

| Pin | Name | Connection |
|---|---|---|
| 9 | VDD | `+3V3`; 1 µF and 100 nF to `GND` at the pin |
| 7 | VSS | `GND` |
| 3 | PD6 | `SAT_RX`: J1 IN pin 3 |
| 11 | PC1 | `SAT_RX` (same net) |
| 2 | PD5 | 33 Ω → J2 OUT pin 3 (`DATA_FWD` out) |
| 10 | PC0 | 33 Ω → `DATA_RET` |
| 13 | PC3 | `PRESENT`: J2 OUT pin 5 (internal pull-up) |
| 12 | PC2 | `ID_DRV`: the ID divider's top, driven high only while reading the ID |
| 20 | PD3 (A4) | `ID`: 10 kΩ from PC2, 3.65 kΩ (R_ID, kind 4) to `GND` |
| 6 | PA2 (A0) | `SW_POS`: the switch common (ladder above) |
| 16 | PC6 | 1.5 kΩ → D1 anode; cathode to `GND` |
| 18 | PD1 | `SWIO`: programming pad |
| 4 | PD7 | `NRST`: programming pad |
| others | | no connect |

## Connectors J1 (IN), J2 (OUT): JST SH 5-pin vertical, BM05B-SRSS-TB (C160391)

The satellites' pinout:

1. `GND`
2. `+3V3`, straight through
3. `DATA_FWD`: `SAT_RX` on J1; on J2, the 33 Ω from PD5
4. `DATA_RET`: straight through, tapped by the 33 Ω from PC0
5. `PRESENT`: `GND` on J1, PC3 on J2

Mounting pads unconnected.

## Board-kind ID

**Kind 4, `rotary switch`, R_ID = 3.65 kΩ** (contract §1.6): 15 × 3.65 / 13.65 = 4.01,
or 274 counts. One slot: input `SWITCH` (position index), output `LED` for D1.

## Status LED D1, programming

* **D1:** LTST-C230KRKT (C125107), reverse-mount under a lens hole, 1.5 kΩ from PC6:
  ≈0.8 mA. Driven from the `LED` output slot, so an instrument can light it in a
  given position, or blink it while a mode is armed.
* **J3:** `SWIO`, `+3V3`, `GND`, `NRST` pads on a 2.54 mm row, not fitted, DNP.

## Power

| Load | Current |
|---|---|
| MCU | ≈2–3 mA |
| LED | 0.8 mA |
| Ladder (always on) | 0.08 mA |
| ID divider (only during the boot read) | — |
| **Total** | **≈4 mA**, inside the contract's 6 mA per satellite |

## Firmware

To do in `rodeo-satellite`, when asked:

* **`src/rotary.*`:** the decoder above (taps, the 0.3-step window, 10 ms
  stability, hold through gaps), host-tested like `ladder.*`.
* **`SAT_BOARD_SWITCH_1` in `src/boards.h`:** the single pot's map with `SW_POS` on
  PA2 as an ADC channel.
* **`firmware/satellite.c`:** kind 4 reads PA2 each millisecond with the pots' ADC
  path and reports `sat_switch(position)`. `sat_frame.h` already gives kind 4 one
  slot, reporting `SWITCH`.
* **Tests:** every tap decodes to its position at ±1 % resistor error; 0 V, and the
  midpoints between taps, hold the last position; a step through the gap reports
  the new position once, after 10 ms.

## Parts

| Part | Refs | Qty | LCSC |
|---|---|---|---|
| Alps SRBV141404 | SW1 | 1 | C209777 (434 at JLC) |
| CH32V003F4P6 | U1 | 1 | C5187096 |
| BM05B-SRSS-TB | J1, J2 | 2 | C160391 |
| LTST-C230KRKT | D1 | 1 | C125107 |
| 4.7 kΩ 0603 1 % | R6–R14 (ladder) | 9 | C23162 (basic) |
| 1 MΩ 0603 1 % | R15 (`SW_POS` pull-down) | 1 | C22935 (basic) |
| 100 nF 0402 | C1 (`SW_POS`), C3 (VDD) | 2 | C1525 |
| 10 kΩ 0603 1 % | R3 (ID top) | 1 | C25804 |
| 3.65 kΩ 0603 1 % | R5 (R_ID) | 1 | C2930089 (15,327) |
| 33 Ω 0603 | R4 (DATA_FWD), R2 (DATA_RET) | 2 | C23140 |
| 1.5 kΩ 0603 | R1 (LED) | 1 | C22843 |
| 1 µF 0402 | C2 (VDD) | 1 | C52923 |
| M2 mounting hole | H1 | 1 | — |

Stock checked at JLC on 2026-10-26. `fp-lib-table` and `sym-lib-table` are set up
for the project; `lib/` also holds the singles' LED footprint, copied from the
single pot.

## Open

* **On the first boards:** the switch's seated height, for the standoff length, and
  a pot knob on its shaft.
* **Derek's review** of the schematic and layout: see [Layout](#layout). JLC
  outputs, the panel and renders come after.
* **Bring-up:** the ID read (kind 4), each position's reading against its tap, the
  10 ms settling while turning briskly, and the LED.
