# Rodeo satellite: rotary switch ×1

One rotary selector switch, up to 8 positions, with a status LED, read by a CH32V003
on the satellite chain: board kind 4, the contract's single `SWITCH` slot. For a
mode or range selector whose position must be absolute and survive a power cycle,
which an encoder's can't. It's built like the [single pot](../rodeo-pot-1/README.md):
the same outline, MCU, connectors and pin map, with the switch read on the pot's ADC
pin through a resistor ladder. Wire contract:
[`RODEO_SATELLITE_CHAIN_CONTRACT.md`](../rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md).

Specified 2026-10-26; schematic and layout to come (Derek). **The switch itself
isn't settled** (see [Switch](#switch-sw1)); everything else is written to work with
any single-pole switch up to 8 positions.

## Decisions

* **One ADC pin, not one GPIO per position.** The switch's positions tap a resistor
  ladder and its common goes to the single pot's wiper pin (PA2). The board is then
  the single pot with a different part on top, and the ladder doesn't care how many
  positions the switch has: a 4-position switch just never reaches taps 5–8. One GPIO
  per position would need 8 pins and a different map from every other single board.
* **Taps at 1/9 to 8/9 of 3V3, never at the rails.** Nine equal resistors make eight
  taps 114 counts apart. A 1 MΩ pull-down on the common means a reading near 0 V is
  "between positions", never position 1. The ladder runs from the same 3V3 as the
  ADC's reference, so the reading is ratiometric: a sagging supply at the end of a
  long chain doesn't move it.
* **The common's 100 nF holds the last position through the switch's gap.**
  Break-before-make contacts leave the common open for a few milliseconds per step.
  Through 1 MΩ the cap's voltage barely moves in that time (τ = 0.1 s), so the
  reading goes from one tap straight to the next. Firmware also waits for a stable
  reading before reporting a new position.
* **Same MCU, CH32V003F4P6, and the single pot's pin map** (`PRESENT` PC3, LED PC6, ID
  on PD3 fed from PC2), for the reasons in its spec.
* **Hung on the switch's bushing and nut plus an M2 standoff at H1,** as the single
  pot is. A rotary switch's detents take more torque than a pot, so the standoff
  matters more here: it stops the board turning when the knob is forced past the
  end stop.

## Switch SW1

**Recommended: Taiwan Alpha SR1712F-0108-…-N9** (1 pole, 8 positions, 17 mm body,
M9 × 0.75 bushing), or the same series with fewer positions. It's the
compact Alpha series common in modular synths. Its M9 × 0.75 thread is the single
pot's, so the panel hole (Ø9.5 mm) and nut are the same, and its 17 mm body fits the
25 mm board. **JLC doesn't stock it** (0 on 2026-10-26), so it would be bought
elsewhere and either consigned to JLC or hand-soldered after SMT; it's the only
through-hole part, on the top side.

Considered and set aside:

| Part | JLC stock | Why not |
|---|---|---|
| Alps SRRM1C6200 (C278333), 1P12T | 102 | 24.9 × 31 mm, too big for the board; rotates endlessly with no end stop; shorting contacts; Alps lists it as not recommended for new designs |
| Alps SRBV181004 (C390927), 1P8T | 40 | Low-profile with a plastic flat actuator and no bushing, so nothing to hang the board from; little stock |

To confirm against the SR1712F datasheet before the schematic: the pin circle and
footprint, the mounting face's height above the PCB, the available shaft lengths
(the knob should sit level with the pots': shaft end 25 mm above the PCB, as on the
single pot), whether the bushing has an anti-rotation tab that needs a panel hole,
and the shaft's flat. A footprint and 3D model will be needed; neither is in
KiCad's libraries.

## Board

