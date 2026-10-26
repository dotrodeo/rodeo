# Rodeo satellite: button bank ×4

Four momentary buttons in a row on 12.5 mm centres, each with an addressable RGB LED
beside it, read and driven by one CH32V003 on the satellite chain. The third Rev B
satellite, after the [pot bank](../rodeo-pot-bank-4/README.md) and the
[encoder bank](../rodeo-encoder-bank-4/README.md). Wire contract:
[`RODEO_SATELLITE_CHAIN_CONTRACT.md`](../rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md).

Specified 2026-10-19; schematic drawn 2026-10-19; laid out by Derek and routed 2026-10-22.

## Decisions

* **The buttons aren't lit; an LED sits beside each one.** RGB-illuminated tact
  switches at this size are either out of stock at JLC (E-Switch TL3220, $8 each)
  or thinly documented (HOOYA TC-1196A-RGB). No switch under ~14 mm passes light
  through for a reverse-mount LED. A plain switch plus a separate LED keeps both
  parts cheap and well stocked.
* **WS2812B-2020-V6 LEDs, not RGBW.** The in-stock RGBW parts (SK6812 family) need
  3.7–5.5 V and 0.65 × VDD on their data input, so they'd need the aux 5 V and a level
  shifter. The WS2812B-2020-V6 runs from 3.3–5.3 V and switches at 0.55 × VDD, so a
  3.3 V MCU drives it at either supply.
