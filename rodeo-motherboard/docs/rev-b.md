# Rodeo Motherboard — Rev B

**Status: schematic in progress.** Brainstormed with Derek on 2026-10-04. Later
the same day, the hybrid satellite chain replaced the third ADC (see
[Satellite chain](#satellite-chain-on-usart3-hybrid-analog)). The power sheet was
drawn and reviewed on 2026-10-05/06 (see [Power](#power)). On 2026-10-06 the two
on-board ADCs gave way to a second audio codec (see
[Second codec](#second-codec-on-sai2-4-in--4-out)), which reshaped the pin map.
Everything else is still a plan. The board in hand is Rev A, described in
[`hardware-map.md`](hardware-map.md).

The Rev B KiCad project is `rodeo-motherboard/rev-b/kicad/rodeo-motherboard/`
on the `rev-b` branch. Parts KiCad doesn't ship are in its project library,
`lib/rodeo_jlc.*` (symbols, footprints and STEP models imported from LCSC, each
checked against its datasheet).

**Rev B is a breaking change.** Harness, connector-numbering and `board.h`
compatibility with Rev A are not constraints. rodeo-001 and rodeo-002 get
rewired or remapped as needed if they move to Rev B.

This doc gathers every Rev B item in one place. Before it existed they were
spread across `hardware-map.md`, `rodeo-001/docs/RODEO_CONTROL_MAP.md`,
`rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md` and
`rodeo-sdk/docs/RODEO_ADC_LIB_DESIGN.md`. Those are the history; this is the list.

---

## Seed pin map

Every pin on the 40-pin header is now spoken for. Rev A had seven unconnected
(1, 8, 29, 30, 31, 34, 35), plus USB 36/37 going nowhere.

| Seed pin | STM32 | libDaisy | Rev A | **Rev B** | Net |
|---|---|---|---|---|---|
| 1 | PB12 | `D0` | unconnected | **SPI expansion header chip-select** (10 kΩ pull-up) | `SPI_EXT_CS` |
| 2 | PC11 | `D1` | `DIG_1` | **satellite chain RX** (USART3) | `SAT_RX` |
| 3 | PC10 | `D2` | `DIG_2` | **satellite chain TX** (USART3) | `SAT_TX` |
| 4 | PC9 | `D3` | `DIG_3` | **second codec reset** (10 kΩ pull-down) | `CODEC2_RST` |
| 5–7 | PC8, PD2, PC12 | `D4`–`D6` | `DIG_4`–`DIG_6` | **SD card, 1-bit SDMMC** (D0, CMD, CLK) | `SD_D_0`, `SD_CMD`, `SD_CLK` |
| 8 | PG10 | `D7` | unconnected | digital jack | `DIG_1` |
| 9–11 | PG11, PB4, PB5 | `D8`–`D10` | SPI1 (ADCs) | SPI1, to the expansion header | `SPI_SCK`, `SPI_MISO`, `SPI_MOSI` |
| 12–13 | PB8, PB9 | `D11`, `D12` | I²C1 | I²C1, 4 taps, 2.2 kΩ pull-ups | `DAISY_SCL`, `DAISY_SDA` |
| 14–15 | PB6, PB7 | `D13`, `D14` | MIDI (USART1) | MIDI (unchanged) | `MIDI_TX`, `MIDI_RX` |
| 16–19 | — | — | audio in/out | onboard codec in/out (SAI1) | `AUDIO_IN_1/2`, `AUDIO_OUT_1/2` |
| 20 | — | — | GND | audio ground, 0 Ω (R21) to `GND` at the Seed | `GNDA` |
| 22 | PC0 | `D15`/`A0` | `DIG_7` | digital or analog jack | `DIG_2` |
| 23 | PA3 | `D16`/`A1` | `DIG_8` | digital or analog jack | `DIG_3` |
| 24–28 | PB1, PA7, PA6, PC1, PC4 | `D17`–`D21`/`A2`–`A6` | `DIG_9`–`DIG_12`, status LED | digital or analog jacks (LED removed) | `DIG_4`–`DIG_8` |
| 29–30 | PA5, PA4 | `D22`, `D23` | unconnected | **CV header** (DAC 2, DAC 1) | `CV_DAC2`, `CV_DAC1` |
| 31 | PA1 | `D24` | unconnected | **second codec MCLK** (SAI2) | `CODEC2_MCLK` |
| 32 | PA0 | `D25` | ADC A chip-select | **second codec ADC data in** (SAI2 SD_B) | `CODEC2_SDOUT` |
| 33 | PD11 | `D26` | ADC B chip-select | **second codec DAC data out** (SAI2 SD_A) | `CODEC2_SDIN` |
| 34 | PG9 | `D27` | unconnected | **second codec LRCK** (SAI2 FS) | `CODEC2_LRCK` |
| 35 | PA2 | `D28` | unconnected | **second codec SCLK** (SAI2 SCK) | `CODEC2_SCLK` |
| 36–37 | PB14, PB15 | `D29`, `D30` | USB D−/D+, dead-end | **USB data to J2** | `USB_OTG_HS_N/P` |
| 21, 38 | — | — | Seed 3V3 outputs, unconnected | unconnected (the Seed's own rails) | — |
| 39, 40 | — | — | VIN, GND | `SEED_VIN`, `GND` | |

`CODEC2_SDIN`/`SDOUT` are named from the CS4272's side: SDIN feeds its DAC, SDOUT
comes from its ADC. The SAI2 pins and directions are the ones Daisy Patch uses for
its second codec (`rodeo-sdk/libDaisy/src/daisy_patch.cpp`).

**Digital jacks: 8** (Rev A had 12). They are pins 8 and 22–28. All but pin 8
(PG10) can also read analog through the Seed's own ADC (`A0`–`A6`), and the CV
header's two pins can too (`A7`/`A8`). PB1 (pin 24) and PC1 (pin 27) share
external-interrupt line 1, so don't plan both as interrupt-driven clock or gate
inputs. Jack numbering is left to layout. Buttons are expected to move onto the
satellite chain, so fewer jacks are needed than before.

---

## Decided

### Chip-select pull-up on the SPI bus
10 kΩ to `+3V3_D` on `SPI_EXT_CS` (pin 1), the only chip-select left on the
motherboard. Without it the line floats during boot, reset or reflashing, which
can enable whatever is plugged into the expansion header. Firmware parks it
high (`611f48a`), but only once it is running. The ADC chip-selects (pins
32/33) went with the on-board ADCs. *From: RODEO_CONTROL_MAP.md Issue 6.*

### SPI1 goes to an expansion header (was J43)
With the ADCs off the board (see [ADC expander](#adc-bank-moves-to-an-optional-spi-expander-board)),
SPI1 only serves the expansion header: Rev A's J43, which had no chip-select. It
gets a new reference designator; nets are `SPI_SCK`, `SPI_MISO`, `SPI_MOSI` and
`SPI_EXT_CS`, plus `+3V3_D`, `+3V3_A` and GND. An off-the-shelf SPI OLED or the
ADC expander board plugs in with a plain cable.

* 33 Ω series resistors where each line is driven: SCK and MOSI at the Seed.
  A device's MISO resistor belongs on that device.
* Firmware hand-off only matters if an OLED and the expander ever share the
  bus: SPI mode changes on every switch (the ADCs need mode 1, most displays
  mode 0 or 3), and OLED frames go out in chunks between ADC reads. A second
  device would also need a second chip-select on the header.

**To verify on the bench:** a long or noisy cable on the header adds load to
the bus. The 33 Ω resistors and a modest clock should cover it.

### Satellite chain on USART3 (hybrid analog)
Decided 2026-10-04; moved from USART2 to USART3 on 2026-10-06 (below). Every
control beyond the Seed's own ADC jacks, and every button, goes on a
**daisy-chain of smart satellites** instead of motherboard channels. Each
satellite carries a ~$0.10 MCU that reads its control against its own supply,
which is also its ADC reference, and passes data along a UART ring. A
satellite's position in the chain is its address. Growth means adding
satellites, not respinning the motherboard. The frame format gets its own
contract doc next to [`RODEO_HUB_I2C_CONTRACT.md`](RODEO_HUB_I2C_CONTRACT.md).
It is not written yet.

**Board side**
* **USART3: TX on pin 3 (PC10), RX on pin 2 (PC11), alternate function AF7**
  (libDaisy `seed::D2`/`seed::D1`). It was USART2 on pins 35/23, but USART2's
  TX exists only on PA2, which the second codec needs as SAI2's bit clock.
  Checked 2026-10-06 against libDaisy's `src/per/uart.cpp` pin table: every
  other UART pair on the header lands on pins already used by SD, I²C, SPI,
  MIDI or USB. PC10/PC11 are free only because the SD card drops to 1-bit
  mode.
* One connector: chain 3V3, GND, TX and RX. Its pinout, and how the ring
  closes, come with the contract doc. The ring closes either with a return
  cable from the last satellite or with a return wire through every satellite
  plus a terminator plug.
* A 33–100 Ω series resistor on TX for the cable, and ESD protection on both
  lines.
* **The chain gets its own filtered 3V3 feed, `+3V3_SAT`:** FB4 (BLM21PG221SN1D)
  plus 10 µF and 100 µF electrolytic off `+3V3_D`, so 20–30 satellite MCUs
  switching stay off the other rails (see [Power](#power)). Budget ~3 mA per
  satellite, about 100 mA for 30.

**Firmware side: libDaisy cannot drive this as it stands**
* libDaisy's `UartHandler` uses one fixed RX DMA stream (`DMA1_Stream5`) and
  one TX stream (`DMA2_Stream4`) for every UART. MIDI's `MidiUartHandler` keeps
  the RX stream running permanently in circular listen mode, so starting a DMA
  listen on USART3 would take the stream away from MIDI. Interrupt-only receive
  is not viable at ~1 Mbaud.
* libDaisy also defines `USART3_IRQHandler` (and `USART2_IRQHandler`) itself,
  so a Rodeo driver cannot own that interrupt without a duplicate-symbol link
  error.
* **Plan:** write a small USART3 driver in `rodeo-sdk`, built on the HAL and on
  DMA streams libDaisy leaves free: `DMA1_Stream7` for RX and `DMA2_Stream5` for
  TX. `DMA2_Stream6`/`7` are also free; `DMA2_Stream0`/`1` belong to the DAC,
  which the CV header uses, and `DMA1_Stream3`/`4` to SAI2. Ring frames are a
  fixed length, so RX can be one normal (non-circular) DMA transfer of exactly a
  frame, finished by the DMA stream's own interrupt. The USART interrupt is
  never needed. The fallback is to patch the libDaisy submodule so each UART
  gets its own streams, which is a TODO in libDaisy already ("dynamic DMA Stream
  acquisition").
* **Bench testing on Rev A:** PC10/PC11 are Rev A's `DIG_2`/`DIG_1`
  connectors, so USART3 can be tested on Rev A as it stands, no bodge wires.

### ADC bank moves to an optional SPI expander board
Decided 2026-10-06. Rev A's two ADS114S08s added 24 high-quality analog inputs,
which mattered before the satellite chain existed. Now satellites give nearly
unlimited inputs, so the bank leaves the motherboard and becomes an optional
board on the SPI expansion header. Its pins (32/33) go to the second codec, and
~$19 of ADCs per board goes with it.

* **The expander, when it's built:** one ADS114S08 on `SPI_EXT_CS` (a second
  chip needs a second chip-select on the header), AVDD and REFP0 from the
  header's `+3V3_A` with the pots on the same rail (ratiometric), DVDD/IOVDD on
  `+3V3_D`. CLK to GND, START high, ~RESET pulled up, DRDY and REFOUT unused,
  as on Rev A.
* **Carried over from the Rev A review:** 1 MΩ pull-downs on every input (an
  unused channel otherwise reads noise; under 0.3% loading error with a 10 K
  pot); 330 nF + 1 µF on AVDD, 100 nF on DVDD and IOVDD; JST SH 3-pin pot
  connectors wired 1 = supply, 2 = wiper, 3 = GND (matches `rodeo-potentiometer`).
* **New for the expander:** 100 nF from REFP0 to REFN0; 1 kΩ + 10 nF on each
  input (cable ESD and RF pickup); 33 Ω on each ADC's DOUT.
* **Part:** Rev A's ADS114S08**B**IPBSR is down to 18 at JLC. The plain
  ADS114S08IPBSR (C2670036, 244 in stock) runs the existing driver unchanged; the
  24-bit ADS124S08IPBSR (C2870171, 1,380) is a drop-in whose `RDATA` returns 3
  bytes, not 2. All three have identical pinouts (checked against TI's three
  datasheets). Symbol: `rodeo_jlc:ADS1x4S08_TQFP32`.

### I²C: pull-ups move to the motherboard, 4 taps
* Pull-ups go on the motherboard, so the bus works with no hub attached. The
  Rev B encoder hub drops its own 2× 2.2 kΩ. **Hub and motherboard revisions
  must match:** a Rev A hub on a Rev B motherboard puts ~1.1 kΩ on the bus,
  which is at the limit.
* **2.2 kΩ to `+3V3_D`.** Four taps with the default 150 mm cables come to about
  125 pF, which 2.2 kΩ handles at 400 kHz (≈2.8 kΩ is the limit). Long cables
  (≈215 pF for 4 × 500 mm) need 100 kHz, or 1.5 kΩ for 400 kHz. The contract
  boots the bus at 100 kHz. Capacitances are estimates; measure rise time with
  the longest shipped cable.
* 4 tap connectors on the one bus. No pins needed.
* No multiplexer footprint. There are only four hub addresses (0x30–0x33), the
  OLED (0x3C) and the MAX17048 fuel gauge (0x36), so nothing collides. An
  off-the-shelf Qwiic multiplexer can plug into a tap if that ever changes.

*From: RODEO_CONTROL_MAP.md (I²C pull-ups) and RODEO_HUB_I2C_CONTRACT.md.*

### SD card slot (native mode, 1-bit)
For samples and wavetables, loaded into the Seed's SDRAM. It uses the Seed's
SDMMC interface in **1-bit mode** on pins 5–7 (D0, CMD, CLK). It was 4-bit on
pins 2–7 until 2026-10-06; 1-bit frees PC9–PC11 for the satellite chain and the
second codec's reset.
* **Speed:** 6.25 MB/s raw at libDaisy's default 50 MHz, an estimated 3–5 MB/s
  through FatFS (4-bit would be ~10–15 MB/s). That loads a wavetable in well
  under a second and a minute of stereo audio in ~3 s, and fills all 64 MB of
  SDRAM in ~16 s. Load in the background where it matters. Streaming many
  voices straight from the card is where 1-bit would pinch.
* **Lost:** the Daisy bootloader's update-from-SD, which needs all four data
  lines. USB DFU still works.
* The slot goes **on the motherboard**, close to pins 5–7, at a board edge
  reachable through the case. 25–50 MHz SDMMC does not tolerate a ribbon to a
  daughterboard well.
* Pull-ups on CMD and on **all four** DAT lines at the socket, as the SD spec
  requires. DAT1–DAT3 stay on the card even though the Seed doesn't use them,
  and DAT3 also selects the card's mode at power-up. ESD protection on the
  socket.
* **No card-detect switch.** It would cost a jack, and firmware checks for a
  card when it needs one.
* libDaisy supports 1-bit with `SdmmcHandler::Config::width =
  BusWidth::BITS_1`, and FatFS is built in.

**To verify:** that the Seed3 has the same 64 MB SDRAM as the classic Seed; and
the real 1-bit speed. Rev A's `DIG_1`–`DIG_6` connectors are the SDMMC pins, so a
microSD breakout on them can benchmark 1-bit against 4-bit (at 25 MHz over
flying leads).

### USB data on the panel USB-C, including firmware flashing
Wire the USB-C's D+/D− to Seed pins 36/37 through a USBLC6-2SC6 (U2), with
5.1 kΩ CC pull-downs (R3/R4). The connector is now an HRO TYPE-C-31-M-12 (J2):
Rev A's GCT USB4105-GF-A had run out at LCSC. No external USB PHY or "OTG" chip
is needed: the STM32H750's USB_OTG_HS peripheral has a built-in full-speed PHY
on these pins (libDaisy sets `USB_OTG_EMBEDDED_PHY`), and Rodeo is always the
USB device. Daisy's own dev-kit schematic is wired the same way.
* Gives USB-MIDI, USB audio and serial through libDaisy's `FS_EXTERNAL`.
* **Flashing:** libDaisy ships Daisy bootloader builds marked `extdfu`, which
  accept DFU over pins 36/37 (`rodeo-sdk/libDaisy/doc/md/_a7_Getting-Started-Daisy-Bootloader.md`).
  rodeo-002 already runs under the Daisy bootloader (`APP_TYPE ?= BOOT_SRAM`).
  The Rev B builds install `dsy_bootloader_v6_4-extdfu-2000ms.bin` instead of
  libDaisy's default (`intdfu-2000ms`, `rodeo-sdk/libDaisy/core/Makefile`). The
  bootloader itself still goes in once over the Seed's micro-USB.

**To verify:** the bootloader builds look like either internal *or* external
DFU, not both. If so, after the switch the micro-USB only works for the STM32's
ROM recovery mode.
*Closes hardware-map.md Issue 2.*

### Second codec on SAI2 (4 in / 4 out)
Decided 2026-10-06. A CS4272 on SAI2 adds two audio inputs and two outputs to
the Seed's onboard codec, the way Daisy Patch adds its second codec. It costs
pins 31–35 (from two jacks and the ADC chip-selects) and pin 4 for its reset.

* **CS4272-CZZR** (C2151111, 1,367 at JLC, ~$4.12): 24-bit, 114 dB dynamic
  range on both ADC and DAC, differential inputs and outputs (better ground-noise
  rejection, which helps with Issue 4). Stand-alone mode: pin-strapped (M0/M1,
  I²S/LJ), no control bus. Set for left-justified 24-bit, which is what
  libDaisy's 24-bit SAI mode sends.
* **Clocks:** the Seed is master. SAI2 drives MCLK at 256 × fs (12.288 MHz at
  48 kHz), SCLK and LRCK, as for Daisy Patch's codec.
* **Reset (`CODEC2_RST`, pin 4, 10 kΩ pull-down):** the datasheet requires RST
  low until MCLK and LRCK are stable, and they only start once firmware starts
  SAI2. So firmware releases it; an RC can't guarantee the order.
* **Supplies:** VA from `+5V_A`; VD and VL from `+3V3_D` (VL sets the logic
  level, matching the Seed). Full scale at VA = 5 V is ≈5.65 Vpp differential in
  and ≈4.8 Vpp differential out.
* **Analog front end (to design with the audio sheet):** a dual op-amp per
  direction on `+5V_A`: single-ended line in to differential, and differential
  to single-ended line out, biased at the codec's VCOM.

**To verify:** Daisy Patch's dual-codec setup re-initialises SAI1 with its own
Seed's settings. Ours must reuse whatever libDaisy configures for the Seed3,
which libDaisy doesn't name (it knows the AK4556, WM8731 and PCM3060 Seeds).
See the Seed3 check in `hardware-map.md`.

### Power
Drawn on the power sheet and reviewed against every part's datasheet, 2026-10-06.
USB-C is a plain 5 V input (no USB-PD), so a laptop can run and flash the board.
A built-in single-cell Li-ion battery and an optional power daughterboard are the
other two sources. Anything big (LED strips and the like) is powered by the
daughterboard directly, never through the motherboard.

```
USB-C 5 V (J2) ─ SMBJ5.0A ─┐
                           ├─ U3 TPS2121 ─ U1 BQ24073 ─ BATT_VCC ─ U6 TPS63070 ─ +5V_SYS
Power board 5 V (J3) ──────┘  mux          charger +               buck-boost,
                              J3 first     power path              EN ← SW1
                                                 │ BAT ─ U5 MAX17048 (I²C 0x36)
1S Li-ion (J1) ─ U4 DW01A + Q1 FS8205A ──────────┘

+5V_SYS ─┬─ U7 TPS563208 buck ─────────► +3V3_D   ─ FB4 ─► +3V3_SAT (satellite chain)
         ├─ FB1 ─ U8 TPS7A2033 LDO ────► +3V3_A   (SPI expansion header: ADC expander)
         ├─ FB2 + 10 µF + 100 µF ──────► SEED_VIN (Seed pin 39)
         └─ FB3 + 10 µF + 100 µF ──────► +5V_A    (headphone-amp / CV daughterboards)
```

**Inputs**
* **USB-C (J2):** 5 V only, 5.1 kΩ CC pull-downs, SMBJ5.0A on VBUS, USBLC6 on
  D+/D−.
* **Power daughterboard (J3, JST B4B-XH-A):** pins 1–2 GND, 3–4 `EXT_PWR`, a
  regulated 5 V from the board's own barrel-jack supply. XH can't be confused
  with the battery's PH plug.
* **Mux (U3, TPS2121), automatic switchover with priority:** PR1 = 28 k/15 k
  from `EXT_PWR`, CP2 = 34 k/15 k from USB, so the power board wins while it is
  above ≈0.88 × the USB voltage (≈4.4 V). ILM 61.9 kΩ (≈2 A), SS 100 nF, OV1/OV2/ST
  grounded. It also stops either source back-feeding the other.

**Charger and battery**
* **U1, BQ24073** (charger with power path). The system runs from the input and
  the cell charges from what's left. It runs with no battery or a dead one, and
  the cell tops up the system when the input can't keep up. VIN-DPM backs off
  at 4.5 V, so a weak laptop port doesn't crash. ILIM 1.18 kΩ sets the input
  limit to ≈1.36 A (fits a 1.5 A USB-C port). ISET 1.13 kΩ sets 0.79 A charge.
  TS has 10 kΩ (no thermistor in typical packs). TMR is left open (default
  safety timers) and TD is grounded. PGOOD drives a green LED (D2) and CHG a red
  one (D3).
* **Battery connector (J1):** JST PH 2-pin, side entry. Hobby packs disagree on
  PH polarity, so the silkscreen must mark +.
* **Protection:** DW01A (U4) + FS8205A (Q1) in the cell's negative lead. Trips at
  4.30 V overcharge, 2.50 V overdischarge, and 150 mV across the FETs (≈3–3.75 A).
  **Board ground is the cell's negative after the FETs (`BATT_N` → S1–S2 →
  `GND`);** `BATT_N` touches only J1, U4, Q1 and C6.
* **Fuel gauge:** MAX17048 (U5) on the I²C bus at 0x36, CELL/VDD on `BATT_P`.
* **No reverse-polarity protection.** A high-side PMOS doesn't protect a
  low-side DW01A. Rely on the keyed connector and the silkscreen.

**+5V_SYS: U6, TPS63070 buck-boost**
* 2.0–16 V in, 2 A out, forced PWM at 2.4 MHz (PS/SYNC grounded), so 5 V holds
  from a sagging laptop port and from a 3.0 V cell alike. FB divider 100 k/19.1 k
  gives 4.99 V. L1 Sunlord MWSA0518S-1R5MT (1.5 µH, 8 A sat). Caps per TI's
  reference BOM: 3 × 10 µF in, 10 µF + 3 × 22 µF out, 100 nF on VAUX.
* **Power switch (SW1):** Yuandi TK-6580A-1, right-angle latching push button
  (5.8 × 5.1 × 5.8 mm, plunger 4.8 mm past the body). Latched in connects EN to
  `BATT_VCC`. R18 1 MΩ pulls EN down, so off draws nothing. The cell still
  charges with the switch off. Mount the body against the panel: the plunger
  only clears a ~1.6 mm panel by ≈3 mm.

**Rails below +5V_SYS**
* **`+3V3_D`, U7 TPS563208:** forced-continuous 580 kHz buck. L2 MWSA0518S-2R2MT
  (2.2 µH), 33.2 k/10 k divider (3.32 V), 2 × 22 µF out. Feeds satellites (via
  `+3V3_SAT`), hubs, OLED, I²C taps, power taps, the second codec's VD/VL, and
  the expansion header.
* **`+3V3_A`, U8 TPS7A2033:** 7 µV RMS, 95 dB PSRR at 1 kHz, fed from +5V_SYS
  through FB1 (BLM18KG601SN1D) with 1 + 10 µF either side. Designed for the
  ADCs' AVDD/REFP0 and their pots; since the ADCs moved off-board (2026-10-06)
  it goes to the SPI expansion header as the expander's analog supply.
  Replaces Rev A's U2 TLV76733.
* **`SEED_VIN`, `+5V_A`, `+3V3_SAT`:** each a BLM21PG221SN1D ferrite feeding
  10 µF ceramic + 100 µF electrolytic. The electrolytic's ESR damps the
  ferrite/ceramic resonance; don't swap it for a low-ESR part. `SEED_VIN`'s caps
  sit at Seed pin 39. `+5V_A` is filtered but not re-regulated.

**Budget.** Design to a 1.5 A USB-C port (7.5 W). Rodeo itself is ≈2 W, which
leaves ≈1 A at 5 V for LEDs and taps: ~15–20 LEDs at full white, many more
dimmed. On battery, keep +5V_SYS under ≈1.2 A or the DW01A's overcurrent trip
gets close. A 500 mA USB-A port runs Rodeo with LEDs dark; there are no spare
pins for detecting the port's current, so firmware should default to dim LEDs.
Runtime is roughly 3 h on 2000 mAh, 5 h on a 3400 mAh 18650.

**Layout notes:** BQ24073 can dissipate 1–1.4 W while charging and running, so
it needs a solid ground pour with vias under its pad. Keep the TPS563208 and
TPS63070 switch nodes small, with their input caps at the pins. Put power entry
and LED connectors at one end, and the Seed audio and the second codec at the
other.

Every part has an `LCSC Part` field. All are stocked at JLC as of 2026-10-06.

### CV header, pins 29/30 reserved
The Seed's only DACs. They go to a small header (DAC 1, DAC 2, GND, `+3V3_D`, `+5V_A`)
for a future CV daughterboard. The DACs only output 0–3.3 V, so useful CV ranges
need an op-amp stage, and that belongs on the daughterboard. Both pins can also
read analog voltages, so the same header can take CV inputs.

### Removed
* **Status LED** (D5, R10). The Seed's own LED stays. Pin 28 becomes a jack.
* **The two on-board ADS114S08s,** their 24 pot connectors, pull-downs and
  chip-select pull-ups. They become the optional
  [ADC expander](#adc-bank-moves-to-an-optional-spi-expander-board).

### Housekeeping
* **Fix the schematic hierarchy.** The root sheet instantiates its subsheets,
  so ERC and "update PCB from schematic" work and the schematic becomes the
  source of truth again. *Closes hardware-map.md Issue 3.*

### Not happening
* **Power jumpers.** That was a miscommunication. The five power taps J45–J49
  as on Rev A are all that's needed. Their pin 3 was `VIN`; on Rev B it will be
  `+5V_SYS`, and pin 2 `+3V3_D` (not drawn yet).

---

## Open — audio and power

These hang together through the grounding question. The bench tests in
[`hardware-map.md` Issue 4](hardware-map.md) decide how much of it is needed,
so they come first.

* **The hum on the line out that grows with each held key** (hardware-map.md
  Issue 4). The power design now covers three of the candidate fixes: bulk at
  the Seed's VIN (C28 100 µF), a ferrite between the supply and VIN (FB2), and a
  clean-supply option (the battery takes the USB host's ground out of the path).
  Still open: a dedicated ground return for the J4/J5 sleeves, and a series R on
  J5. Running Rev B from the battery against USB is now a useful A/B test.
* **Filtering on audio I/O.** An RC on the outputs (J5 has no series R today),
  ESD protection on every jack contact, and a check of the input against hot
  line-level sources. Now for both codecs' jacks.
* **The second codec's analog front end** (see
  [Second codec](#second-codec-on-sai2-4-in--4-out)): op-amp choice for 5 V
  single supply, gain staging to line level, and whether its jacks share the
  onboard codec's ground scheme (`GNDA`).
* **Headphone output with volume control**, probably as a daughterboard. Which
  amp, and whether volume is an analog pot after the amp (0 pins) or software.
  Optionally 1 pin for mute (pop suppression) and 1 for headphone detect, which
  would come out of the 8 digital jacks. Its supply is `+5V_A`.
* **1/4" vs 1/8" jacks, and whether the audio jacks move to a daughterboard.**
  If they move, the connector needs its own audio-ground pins rather than the
  shared board ground. Done carelessly it makes the hum worse; done deliberately
  it is the dedicated return Issue 4 asks for. The Seed's outputs are
  single-ended, so a 1/4" TRS jack would not be balanced on its own.

---

## Considered and rejected

| Option | Why not |
|---|---|
| Third ADS114S08 (U5), 36 analog channels | Planned until 2026-10-04. The satellite chain goes past 24 controls by adding satellites, with no extra motherboard channels, connectors (J50–J61) or pull-downs. Dropping it frees pin 8. *Was: RODEO_CONTROL_MAP.md, "REV-B UPGRADE PATH".* |
| Encoder hub as the satellite-ring master | Costs no Seed pins, but adds a hop, puts every reading through I²C (~60 bytes a scan) and changes the hub firmware. A UART on the Seed is direct and DMA-capable. |
| Second SPI bus (SPI6 on pins 29/30 + `DIG_10`/`DIG_11`) | SPI6 is the only one that reaches the header (SPI2's clock isn't broken out, SPI3 sits on the SDMMC pins). It would take the DAC pins and 1–2 jacks, and libDaisy has no DMA for SPI6. SPI devices are uncommon in DIY synths, and the OLED is the likely one, which a shared SPI1 serves. |
| SD card over SPI | Too slow for samples and wavetables, and libDaisy's FatFS path is SDMMC. |
| Second hardware I²C bus | Per the STM32H750 datasheet (from memory, not re-checked), the only other I²C peripheral that reaches the header is on the MIDI pins. Several taps on one bus covers the need. |
| Two (or one) ADS114S08 on the motherboard | Rejected 2026-10-06 for the second codec. Satellites now provide analog inputs; the ADC bank lives on as an optional expander board. One on-board ADC plus the codec (chip-select on pin 4) was the compromise considered: 12 inputs for ~$9 and a jack. |
| Multichannel codec on SAI2 (PCM3168A, 6 in / 8 out) | libDaisy's SAI driver is stereo I²S only (two slots per frame), so anything past 2 + 2 on SAI2 needs TDM written into `SaiHandle` and `AudioHandle`, plus 14 channels of analog front end. Revisit if 4 / 4 isn't enough. |
| PCM1808 + PCM5102A, CS4270, AK4556 | The pair is cheaper (~$2.14) but 99 dB on the ADC and single-ended. CS4270 is a step below the CS4272. AK4556 (Daisy Patch's codec) has no JLC stock. |
| Satellite chain on USART2 (pins 35/23) | USART2 TX only exists on PA2, which SAI2 needs as SCK. |
| 4-bit SD | Needs PC9–PC11, which became the satellite chain and the codec reset. Costs speed and the bootloader's update-from-SD. |
| SD card-detect switch | Costs a jack, see above. |
| USB-PD input (HUSB238 + 9–12 V) with 5 V and 3.3 V rails at 3 A each | 25 W needs a 30 W+ PD charger and leaves a laptop unable to run the board. LEDs were the only reason for 3 A, and they go on the power daughterboard. The HUSB238 also runs BC1.2 detection on D+/D−, which would fight the Seed's USB link. |
| Separate LED rail (TPS55288, firmware-enabled, current-limited) | Only needed with the PD design. Its output also stays off until I²C enables it, so it can't power the Seed. |
| Power daughterboard carrying the battery | Battery is wanted on the motherboard. The daughterboard is now only a 5 V barrel-jack supply. |
| Alps SPUJ190900 power switch | Right type and quality, but 7.5 × 15.2 × 8.8 mm. The TK-6580A-1 does the job in a quarter of the area. |
| Slide switch for power | Smallest and cheapest, but fiddly through a panel slot. Fallback if the panel turns out too thick for the TK-6580's reach. |
| Reverse-polarity PMOS on the battery | Doesn't protect a DW01A in the negative lead. |

---

## Firmware consequences (when Rev B exists)

* `rodeo-sdk/common/board.h`: new map. 8 jacks on `seed::D7`, `D15`–`D21` (seven
  of them analog-capable as `A0`–`A6`); expansion-header chip-select on
  `seed::D0`; satellite chain on USART3, `seed::D2` (TX) / `seed::D1` (RX);
  second codec reset on `seed::D3`; CV pins on `D22`/`D23`.
* **Audio:** run SAI1 (onboard codec) and SAI2 (CS4272) together through
  `AudioHandle::Init(cfg, sai1, sai2)`, as `daisy_patch.cpp` does, for 4 in /
  4 out in one callback. Release `CODEC2_RST` after SAI2's clocks are running.
* **SD:** `SdmmcHandler` with `BusWidth::BITS_1`.
* `RodeoAdc` moves with the ADCs to the expander: one chip on `SPI_EXT_CS`.
  The existing driver works unchanged with an ADS114S08; an ADS124S08 needs
  `ReadData()` to take 3 bytes.
* New: the satellite-chain driver (USART3 on `DMA1_Stream7`/`DMA2_Stream5`, see
  [Satellite chain](#satellite-chain-on-usart3-hybrid-analog)) and the ring
  protocol, plus the satellite firmware itself.
* SPI1 hand-off only if an OLED and the ADC expander are both connected.
* Bootloader: `extdfu` build. App makefiles keep `BOOT_SRAM`. Update-from-SD is
  no longer available.
* New: a MAX17048 driver (I²C 0x36) for battery % and a save-state-and-dim
  warning before the cell runs out.
* LED brightness defaults low, since the board can't tell a 500 mA USB-A port
  from a 1.5 A one. The charger's PGOOD/CHG and the mux's ST don't reach the
  Seed.
* The hub firmware is unaffected. The Rev B hub only loses two resistors.