* **25 × 25 mm, 2 mm corners,** the single pot's outline. Coordinates are from the
  top-left corner seen from the front (panel side), y down.
  * Switch shaft at (12.5, 12.5). Panel hole Ø9.5 mm for the M9 bushing.
  * LED D1 at (12.5, 3.0), as on the single pot, if the switch's pins leave room.
    The 17 mm body's edge sits 4 mm from the shaft's top, so D1 may need moving.
  * M2 hole H1 at (22.5, 2.5), as on the single pot, again if the body allows.
  * Chain connectors J1 (IN) at (3, 20) and J2 (OUT) at (22, 20); programming pads
    J3 down the right edge.
* **Top side (faces the panel): the switch only**, through-hole. **Bottom: every SMD
  part**, the ladder included, so JLC's SMT is single-sided.
* **2 layers,** as on the single pot.
* **Silkscreen (back):** `RODEO ROTARY SWITCH / Kind 4 · Rev B1 / 2026-10`, the
  licence and source, `© 2026 DOT RODEO LLC`, `CHAIN IN` / `CHAIN OUT` with pin-1
  marks, `S V G N` at J3. On the back, small, beside each switch terminal: its
  position number, `1`–`8`, so a builder can check the ladder order against the
  switch.

## The ladder

```
+3V3 ─ R ─ T8 ─ R ─ T7 ─ R ─ T6 ─ … ─ T2 ─ R ─ T1 ─ R ─ GND      (9 × R)

switch position n ── Tn          switch common ── SW_POS ── PA2
                                 SW_POS: 1 MΩ to GND, 100 nF to GND
```

* **R = 4.7 kΩ (1 %), nine of them:** 42.3 kΩ in all, 78 µA. Tap n sits at
  n/9 × 3V3: 114, 227, 341, 455, 568, 682, 796 and 909 counts for positions 1–8.
* The worst-case source impedance at a tap is under 11 kΩ. The 1 MΩ load pulls a tap
  down by about 1 % at most, 6 counts, against 57 counts to the next decision
  point.
* **Decoding:** position = round(9 × reading / 1023), valid when it's 1–8 and the
  reading is within 0.3 of a step of the tap (the ID resistor's rule). A reading
  near 0 V, or between taps, keeps the last position. A new position is reported
  after it has read the same for 10 ms.
* **`SWITCH` slot:** `D0` = position − 1, so 0–7, as the contract's position index.
  Until the first valid reading after power-up, the slot is `EMPTY`.

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
| Alpha SR1712F-0108 (to confirm) | SW1 | 1 | not at JLC: consign or hand-solder |
| CH32V003F4P6 | U1 | 1 | C5187096 |
| BM05B-SRSS-TB | J1, J2 | 2 | C160391 |
| LTST-C230KRKT | D1 | 1 | C125107 |
| 4.7 kΩ 0603 1 % | ladder | 9 | to source |
| 1 MΩ 0603 | `SW_POS` pull-down | 1 | to source |
| 100 nF 0402 | `SW_POS`, VDD | 2 | C1525 |
| 10 kΩ 0603 1 % | ID top | 1 | C25804 |
| 3.65 kΩ 0603 1 % | R_ID | 1 | to source |
| 33 Ω 0603 | DATA_FWD, DATA_RET | 2 | C23140 |
| 1.5 kΩ 0603 | LED | 1 | C22843 |
| 1 µF 0402 | VDD | 1 | C52923 |
| M2 mounting hole | H1 | 1 | — |

Fill the new values with `tools/kicad/lcsc_fill.py` once the schematic is drawn.

## Open

* **The switch:** confirm the SR1712F (above), or choose another, and decide how it
  gets onto the board (consigned to JLC or hand-soldered). If a JLC-stocked switch
  with a bushing turns up, it's worth the swap.
* **Position count:** 8 is the ladder's limit. A 12-position switch would need 13
  taps, 79 counts apart: still workable, but the switches that size are 24 mm or
  more and don't fit the 25 mm board.
* **Bring-up:** the ID read (kind 4), each position's reading against its tap, the
  10 ms settling while turning briskly, and the LED.