* **The LEDs run from the chain's 3V3 or from an optional aux 5 V,** switched
  automatically (see [LED supply](#led-supply)). Without the aux cable the buttons
  and LEDs both work, with the LEDs capped in firmware to fit the chain's budget
  and dimmer on blue and green at the end of a long chain. With it, full brightness.

## Board

Built like the [pot bank](../rodeo-pot-bank-4/README.md) and the
[encoder bank](../rodeo-encoder-bank-4/README.md): a 20 mm strip with the controls on
the panel side.

* **Outline 20 × 75 mm, rounded corners.** Coordinates are from the board's top-left
  corner, y down the strip; the MCU end is the top.
  * Switch centres at x = 10 mm, y = 18.75, 31.25, 43.75, 56.25 mm (12.5 mm apart).
  * LED centres at x = 17 mm, level with each switch: 7 mm to its right.
  * M2 mounting holes (`MountingHole_2.2mm_M2`) at (10, 3) and (10, 72).
* **Top side (faces the panel): the four switches and four LEDs,** plus the LEDs'
  decoupling caps and the LED supply parts (Q1, D5, C7, C8) by the aux connectors.
* **Bottom side:** the MCU and its passives, the chain connectors J1 (IN, y = 25) and
  J2 (OUT, y = 50) between the switch pins, the aux connectors J3/J4 at the bottom
  end, and the unfitted programming header J5. **Assembled on both sides**, since
  the LEDs must face the panel.
* **4 layers:** signals on F.Cu and B.Cu; In1 solid `GND`; In2 split, `+3V3` (which
  carries the chain's pass-through from J1 to J2) on the left and `V_LED` along the
  right edge under the LEDs, with `AUX_5V` routed in the gap.
* **The board hangs from the top panel on two M2 standoffs.** Buttons have no
  bushing to hang from, and pressing them pushes the board away from the panel, so
  the standoffs carry the load. Standoff length, switch height and caps are chosen
  together with the panel (see [Open](#open)).
* **Silkscreen:** button numbers `1`–`4` beside the switches and the `RODEO`
  wordmark on the front. On the back: `RODEO BUTTON BANK ×4`,
  `Kind 7 · Rev B1 · 2026-10`, the licence and source, `© 2026 DOT RODEO LLC`,
  `CHAIN IN` / `CHAIN OUT`, `AUX 5V` and `+` at J3 and J4 (they're in parallel, so
  either can be the input), and `S V G N` at J5.

## Switches SW1–SW4: Omron B3F-1052 (C87037)

6 × 6 mm through-hole tact switch, 7.3 mm to the top of a square stem that takes
Omron's B32 snap-on caps, 1.5 N, rated 300,000 operations. 5,052 in stock at JLC on
2026-10-19.

* **Footprint: the common 6 × 6 mm through-hole pattern, pins 6.5 × 4.5 mm.**
  Omron B3F, Alps SKHH and C&K PTS645 all fit it, so the switch height is a BOM
  choice: e.g. Alps SKHHAPA010 (C139809) has a 9.5 mm round stem for a stem
  through the panel without a cap.
* Each switch: one side to `GND`, the other to its MCU pin with the internal
  pull-up on. Firmware debounces. Switch n is button n, numbered from the IN end.

## LEDs D1–D4: Worldsemi WS2812B-2020-V6 (C52917434)

2.0 × 2.0 × 0.84 mm, 24-bit GRB, 800 kbit/s, 12 mA per colour (36 mA per LED at full
white). Supply 3.3–5.3 V; data input high at 0.55 × VDD. 860,680 in stock at JLC on
2026-10-19.

* Chained D1 → D2 → D3 → D4, D1 beside SW1. `LED_DIN` from PC6 through R5 100 Ω.
* 100 nF from `V_LED` to `GND` at each LED (C3–C6), as the datasheet's application
  circuit shows.
* **Light pipes.** Each LED is meant to sit under a 3 mm light pipe pressed into the
  panel (Bivar PLP1 from behind, PLPC3 from the front, or a printed clear rod), or
  to show through a bare Ø3.2 mm panel hole. On the board:
  * nothing taller than the LED within 2.5 mm of an LED centre, so a pipe can sit
    within ≈0.5 mm of it (the 0402 decoupling caps at 2.5 mm are lower than the LED);
  * LED centres exactly as listed above, for the panel drawing;
  * one tented `GND` via per LED sits 1.84 mm from its centre: flat under a pipe,
    visible through a bare hole.
  The pipe length follows from the standoff length (the LED's top is 0.84 mm above
  the board).
* **Button caps can be up to ≈9.8 mm square:** that leaves a 0.5 mm web of panel
  between a cap's hole and the Ø3.2 mm LED hole 7 mm away.

## LED supply

`V_LED` feeds the four LEDs and nothing else.

```
AUX_5V (J3 pin 1) ── D5 B5819W ──┬── V_LED ── D1…D4
                                 │
+3V3 ── Q1 AO3401A (drain→source)┘    gate ← AUX_5V
```

* **Aux present:** D5 feeds `V_LED` at ≈4.7 V. Q1's gate is at 5 V and its source
  at 4.7 V, so it's off, and its body diode (drain at 3.3 V) is reverse biased: the
  chain's 3V3 sees no LED current.
* **Aux absent:** R6 + R7 (below) pull Q1's gate to `GND`, so Q1 turns on (V_GS ≈ −3 V,
  85 mΩ) and the chain's 3V3 feeds `V_LED`. D5 keeps `V_LED` off the aux pins.
* **Q1: AO3401A (C15127)**, P-channel, SOT-23, JLC basic part. **D5: B5819W
  (C8598)**, 1 A Schottky, SOD-123, basic part.
* 10 µF on `V_LED` (C7) and 10 µF at J3 (C8).
* **`AUX_SENSE`:** R6 100 kΩ / R7 100 kΩ from `AUX_5V` to `GND`, midpoint to PC4
  (A2), so firmware knows which supply the LEDs are on and sets the brightness cap.
  2.5 V with aux, 0 V without; 25 µA. The same divider is Q1's gate pull-down, so
  Q1 needs no resistor of its own.
* **Data level:** with aux, the LEDs need 0.55 × 4.7 ≈ 2.6 V on `LED_DIN`; the MCU's
  output high is its own supply, ≥ 2.9 V even at the end of a 30-satellite chain.

## Aux connectors J3 (IN), J4 (OUT): JST PH 2-pin vertical SMD, B2B-PH-SM4-TB (C160352)

| Pin | Signal |
|---|---|
| 1 | `AUX_5V` |
| 2 | `GND` |

`AUX_5V` passes straight from J3 to J4, so one aux feed can serve several LED
satellites in a row. Both on the bottom side. The pin order matches the 5 V aux
output planned for the motherboard's next revision
([`revisions.md`](../docs/revisions.md)). `AUX_5V` runs on 0.4–0.8 mm tracks (up to 144 mA).

## MCU: CH32V003F4P6 (C5187096), TSSOP-20

Ring closing and connectors as on the pot bank (USART1 mapping 0 normally, mapping
3 when last; IN `DATA_FWD` on both PD6 and PC1).

| Pin | Name | Connection |
|---|---|---|
| 9 | VDD | `+3V3`; C2 100 nF and C1 1 µF to `GND` at the pin |
| 7 | VSS | `GND` |
| 3 | PD6 | `SAT_RX`: J1 IN pin 3 |
| 11 | PC1 | `SAT_RX` (same net) |
| 2 | PD5 | R2 33 Ω → J2 OUT pin 3 |
| 10 | PC0 | R1 33 Ω → `DATA_RET` |
| 13 | PC3 | `PRESENT`: J2 OUT pin 5 (internal pull-up) |
| 20 | PD3 (A4) | `ID`: R3 10 kΩ from `+3V3`, R4 8.66 kΩ (R_ID, kind 7) to `GND` |
| 16 | PC6 | `LED_DIN` via R5 100 Ω: SPI1 MOSI, which clocks the WS2812 bit stream |
| 14 | PC4 (A2) | `AUX_SENSE` |
| 5 | PA1 | `SW_1` |
| 6 | PA2 | `SW_2` |
| 1 | PD4 | `SW_3` |
| 8 | PD0 | `SW_4` |
| 18 | PD1 | `SWIO`: programming pad |
| 4 | PD7 | `NRST`: programming pad |
| 12, 15, 17, 19 | PC2, PC5, PC7, PD2 | no connect (spare) |

* **The switches use different pin numbers (1, 2, 4, 0)**, so each could have its own
  EXTI line if firmware ever wants interrupts instead of polling.
* **PC5 (SPI1 SCK) and PC7 (MISO) stay unconnected**, so the SPI bit stream can run
  on PC6 alone without either pin doing anything.

## Connectors, ID, programming

As on the pot bank:

* **J1 IN, J2 OUT**: BM05B-SRSS-TB (C160391), vertical, bottom side. `+3V3` passes
  from J1 to J2 on In2's pour, `GND` on In1.
* **Board kind 7, `button bank ×4`, R_ID 8.66 kΩ (C23114).** Four `BUTTON` input
  slots, buttons 1–4 in order. Output slots: see [Firmware](#firmware).
* **J5**: SWIO, `+3V3`, `GND`, NRST pads, not fitted, excluded from the BOM.
* No status LED: D1–D4 serve `IDENTIFY`.

## Power

| | Chain 3V3 | Aux 5 V |
|---|---|---|
| MCU, ID divider, aux sense | ≈3 mA | ≈3 mA |
| LEDs | firmware cap, default ≈24 mA total (≈20 % of full scale over the four) | up to 144 mA (all four full white) |
| LED quiescent | 4 µA | — |

On chain power the cap keeps a board's extra current near 21 mA above the 6 mA
baseline; by the contract's rule (§1.4) that's fine anywhere in a chain of 30 if it's
the only lit satellite, and several of them fit in a shorter chain. At the far end of
a long chain `V_LED` can fall to ≈2.9 V, below the LED's rated 3.3 V: expect blue and
green to dim first. Measure on the first build.

## Firmware

The contract now assigns kind 7 and the `RGB` output slot (§1.6, §3.2). The code is
still to be written:

* **Board kind 7** in `sat_frame.h`: four slots, `BUTTON` in and `RGB` out, which
  also means `sat_kind_slots()` stops treating 4 slots as a banks-only case.
* **`SAT_OUT_RGB` = `0x11`**, `D0`–`D1` an RGB565 colour, little-endian, one per
  button. `SAT_OUT_LED` still works on the same slot and shows as white.
* **WS2812 driver** on SPI1 MOSI (PC6) with DMA, as ch32v003fun's
  `ws2812b_dma_spi_led_driver.h` does; 4 LEDs × 24 bits take 120 µs.
* **Brightness cap** from `AUX_SENSE`.

## Library and 3D models

`lib/rodeo_button_bank.pretty` holds copies of two KiCad footprints that point at
models KiCad doesn't ship, in `lib/rodeo_button_bank.3dshapes`:

* **J3/J4, `JST_PH_B2B-PH-SM4-TB_1x02-1MP_P2.00mm_Vertical`:** LCSC's model for
  C160352, turned 180° and moved 2.0 mm along y. Placed by its solder tails and tabs,
  not by EasyEDA's footprint (which disagrees with JST's land pattern): the signal
  pins end 0.5 mm short of the pads' outer ends, the tabs are centred on their pads,
  and the body fills the silkscreen outline.
* **D1–D4, `LED_WS2812B-2020_PLCC4_2.0x2.0mm`:** LCSC has no model for the V6, so this
  is the original WS2812B-2020's (C965555), the same 2.0 × 2.0 × 0.8 mm package; its
  pads match KiCad's footprint exactly.

## Fab outputs

`tools/kicad/jlc_fab.py`: `jlcpcb/` for single boards, `jlcpcb-panel/` for the 1 × 4
panel (100 × 75 mm, rails left and right; tabs at 28 and 50 mm on the left edge and 23.5 and 49 mm on the right,
clear of the LEDs and the parts by the edges). Check the polarised bottom parts (U1,
the JST connectors) in JLC's order preview.

## Parts

| Part | Refs | Qty | LCSC |
|---|---|---|---|
| B3F-1052 | SW1–SW4 | 4 | C87037 |
| WS2812B-2020-V6 | D1–D4 | 4 | C52917434 |
| B5819W | D5 | 1 | C8598 |
| AO3401A | Q1 | 1 | C15127 |
| CH32V003F4P6 | U1 | 1 | C5187096 |
| BM05B-SRSS-TB | J1, J2 | 2 | C160391 |
| B2B-PH-SM4-TB | J3, J4 | 2 | C160352 |
| 1 µF 0402 | C1 | 1 | C52923 |
| 100 nF 0402 | C2–C6 | 5 | C1525 |
| 10 µF 0603 | C7, C8 | 2 | C19702 |
| 33 Ω 0603 | R1, R2 | 2 | C23140 |
| 10 kΩ 0603 1 % | R3 | 1 | C25804 |
| 8.66 kΩ 0603 1 % | R4 | 1 | C23114 |
| 100 Ω 0603 | R5 | 1 | C22775 |
| 100 kΩ 0603 | R6, R7 | 2 | C25803 |

All in stock at JLC on 2026-10-19.

## Open

* **Panel stack-up:** standoff length, switch height and caps, chosen together. The
  board doesn't depend on them beyond the footprint and the ≈9.8 mm cap limit.
* **Light pipe length**, once the standoff length is set.
* **The `RODEO` wordmark** is three overlapping lines that run off the board edge and
  over H1; KiCad flags it as silkscreen warnings, and the fab clips it at the edge.
