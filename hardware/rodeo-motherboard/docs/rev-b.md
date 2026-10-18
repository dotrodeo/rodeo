# Rodeo Motherboard — Rev B

**Status: ordered, not yet built.** Schematic, layout and routing are done, the
board is DRC-clean, and its fab outputs and silkscreen are final. It was ordered from
JLC on 2026-10-18 with the seven Rev B daughterboards and the five rear panels, a
first run of five of each. Nothing on Rev B has been on a bench yet, so every "decided" item below is
a design decision, not a measured result; the bench work each one still needs is
noted where it comes up. The board in hand is still Rev A, described in
[`hardware-map.md`](../../../rev-a/rodeo-motherboard/docs/hardware-map.md).

How it got here:

* **2026-10-04:** brainstormed with Derek. Later the same day the hybrid satellite
  chain replaced the third ADC (see
  [Satellite chain](#satellite-chain-on-usart3-hybrid-analog)).
* **2026-10-05/06:** power sheet drawn and reviewed (see [Power](#power)).
* **2026-10-06:** the two on-board ADCs gave way to a second audio codec (see
  [Second codec](#second-codec-on-sai2-4-in--4-out)), which reshaped the pin map.
* **2026-10-06/07:** Daisy, Codec and Audio Interface sheets (see
  [Audio interface](#audio-interface-and-jack-board)), then the
  [SD](#sd-card-slot-native-mode-1-bit) and [MIDI](#midi-moves-to-a-daughterboard)
  sheets.
* **2026-10-08:** connectors sheet; power input and switch moved to cabled
  daughterboards; first-draft placement from the [Layout notes](#layout-notes).
* **2026-10-12:** fully routed, DRC-clean, first fab outputs.
* **2026-10-16:** Kickstarter silkscreen (see
  [`silkscreen.md`](../../docs/silkscreen.md)), outputs regenerated and uploaded
  to JLC.

The KiCad project is this folder, `hardware/rodeo-motherboard/`. Parts KiCad doesn't
ship are in its project library, `lib/rodeo_jlc.*` (symbols, footprints and STEP
models imported from LCSC, each checked against its datasheet). Every placed part has
an LCSC number, audited against JLC.

**Rev B is a breaking change.** Harness, connector-numbering and `board.h`
compatibility with Rev A are not constraints. rodeo-001 and rodeo-002 get
rewired or remapped as needed if they move to Rev B.

This doc gathers every Rev B item in one place. Before it existed they were
spread across `hardware-map.md`, `instruments/rodeo-001/docs/RODEO_CONTROL_MAP.md`,
`instruments/rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md` and
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
| 8 | PG10 | `D7` | unconnected | I/O connector (digital only) | `DIG_1` |
| 9–11 | PG11, PB4, PB5 | `D8`–`D10` | SPI1 (ADCs) | SPI1, to the expansion header | `SPI_SCK`, `SPI_MISO`, `SPI_MOSI` |
| 12–13 | PB8, PB9 | `D11`, `D12` | I²C1 | I²C1, 4 taps, 2.2 kΩ pull-ups | `DAISY_SCL`, `DAISY_SDA` |
| 14–15 | PB6, PB7 | `D13`, `D14` | MIDI (USART1) | MIDI (unchanged) | `MIDI_TX`, `MIDI_RX` |
| 16–19 | — | — | audio in/out | onboard codec in/out (SAI1) | `AUDIO_IN_1/2`, `AUDIO_OUT_1/2` |
| 20 | — | — | GND | audio ground, 0 Ω (R14) to `GND` at the Seed | `GNDA` |
| 22 | PC0 | `D15`/`A0` | `DIG_7` | I/O connector (digital or analog) | `DIG_2` |
| 23 | PA3 | `D16`/`A1` | `DIG_8` | I/O connector (digital or analog) | `DIG_3` |
| 24–28 | PB1, PA7, PA6, PC1, PC4 | `D17`–`D21`/`A2`–`A6` | `DIG_9`–`DIG_12`, status LED | I/O connectors, digital or analog (LED removed) | `DIG_4`–`DIG_8` |
| 29–30 | PA5, PA4 | `D22`, `D23` | unconnected | **I/O connectors CV2/CV1** (DAC out or analog in) | `CV_DAC2`, `CV_DAC1` |
| 31 | PA1 | `D24` | unconnected | **second codec MCLK** (SAI2) | `CODEC2_MCLK` |
| 32 | PA0 | `D25` | ADC A chip-select | **second codec ADC data in** (SAI2 SD_B) | `CODEC2_SDOUT` |
| 33 | PD11 | `D26` | ADC B chip-select | **second codec DAC data out** (SAI2 SD_A) | `CODEC2_SDIN` |
| 34 | PG9 | `D27` | unconnected | **second codec LRCK** (SAI2 FS) | `CODEC2_LRCK` |
| 35 | PA2 | `D28` | unconnected | **second codec SCLK** (SAI2 SCK) | `CODEC2_SCLK` |
| 36–37 | PB14, PB15 | `D29`, `D30` | USB D−/D+, dead-end | **USB data to the power board (J2)** | `USB_OTG_HS_N/P` |
| 21, 38 | — | — | Seed 3V3 outputs, unconnected | unconnected (the Seed's own rails) | — |
| 39, 40 | — | — | VIN, GND | `SEED_VIN`, `GND` | |

`CODEC2_SDIN`/`SDOUT` are named from the CS4272's side: SDIN feeds its DAC, SDOUT
comes from its ADC. The SAI2 pins and directions are the ones Daisy Patch uses for
its second codec (`rodeo-sdk/libDaisy/src/daisy_patch.cpp`).

**General-purpose I/O: 10 connectors** (Rev A had 12 digital ones). They are
pins 8 and 22–28 (`DIG_1`–`DIG_8`) plus the two DAC pins 29/30. All but pin 8
(PG10) can also read analog through the Seed's own ADC (`A0`–`A8`). PB1 (pin 24)
and PC1 (pin 27) share external-interrupt line 1, so don't plan both as
interrupt-driven clock or gate inputs. Buttons are expected to move onto the
satellite chain, so fewer are needed than before. See
[Generic I/O connectors](#generic-io-connectors).

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
`SPI_EXT_CS`, plus `+3V3_D`, `+3V3_A` and GND. It's a 7-pin JST SH
(BM07B-SRSS-TB, C160393); pins 1–5 keep Rev A's J43 order:

| Pin | Signal |
|---|---|
| 1 | `+3V3_D` |
| 2 | GND |
| 3 | `SPI_MOSI` (33 Ω at the Seed) |
| 4 | `SPI_MISO` |
| 5 | `SPI_SCK` (33 Ω at the Seed) |
| 6 | `SPI_EXT_CS` (10 kΩ pull-up) |
| 7 | `+3V3_A` (for the ADC expander's ratiometric pots) |

The ADC expander board plugs in with a plain cable. An SPI OLED works too, but
most need a data/command (DC) pin and often a reset pin beyond SPI; those would
come from the generic I/O connectors.

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
satellites, not respinning the motherboard. The frame format, discovery
and fault handling are in
[`RODEO_SATELLITE_CHAIN_CONTRACT.md`](RODEO_SATELLITE_CHAIN_CONTRACT.md) (draft v1,
2026-10-08).

**Board side**
* **USART3: TX on pin 3 (PC10), RX on pin 2 (PC11), alternate function AF7**
  (libDaisy `seed::D2`/`seed::D1`). It was USART2 on pins 35/23, but USART2's
  TX exists only on PA2, which the second codec needs as SAI2's bit clock.
  Checked 2026-10-06 against libDaisy's `src/per/uart.cpp` pin table: every
  other UART pair on the header lands on pins already used by SD, I²C, SPI,
  MIDI or USB. PC10/PC11 are free only because the SD card drops to 1-bit
  mode.
* **One chain connector: JST SH 5-pin** (decided 2026-10-08), the same
  connector on each satellite's IN and OUT:

  | Pin | Signal | Satellite IN | Satellite OUT | Motherboard |
  |---|---|---|---|---|
  | 1 | GND | GND | GND | GND |
  | 2 | 3V3 | supply in | supply out | `+3V3_SAT` through a PTC fuse |
  | 3 | `DATA_FWD` | MCU RX | MCU TX (normally) | `SAT_TX` through 33 Ω |
  | 4 | `DATA_RET` | pass-through trace to OUT pin 4, MCU return pin tapped on | ← | `SAT_RX`, 10 kΩ pull-up |
  | 5 | `PRESENT` | tied to GND | MCU input, pull-up | not connected |

* **The ring closes itself, with nothing plugged into the last satellite.** A
  satellite whose OUT `PRESENT` reads high (nothing downstream) is last: it
  moves its UART TX from the `DATA_FWD` pin to its `DATA_RET` tap, so its frames
  turn around and pass back through every satellite to the Seed. Everyone else
  leaves the tap high-impedance. Considered and set aside: a terminator plug (a
  part to make and lose), a return cable to a second motherboard connector
  (a long cable across the enclosure), and a firmware handshake on `DATA_FWD`
  instead of the `PRESENT` pin (timing-dependent at power-up).
* **Satellite MCU requirement:** UART TX must be remappable at runtime to a
  second pin (the `DATA_RET` tap). Copying TX in software can't keep up at
  ~1 Mbaud. The CH32V003's USART1 has four pin mappings; check the chosen MCU's
  datasheet.
* **Contract requirement: a "close the ring here" command.** A dead or unflashed
  satellite still pulls `PRESENT` low, so the board before it keeps sending into
  a dead end and the whole ring goes quiet. Commands still reach every satellite
  up to the break, so the Seed can tell satellite *k* to close the ring, step
  *k* until frames return, and keep the chain running up to the fault.
* **Power budget: ≈6 mA per satellite at 30 satellites** (MCU plus a 1–2 mA
  LED). The chain is fed from one end; each link (150 mm of 28 AWG plus two
  connector matings, out and back) is ≈0.14 Ω, so the last of 30 satellites sits
  ≈67 Ω × (current per satellite) below the motherboard: 0.4 V at 6 mA, leaving
  2.9 V. The chain stays on 3.3 V rather than 5 V so logic levels match the
  Seed. Anything LED-heavy gets its own power, outside the chain.
* **Motherboard protection:** a 0.75 A PTC (nSMD075, C70078) between
  `+3V3_SAT` and the connector, so a crushed cable can't pull down `+3V3_D`;
  33 Ω on `SAT_TX`; ESD protection on pins 3 and 4.
* A cable plugged into the wrong 5-pin SH (the MIDI connector) is harmless both
  ways: a satellite on the MIDI port gets no power, and the passive MIDI board on
  the chain port puts 3.3 V only on an unplugged jack's ring. Label both on the
  silkscreen.
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
  which the CV outputs use, and `DMA1_Stream3`/`4` to SAI2. Ring frames are a
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
  connectors wired 1 = GND, 2 = wiper, 3 = supply (matches `rodeo-potentiometer`).
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
* 4 tap connectors on the one bus. No pins needed. 4-pin JST SH (BM04B-SRSS-TB,
  C160390), Rev A's J42 and Qwiic pinout: 1 GND, 2 `+3V3_D`, 3 SDA, 4 SCL.
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
* The slot goes **on the motherboard**, close to pins 5–7. 25–50 MHz SDMMC does
  not tolerate a ribbon to a daughterboard well.
* **The card is internal** (decided 2026-10-13): no panel access. Its contents
  are loaded over USB instead, with Rodeo showing up on the computer as a USB
  drive. That needs firmware: libDaisy carries ST's USB-device mass-storage class
  in its middleware but doesn't build or use it (it uses only the host MSC class,
  for USB sticks). A Rodeo build would add the MSC device class backed by the
  SDMMC driver, composite with USB-MIDI and serial on the one port. Full-speed
  USB moves roughly 0.5–1 MB/s, so a 64 MB sample set takes about a minute.
* Pull-ups on CMD and on **all four** DAT lines at the socket, as the SD spec
  requires. DAT1–DAT3 stay on the card even though the Seed doesn't use them,
  and DAT3 also selects the card's mode at power-up. ESD protection on the
  socket.
* **No card-detect switch.** It would cost a jack, and firmware checks for a
  card when it needs one.
* libDaisy supports 1-bit with `SdmmcHandler::Config::width =
  BusWidth::BITS_1`, and FatFS is built in.

**As drawn** (`sd.kicad_sch`, 2026-10-07):
* Socket: SHOU HAN TF PUSH, push-push (C393941), in the project library with
  its 3D model. Its CD pin is left unconnected.
* 47 kΩ pull-ups to `+3V3_D` on CMD and DAT0–DAT3; 10 µF + 0.1 µF at VDD.
* 22 Ω series on CLK as a source termination. **On the PCB it goes next to the
  Seed**, not the socket.
* SRV05-4 ESD array (C13612) at the socket on CLK (socket side of the 22 Ω),
  CMD, DAT0 and DAT3.

**To verify:** that the Seed3 has the same 64 MB SDRAM as the classic Seed; and
the real 1-bit speed. Rev A's `DIG_1`–`DIG_6` connectors are the SDMMC pins, so a
microSD breakout on them can benchmark 1-bit against 4-bit (at 25 MHz over
flying leads).

### USB data through the power board, including firmware flashing
Seed pins 36/37 go to the power input connector (J2 pins 3/4), and the power
daughterboard carries them to its panel USB-C (see [Power](#power)). That board
holds the receptacle's 5.1 kΩ CC pull-downs and a USBLC6-2SC6 at the jack. Until
2026-10-08 the USB-C (HRO TYPE-C-31-M-12; Rev A's GCT USB4105-GF-A had run out at
LCSC) was on the motherboard. No external USB PHY or "OTG" chip is needed: the
STM32H750's USB_OTG_HS peripheral has a built-in full-speed PHY on these pins
(libDaisy sets `USB_OTG_EMBEDDED_PHY`), and Rodeo is always the USB device.
Daisy's own dev-kit schematic is wired the same way.
* Gives USB-MIDI, USB audio and serial through libDaisy's `FS_EXTERNAL`.
* **Flashing:** libDaisy ships Daisy bootloader builds marked `extdfu`, which
  accept DFU over pins 36/37 (`rodeo-sdk/libDaisy/doc/md/_a7_Getting-Started-Daisy-Bootloader.md`).
  rodeo-002 already runs under the Daisy bootloader (`APP_TYPE ?= BOOT_SRAM`).
  The Rev B builds install `dsy_bootloader_v6_4-extdfu-2000ms.bin` instead of
  libDaisy's default (`intdfu-2000ms`, `rodeo-sdk/libDaisy/core/Makefile`). The
  bootloader itself still goes in once over the Seed's micro-USB.
* A power board without USB data (barrel only) leaves flashing to the Seed's
  micro-USB.

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
* **Analog front end:** see [Audio interface](#audio-interface-and-jack-board).

**To verify:** Daisy Patch's dual-codec setup re-initialises SAI1 with its own
Seed's settings. Ours must reuse whatever libDaisy configures for the Seed3,
which libDaisy doesn't name (it knows the AK4556, WM8731 and PCM3060 Seeds).
See the Seed3 check in `hardware-map.md`.

### Audio interface and jack board
Decided and drawn 2026-10-07 (`audio_interface.kicad_sch`). The audio jacks move
to a passive jack board on a 2×10 IDC ribbon. All eight audio signals (the
Seed's 2 in / 2 out and the CS4272's 2 in / 2 out) are aimed at the same level,
about 4 Vpp single-ended at the jack.

**Measured Seed levels** (bench, 2026-10-07, with `rodeo-sdk/examples/10_audio_levels`,
which steps a 1 kHz sine through 1.0/0.5/0.25/0.125 and prints input peaks).
The module reports itself to libDaisy as an AK4556 Seed (Rev4).

| | Full scale | Notes |
|---|---|---|
| Seed output | ≈4 Vpp | AC-coupled, ≈250 Ω out (2 V → 1.6 V peak into 1 kΩ) |
| Seed input | ≈3.9–4.1 Vpp | out → in loopback is unity within ±3 % |

The Seed's own path gets no front end: its outputs go straight to the ribbon,
and its inputs get 100 Ω in series (R56/R57) on the motherboard.

**Ribbon (J4, DC3-2.54-20PAS box header, C2977593).** Odd pins 1–19 are all GND,
interleaved with the signals; the jack board ties its jack sleeves to them and
the motherboard returns them at a single point in the audio corner.

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| 2 | `AUDIO_OUT_1` (Seed) | 12 | `JACK_IN_1` (→ 100 Ω → Seed in 1) |
| 4 | `AUDIO_OUT_2` (Seed) | 14 | `JACK_IN_2` (→ 100 Ω → Seed in 2) |
| 6 | `CODEC2_OUT_A` | 16 | `CODEC2_IN_A` |
| 8 | `CODEC2_OUT_B` | 18 | `CODEC2_IN_B` |
| 10 | `+5V_A` (spare, for jack-board extras) | 20 | not connected |

**CS4272 inputs (U9, U10: THS4521, C170157).** One fully differential amp per
channel on `+5V_A`, single-ended to differential with a gain of 1.4
(7.5 kΩ in, 10.5 kΩ ∥ 150 pF feedback), so a 4 Vpp jack signal is ≈5.6 Vpp
differential, the codec's full scale. Input: 1 MΩ to GND, 10 µF electrolytic
(+ toward the amp); the other input leg goes to GND through a matching 10 µF and
7.5 kΩ. VOCM is left to the internal divider (2.5 V) with 0.1 µF. Outputs go
through 91 Ω each, with 2.7 nF C0G across the codec pins.

**CS4272 outputs (U8: OPA2376, C46316).** The Cirrus datasheet's
differential-to-single-ended filter (CS4272 Fig. 14) per channel, then 22 µF
(+ toward the amp), 560 Ω series and 47 kΩ to GND. Output is ≈3.7 Vpp, within
1 dB of the Seed's.

All capacitors in the signal path are C0G; at 1.5–6.8 nF that means 0603. Every
part on the sheet has its LCSC number, all in stock at JLC on 2026-10-07.

**Output mute (added 2026-10-08).** The CS4272's active-low mute outputs
(`~AMUTEC` pin 23, `~BMUTEC` pin 28, as `CODEC2_MUTE_A/B`) drive a shunt on each
output's connector node (`CODEC2_OUT_A/B`, J4 pins 6/8). Cirrus's suggested circuit
(datasheet Fig. 16) needs a negative rail to hold its transistor off; ours runs from
`+5V_A`:
* Two N-FETs back-to-back (Q3, Q4: 2N7002DW, C83571), sources joined, one drain on
  the output and one on GND. Their body diodes oppose, so when off the output swings
  ±1.85 V without clipping. 1 MΩ (R31/R45) from the gates to the joined sources holds
  them off with no negative supply.
* A P-FET (Q2: BSS84DW, C161617, one half per channel) from `+5V_A` pulls the gates
  up through 10 kΩ (R28/R42) when the mute pin is low.
* Muted: the output is shorted through ≈4–15 Ω against R36/R37 (560 Ω), at least
  30 dB down.
* **Power-up:** the mute pins are low from the moment `+5V_A` rises and the codec
  stays in reset until firmware starts SAI2, so the 22 µF coupling caps charge
  through 560 Ω into the shunt (≈12 ms) instead of into the jack. No thump.
* The codec also mutes a channel after 8,192 zero samples (≈170 ms of digital
  silence), so idle outputs sit at true 0 V.
* Not covered: power-down. Gate drive collapses with `+5V_A`, so a small pop at
  switch-off can remain. Put Q3/Q4 right at R36/R39, R37/R40 and J4.

**Known trade-offs:**
* **Power-up thump on the Seed's own outputs** (`AUDIO_OUT_1/2`): the Seed
  AC-couples them on the module, out of reach. Firmware fade-in if it matters.
* **Polarised input caps** carry ≈+1 V. A sustained negative DC input (Eurorack
  CV) would reverse-bias them; swap to bipolar electrolytics, same footprint, if
  Rodeo gets patched with modular gear.
* **Protection:** the codec inputs see ≤1.6 mA from ±12 V through 7.5 kΩ, fine
  for the THS4521's clamps. The Seed inputs have only 100 Ω, so ESD and
  over-voltage protection belongs on the jack board.

### MIDI moves to a daughterboard
Drawn 2026-10-07 (`midi.kicad_sch`). Still TRS Type A on USART1 (`MIDI_TX`
pin 14, `MIDI_RX` pin 15), but the two 3.5 mm jacks move to a small
daughterboard, like the audio jacks. Two changes from Rev A:
* **TLP2361 (C107626) replaces the 6N138.** The 6N138 is DIP-only at JLC with
  next to no stock. The TLP2361 runs on `+3V3_D` and has a totem-pole output, so
  Rev A's 470 Ω pull-up goes. Its logic is inverting (LED on → low), the same
  sense as the 6N138's. KiCad has no symbol for it; the sheet uses
  `Isolator:TLP2310`, which has the same pinout and footprint but is drawn as
  non-inverting.
* **The MIDI IN sleeve is not connected**, as the MIDI spec requires. Rev A
  grounded it, which closes a ground loop through the cable (another suspect
  for Issue 4). The MIDI OUT sleeve is grounded.

IN: ring → 220 Ω → opto anode, tip → cathode, 1N4148W reverse across the LED.
OUT: ring → 100 Ω → `+3V3_D`, tip → 100 Ω → `MIDI_TX`. Rev A's 100 Ω values are
kept rather than the spec's 3.3 V values (33 Ω/10 Ω): they work with the gear in
hand and hold a tip-to-sleeve short while plugging to ≈33 mA from the Seed pin.

**Connector (J6): JST SH 5-pin vertical, BM05B-SRSS-TB (C160391)**, matching
the other satellites. MIDI's ≈5 mA opto-isolated loops don't need the audio
ribbon's interleaved grounds, and shrouded IDC starts at 10 pins.

| Pin | Signal |
|---|---|
| 1 | GND (the OUT jack's sleeve) |
| 2 | `MIDI_IN_R` |
| 3 | `MIDI_IN_T` |
| 4 | `MIDI_OUT_R` |
| 5 | `MIDI_OUT_T` |

### Power
Drawn on the power sheet and reviewed against every part's datasheet, 2026-10-06.
**Reworked 2026-10-08: the input jacks, the power switch and the status LEDs moved
to cabled daughterboards**, so each build picks its input (USB-C, barrel jack, both)
and puts it where its panel wants it. The motherboard keeps one 5 V input, the
battery, the charger and everything downstream. Anything big (LED strips and the
like) is powered by the daughterboard directly, never through the motherboard.

```
Power board (J2) ─ VIN 5 V ─ U2 BQ24073 ─ BATT_VCC ─ U5 TPS63070 ─ +5V_SYS
  + USB D+/D− to Seed     charger +                buck-boost,
  + PGOOD/CHG LEDs        power path               EN ← switch (J3)
                                │ BAT ─ U4 MAX17048 (I²C 0x36)
1S Li-ion (J1) ─ U3 DW01A + Q1 FS8205A ──┘

+5V_SYS ─┬─ U1 TPS563208 buck ─────────► +3V3_D   ─ FB4 ─► +3V3_SAT (satellite chain)
         ├─ FB2 ─ U6 TPS7A2033 LDO ────► +3V3_A   (I/O connectors, SPI header pin 7)
         ├─ FB3 + 10 µF + 100 µF ──────► SEED_VIN (Seed pin 39)
         └─ FB1 + 10 µF + 100 µF ──────► +5V_A    (codec, front end, jack board)
```

**Power input (J2): JST PH 8-pin, side entry, S8B-PH-K-S (C157915)**

| Pin | Signal | Motherboard |
|---|---|---|
| 1, 6 | `VIN` | U2 IN, input caps, TVS |
| 2, 5 | GND | GND |
| 3 | `USB_D−` | Seed pin 36 (`USB_OTG_HS_N`) |
| 4 | `USB_D+` | Seed pin 37 (`USB_OTG_HS_P`) |
| 7 | `~PGOOD` | U2 pin 7, open-drain, sinks ≤15 mA |
| 8 | `~CHG` | U2 pin 9, open-drain, sinks ≤15 mA |

**The daughterboard contract:**
* Supply **one regulated 5 V on `VIN`: 4.5–5.5 V, up to 1.5 A.** The charger
  limits input to ≈1.36 A and starts backing off at 4.5 V, so use 24–26 AWG wire.
  Power and ground each have two pins (PH is 2 A per pin).
* **USB data is optional.** A USB-C board wires D+/D− through; a barrel-only board
  leaves pins 3–4 unconnected. Full speed (12 Mbit/s) only, so a short internal
  cable (under ≈30 cm) with the pair adjacent is fine; the pair sits between the
  two grounds.
* **The USB-C receptacle's own parts live on the daughterboard**, at the jack: the
  5.1 kΩ CC pull-downs and a USBLC6-2SC6 on D+/D−.
* **LEDs (optional):** each LED's anode through its resistor to the board's own
  `VIN`, cathode to pin 7 (power good, green) or pin 8 (charging, red). Both only
  light with an input present, so the board's `VIN` is the right supply. Aim for
  2–5 mA. With no input (on battery) neither lights; a running indicator on
  battery has to come from firmware.
* **A board with two inputs** (USB-C and barrel, say) carries its own source
  selection, e.g. the TPS2121 priority mux the motherboard used to have.
* Reference boards, designed 2026-10-12:
  [USB-C only](../../rodeo-power-usbc/README.md) (the default build) and
  [USB-C + barrel jack](../../rodeo-power-barrel/README.md) (9–12 V adapter,
  barrel first through a TPS2121, plus a 3 A 5 V output for LED strips).

**Power switch (J3): JST SH 2-pin, BM02B-SRSS-TB (C160388).** Any latching panel
switch on two wires: toggle, rocker or push button.
* Pin 1: `BATT_VCC` through R12 (10 kΩ), so a pinched cable can't short the
  battery rail. Pin 2: U5's EN, with R13 (47 kΩ) to GND and 10 nF to GND.
* R13 sets the current through the switch contacts when on: 50–75 µA over the
  cell's range, with EN still at 2.5 V or more. It was 1 MΩ until 2026-10-16,
  which left 3–4 µA, below what silver-contact switches are made to switch
  reliably. The reference switch is a TK-6580A-1 (C528770), silver-plated.
* Unplugged means off. The cell still charges with the switch off.

**Charger and battery**
* **U2, BQ24073** (charger with power path). The system runs from the input and
  the cell charges from what's left. It runs with no battery or a dead one, and
  the cell tops up the system when the input can't keep up. VIN-DPM backs off
  at 4.5 V, so a weak laptop port doesn't crash. ILIM 1.18 kΩ sets the input
  limit to ≈1.36 A (fits a 1.5 A USB-C port). ISET 1.13 kΩ sets 0.79 A charge.
  TS has 10 kΩ (no thermistor in typical packs). TMR is left open (default
  safety timers) and TD is grounded. PGOOD and CHG go to the power board's LEDs.
* **Input protection:** an SMBJ5.0A TVS on `VIN` at J2, for hot-plug transients
  on the cable. The charger itself withstands 28 V and cuts off above 6.6 V.
* **Battery connector (J1):** JST PH 2-pin, side entry. Hobby packs disagree on
  PH polarity, so the silkscreen must mark +.
* **Protection:** DW01A (U3) + FS8205A (Q1) in the cell's negative lead. Trips at
  4.30 V overcharge, 2.50 V overdischarge, and 150 mV across the FETs (≈3–3.75 A).
  **Board ground is the cell's negative after the FETs (`BATT_N` → S1–S2 →
  `GND`);** `BATT_N` touches only J1, U3, Q1 and C10.
* **Fuel gauge:** MAX17048 (U4) on the I²C bus at 0x36, CELL/VDD on `BATT_P`.
* **No reverse-polarity protection.** A high-side PMOS doesn't protect a
  low-side DW01A. Rely on the keyed connector and the silkscreen.

**+5V_SYS: U5, TPS63070 buck-boost**
* 2.0–16 V in, 2 A out, forced PWM at 2.4 MHz (PS/SYNC grounded), so 5 V holds
  from a sagging laptop port and from a 3.0 V cell alike. FB divider 100 k/19.1 k
  gives 4.99 V. L2 Sunlord MWSA0518S-1R5MT (1.5 µH, 8 A sat). Caps per TI's
  reference BOM: 3 × 10 µF in, 10 µF + 3 × 22 µF out, 100 nF on VAUX.
* EN comes from the power switch (J3, above).

**Rails below +5V_SYS**
* **`+3V3_D`, U1 TPS563208:** forced-continuous 580 kHz buck. L1 MWSA0518S-2R2MT
  (2.2 µH), 33.2 k/10 k divider (3.32 V), 2 × 22 µF out. Feeds satellites (via
  `+3V3_SAT`), hubs, OLED, I²C taps, the second codec's VD/VL, and the SPI header.
* **`+3V3_A`, U6 TPS7A2033:** 7 µV RMS, 95 dB PSRR at 1 kHz, fed from +5V_SYS
  through FB2 (BLM18KG601SN1D) with 1 + 10 µF either side. Supplies the generic I/O
  connectors (pots) and the SPI header's pin 7 (the ADC expander's analog supply).
  Replaces Rev A's U2 TLV76733.
* **`SEED_VIN`, `+5V_A`, `+3V3_SAT`:** each a BLM21PG221SN1D ferrite (FB3, FB1,
  FB4) feeding 10 µF ceramic + 100 µF electrolytic. The electrolytic's ESR damps
  the ferrite/ceramic resonance; don't swap it for a low-ESR part. `SEED_VIN`'s caps
  sit at Seed pin 39. `+5V_A` is filtered but not re-regulated.

**Budget.** Design to a 1.5 A USB-C port (7.5 W). Rodeo itself is ≈2 W, which
leaves ≈1 A at 5 V for LEDs and taps: ~15–20 LEDs at full white, many more
dimmed. On battery, keep +5V_SYS under ≈1.2 A or the DW01A's overcurrent trip
gets close. A 500 mA USB-A port runs Rodeo with LEDs dark; there are no spare
pins for detecting the port's current, so firmware should default to dim LEDs.
Runtime is roughly 3 h on 2000 mAh, 5 h on a 3400 mAh 18650.

Every part has an `LCSC Part` field. All are stocked at JLC as of 2026-10-08.

### Generic I/O connectors
Decided 2026-10-08, replacing a dedicated CV header. Every leftover Seed pin
(`DIG_1`–`DIG_8` and both DACs) gets the same **3-pin JST SH** (BM03B-SRSS-TB,
C160389), in the pinout `rodeo-potentiometer` and `rodeo-rotary-switch` already
use, so those boards plug straight into any analog-capable connector:

| Pin | Signal |
|---|---|
| 1 | GND |
| 2 | signal, through 1 kΩ at the connector |
| 3 | `+3V3_A` |

| Silkscreen | Net | Seed pin | Use |
|---|---|---|---|
| `D1` | `DIG_1` | 8 | digital only |
| `A1`–`A7` | `DIG_2`–`DIG_8` | 22–28 | digital or analog in |
| `CV1`, `CV2` | `CV_DAC1`, `CV_DAC2` | 30, 29 | DAC out (0–3.3 V) or analog in |

* **Supply `+3V3_A`**, the quiet LDO rail (U6, TPS7A2033), which otherwise only
  feeds SPI header pin 7. Ten 10 kΩ pots draw ≈3 mA of its 300 mA; a shorted
  panel cable hits the LDO's current limit and takes down only these connectors.
* **1 kΩ series** protects the pins, two of which (PB1, PA7) aren't 5 V
  tolerant. It doesn't affect internal pull-ups, pot readings or a DAC driving
  an op-amp input.
* **No 5 V on these connectors.** A 4-pin version would be the I²C taps'
  connector, and an encoder hub plugged into it would take `+5V_A` on its SCL.
* **CV outputs:** the DACs only reach 0–3.3 V, so useful CV ranges need an op-amp
  stage. It belongs on the jack board, powered from the audio ribbon's `+5V_A`
  (J4 pin 10), with `CV1`/`CV2` reaching it on their I/O connectors.
* 2-pin `rodeo-button` boards no longer plug in directly; buttons move to the
  satellite chain.

### Removed
* **Status LED** (D5, R10). The Seed's own LED stays. Pin 28 becomes an I/O connector.
* **The two on-board ADS114S08s,** their 24 pot connectors, pull-downs and
  chip-select pull-ups. They become the optional
  [ADC expander](#adc-bank-moves-to-an-optional-spi-expander-board).

### Housekeeping
* **The Seed is socketed:** two 1×20 female headers, 2.54 mm, 8.5 mm tall
  (Kinghelm KH-2.54FH-1X20P-H8.5, C2905423), in A1's own holes. Add them as two
  BOM-only `Conn_01x20_Socket` symbols (excluded from the board) and hand-solder
  them with the Seed seated, so they line up.
* **Fix the schematic hierarchy.** The root sheet instantiates its subsheets,
  so ERC and "update PCB from schematic" work and the schematic becomes the
  source of truth again. *Closes hardware-map.md Issue 3.*

### Not happening
* **Power jumpers.** That was a miscommunication. The five power taps J45–J49
  as on Rev A are all that's needed. Their pin 3 was `VIN`; on Rev B it will be
  `+5V_SYS`, and pin 2 `+3V3_D` (not drawn yet).

---

## Layout notes

Gathered 2026-10-08 from the decisions above, so the PCB can be laid out from
one list. Where a note came from a section, the reason is there.

**Floorplan and ground**
* **Two ends:** power entry (power input J2, switch J3, battery J1), the
  charger and the switchers at one end; the Seed's audio pins, the CS4272, its
  front end and the audio ribbon J4 at the other (the "audio corner"). The Seed
  sits between them.
* **4 layers, as on Rev A** (Rev A: In1.Cu `GND`, In2.Cu `+3V3`). Keep **In1.Cu
  one unbroken `GND` plane** under everything, with no splits or slots. Return
  currents then stay under their own traces, which is what keeps switcher and
  digital noise out of the audio (hardware-map.md Issue 4). In2.Cu carries the
  power pours (Rev B has several rails, so it's zoned rather than one `+3V3`
  plane).
* **Route audio and fast signals on F.Cu**, directly over the In1 ground: the
  codec's analog nets, SAI2 clocks, SD, USB and the satellite chain. Signals on
  B.Cu reference the In2 power pours, so keep those to slow or short runs.
* **`GNDA`** (Seed pin 20) joins `GND` through R14 (0 Ω) right at the Seed, not
  elsewhere.
* **J4's grounds** (all ten odd pins) go straight into the plane at J4. That is
  the jack board's single ground point; nothing else from the jack board
  returns anywhere else.

**Power**
* **U2 BQ24073** dissipates 1–1.4 W while charging and running: solid ground
  pour and thermal vias under its pad.
* **U5 TPS63070 and U1 TPS563208:** smallest possible switch-node copper, input
  caps at the pins, inductors (L1, L2) right next to them, and keep both away
  from the audio corner.
* **Ferrite branches** (FB1–FB4) with their caps at the point of use: FB3 and its
  100 µF (C21) near the Seed's VIN pin, FB1's `+5V_A` caps near the CS4272 and
  op-amps, FB4 near J18.
* **Q1/U3 (battery protection):** `BATT_N` touches only J1, U3, Q1 and C10. Keep
  it short and off the ground plane; board ground starts after Q1.

**Seed and digital**
* **Seed socket:** two 1×20 female headers in A1's holes; the Seed then sits
  ≈8.5 mm plus its own height above the board. Check enclosure clearance.
* **Series resistors go at the Seed (the driving end), not the connector:**
  R80 (`SAT_TX`, 33 Ω), R73/R72 (SPI MOSI/SCK, 33 Ω), R59 (SD CLK, 22 Ω).
* **SD socket (J5)** at a board edge reachable through the case, close to Seed
  pins 5–7, with short, direct CLK/CMD/D0 traces. U11 (ESD) at the socket.
* **USB (J2 pins 3/4 → Seed pins 36/37):** route D+/D− as a 90 Ω differential
  pair, short. The ESD part is on the power board, at its jack.
* **CS4272 clocks and data** (Seed pins 31–35) short and away from the codec's
  VCOM, FILT+ and analog pins.

**Audio**
* **CS4272 (U7):** VCOM and FILT+ caps at their pins, small value nearest
  (datasheet §5.7); VA/VD/VL decoupling at each pin.
* **C68, C69 (2.7 nF)** right at the CS4272's AIN pins.
* **THS4521s (U9, U10):** 150 pF feedback caps and the VOCM cap tight to the
  chip; inputs and outputs symmetric (differential).
* **Output filters (U8 OPA2376):** keep each channel's Fig. 14 network compact;
  its decoupling at the chip.
* **Mute shunts Q3/Q4** right at R36/R39, R37/R40 and J4, so they short the
  connector node.
* **Seed audio pins 16–19** to J4 and R56/R57 by short routes, over solid ground.

**Connectors and protection**
* **ESD at the connector it protects:** U13 at J18 (flow-through: the signal runs
  through pins 1→6 and 3→4), U11 at J5. The SMBJ5.0A TVS on `VIN` at J2.
* **1 kΩ I/O resistors** (R67–R71, R74–R78) at their connectors (J8–J17).
* **F1 (PTC)** between FB4's caps and J18.
* **Silkscreen:**
  * I/O connectors: `D1`, `A1`–`A7`, `CV1` (J17), `CV2` (J16).
  * Label J18 (satellite chain) and J6 (MIDI) clearly: both are 5-pin JST SH.
  * Battery J1: mark +. Hobby packs disagree on PH polarity.
  * Pin 1 / key on J4 (IDC) and every JST.
* **J2 (power input)** and **J3 (switch)** go where cables reach the panel's
  power board and switch; neither needs a board edge.

## Open — audio and power

These hang together through the grounding question. The bench tests in
[`hardware-map.md` Issue 4](../../../rev-a/rodeo-motherboard/docs/hardware-map.md) decide how much of it is needed,
so they come first.

* **The hum on the line out that grows with each held key** (hardware-map.md
  Issue 4). The power design now covers three of the candidate fixes: bulk at
  the Seed's VIN (C21 100 µF), a ferrite between the supply and VIN (FB3), and a
  clean-supply option (the battery takes the USB host's ground out of the path).
  Still open: a dedicated ground return for the J4/J5 sleeves, and a series R on
  J5. Running Rev B from the battery against USB is now a useful A/B test.
* **Filtering on audio I/O.** The second codec's outputs now have 560 Ω series
  R; the Seed's outputs still go to the jacks direct. ESD protection on every
  jack contact belongs on the jack board (see
  [Audio interface](#audio-interface-and-jack-board)).
* **Headphone output with volume control**, probably as a daughterboard. Which
  amp, and whether volume is an analog pot after the amp (0 pins) or software.
  Optionally 1 pin for mute (pop suppression) and 1 for headphone detect, which
  would come out of the generic I/O connectors. Its supply is `+5V_A`.
* **1/4" vs 1/8" jacks on the jack board.** Decided 2026-10-14: two variants
  of one circuit, four stereo TRS jacks each (Seed out, codec out, Seed in,
  codec in), with ESD diodes on every contact and series R plus clamp on every
  input. The 3.5 mm board is designed in
  [`rodeo-audio-3.5mm`](../../rodeo-audio-3.5mm/README.md); the 1/4" board
  follows with hand-soldered jacks, since JLC stocks no right-angle 1/4" PCB
  jacks. All eight signals are single-ended, so a 1/4" TRS jack is not balanced.
* **Seed input impedance** is not measured yet. It decides how much the 100 Ω
  series resistors and any jack-board protection load the Seed's inputs.

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

* **SD over USB:** USB mass-storage device class on the SD card (composite with
  USB-MIDI and serial), so samples load without opening the case. See
  [SD card slot](#sd-card-slot-native-mode-1-bit).
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
* **The encoder hub doesn't carry over to Rev B** (decided 2026-10-18): encoder banks on
  the satellite chain replace it, and the hub stays a Rev A part.
