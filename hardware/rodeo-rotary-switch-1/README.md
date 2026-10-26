# Rodeo satellite: rotary switch ×1

One 4-position rotary selector switch with a status LED, read by a CH32V003 on the
satellite chain: board kind 4, the contract's single `SWITCH` slot. For a mode or
range selector whose position must be absolute and survive a power cycle, which an
encoder's can't. It's built like the [single pot](../rodeo-pot-1/README.md): the same
mounting, MCU, connectors and pin map, with the switch read on the pot's ADC pin
through a resistor ladder that has room for 8 positions. The switch's footprint is
bigger than the pot's, so the outline and placement are its own (see [Board](#board)). Wire contract:
[`RODEO_SATELLITE_CHAIN_CONTRACT.md`](../rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md).

Specified 2026-10-26; schematic and layout to come (Derek).

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
  would serve an 8-position switch (the SRBV181004, if its pins match) with no
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

1 pole, 4 positions, 30° steps with end stops, non-shorting. M9 bushing and a
flatted shaft, 15 mm long. Body 16.2 × 18.5 × 7.5 mm. Turning torque 30 ± 15 mN·m,
lighter than a pot's drag, so it will feel light. Rated 0.3 A at 16 V; life 10,000
steps. 434 in stock at JLC on 2026-10-26, so it is assembled with the board, like
the pots.

* **The 8-position sibling, SRBV181004 (C390927),** lists the same body. If its
  footprint matches, the board takes either as a BOM choice. Only 40 in stock at JLC
  on 2026-10-26.
* **Footprint and 3D model: in `lib/`,** from EasyEDA (C209777) through
  easyeda2kicad. `rodeo_rotary_switch:SW_Alps_SRBV` has 10 terminals in two rows of
  five, 2.54 mm pitch, the rows 18.3 mm apart (y = ±9.15 from the shaft), and two
  Ø2 mm mounting lugs 16 mm apart (x = ±8, y = −1.1). EasyEDA's SRBV181004 footprint
  is identical, so the 4- and 8-position parts are a BOM swap. Alps' own STEP model
  (made in Creo) sits on it correctly: terminals in their holes, lugs in theirs.
* **Symbol: `lib/rodeo_rotary_switch.kicad_sym`,** EasyEDA's, with its footprint and
  LCSC fields set. Its pins are numbered 1–10 like the footprint's terminals and say
  nothing about which is the common and which are positions 1–4; Alps' drawing does.
  Name the pins from it before drawing the schematic. The lugs (pads 11, 12) have no
  pins; tie them to `GND` in the footprint or add pins for them.
* **Considered and set aside:** the Alps SRRM1C6200 (C278333), JLC's only
  12-position switch: 24.9 × 31 mm, too big for the board, it turns endlessly,
  shorting contacts, and Alps lists it as not recommended for new designs. The
  Taiwan Alpha SR1712F would allow up to 8 positions with an M9 bushing, but JLC
  doesn't stock it.

## Board

* **Outline to be decided, 2 mm corners;** 25 mm wide like the other single boards.
  Coordinates below are relative to the shaft, seen from the front (panel side),
  y down.
  * Switch shaft centred across the board's width. Panel hole Ø9.5 mm for the M9
    bushing, as for the pots.
  * **The single pot's placement doesn't fit the footprint.** Its terminal rows are
    18.3 mm apart and its lugs 16 mm apart, so whichever way it turns, terminals or
    lugs land where the single pot has D1 (y = 3) and J1/J2 (y = 20). This board
    needs its own placement, and probably a longer outline (say 25 × 32 mm, with the
    connectors below the switch): Derek's call.
  * LED D1 above the shaft, as on the other single boards, wherever the terminal
    rows leave room for its lens hole.
  * M2 hole H1, as on the single pot, clear of the body and lugs.
* **Top side (faces the panel): the switch only**, through-hole. **Bottom: every SMD
  part**, the ladder included, so JLC's SMT is single-sided.
* **Standoff length** follows from the switch's mounting-face height (on Alps'
  drawing), so the knob sits level with the pots'.
* **2 layers,** as on the single pot.
* **Silkscreen (back):** `RODEO ROTARY SWITCH / Kind 4 · Rev B1 / 2026-10`, the
  licence and source, `© 2026 DOT RODEO LLC`, `CHAIN IN` / `CHAIN OUT` with pin-1
  marks, and `S V G N` at J3. Small position numbers, `1`–`4`, beside the switch's
  terminals, so the ladder order can be checked against the switch.

## The ladder

```
+3V3 ─ R ─ T8 ─ R ─ T7 ─ R ─ T6 ─ … ─ T2 ─ R ─ T1 ─ R ─ GND      (9 × R)

switch position n ── Tn          switch common ── SW_POS ── PA2
                                 SW_POS: 1 MΩ to GND, 100 nF to GND
```

* **R = 4.7 kΩ (1 %), nine of them:** 42.3 kΩ in all, 78 µA. Tap n sits at
  n/9 × 3V3: 114, 227, 341, 455, 568, 682, 796 and 909 counts for positions 1–8. The
  SRBV141404 uses taps 1–4; taps 5–8 are only resistor joints.
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
| 4.7 kΩ 0603 1 % | ladder | 9 | C23162 (basic) |
| 1 MΩ 0603 1 % | `SW_POS` pull-down | 1 | C22935 (basic) |
| 100 nF 0402 | `SW_POS`, VDD | 2 | C1525 |
| 10 kΩ 0603 1 % | ID top | 1 | C25804 |
| 3.65 kΩ 0603 1 % | R_ID | 1 | C2930089 (15,327) |
| 33 Ω 0603 | DATA_FWD, DATA_RET | 2 | C23140 |
| 1.5 kΩ 0603 | LED | 1 | C22843 |
| 1 µF 0402 | VDD | 1 | C52923 |
| M2 mounting hole | H1 | 1 | — |

Stock checked at JLC on 2026-10-26. `fp-lib-table` and `sym-lib-table` are set up
for the project; `lib/` also holds the singles' LED footprint, copied from the
single pot.

## Open

* **From Alps' drawing:** which terminal is the common and which are positions
  1–4 (for the symbol), the mounting-face height and shaft length (for the standoff,
  and a knob level with the pots'), and whether the shaft's flat takes the pots'
  knobs.
* **The board's placement and outline:** see [Board](#board).
* **Bring-up:** the ID read (kind 4), each position's reading against its tap, the
  10 ms settling while turning briskly, and the LED.
