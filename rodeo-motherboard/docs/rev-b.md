# Rodeo Motherboard — Rev B

**Status: planning.** Brainstormed with Derek on 2026-10-04. Nothing here is laid
out yet. The board in hand is Rev A, described in [`hardware-map.md`](hardware-map.md).
Later the same day, the hybrid satellite chain replaced the third ADC (see
[Satellite chain](#satellite-chain-on-usart2-hybrid-analog)).

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

| Seed pin | STM32 | Rev A | **Rev B** |
|---|---|---|---|
| 1 | PB12 | unconnected | **J43 SPI chip-select** (10 kΩ pull-up) |
| 2–7 | PC11, PC10, PC9, PC8, PD2, PC12 | `DIG_1`–`DIG_6` | **SD card, native 4-bit SDMMC** |
| 8 | PG10 | unconnected | **digital jack** |
| 9–11 | PG11, PB4, PB5 | SPI1 (ADCs) | SPI1, shared bus (unchanged) |
| 12–13 | PB8, PB9 | I²C1 | I²C1, 4–6 taps, pull-ups on the motherboard |
| 14–15 | PB6, PB7 | MIDI (USART1) | MIDI (unchanged) |
| 16–19 | — | audio in/out | audio in/out (see open items) |
| 22 | PC0 | `DIG_7` | digital jack |
| 23 | PA3 | `DIG_8` | **satellite chain RX** (USART2) |
| 24–27 | PB1, PA7, PA6, PC1 | `DIG_9`–`DIG_12` | digital jacks |
| 28 | PC4 | status LED | **digital jack** (LED removed) |
| 29–30 | PA5, PA4 | unconnected | **CV header** (DAC 2, DAC 1) |
| 31 | PA1 | unconnected | **digital jack** |
| 32–33 | PA0, PD11 | ADC A/B chip-selects | ADC A/B chip-selects, **now with pull-ups** |
| 34 | PG9 | unconnected | **digital jack** |
| 35 | PA2 | unconnected | **satellite chain TX** (USART2) |
| 36–37 | PB14, PB15 | USB D−/D+, dead-end | **USB data to J1** |

**Digital jacks: 9** (Rev A had 12). They are pins 8, 22, 24–28, 31 and 34.
All of them except pins 8 (PG10) and 34 (PG9) are ADC-capable. Jack numbering
is left to layout. Buttons are expected to move onto the satellite chain, so
fewer jacks are needed than before.

---

## Decided

### Chip-select pull-ups on every SPI device
10 kΩ to +3V3 on all three chip-selects: ADC A (pin 32), ADC B (pin 33)
and J43 (pin 1). Without them a chip-select floats during boot, reset
or reflashing, which can enable a chip and make two devices drive the shared
data line. Firmware parks them high (`611f48a`), but only once it is running.
*From: RODEO_CONTROL_MAP.md Issue 6.*

### One SPI bus, made shareable (no second bus)
Two ADCs use under 2% of SPI1. Each scan step is ~1.16 ms of conversion at
1000 SPS, and the bus only carries ~10 µs of traffic per chip in that time. The
ADS114S08 stops driving `DOUT` while its chip-select is high. On Rev A, what
stopped anything else sharing the bus was hardware: the chip-selects floated,
and **J43 had no chip-select pin at all** (Seed pin 8 dead-ended).

Rev B fixes both:
* J43 gets a chip-select from pin 1, so an off-the-shelf SPI OLED connects with
  a plain cable. J43 becomes power, ground, clock, both data lines and
  chip-select.
* The pull-ups above.

The firmware side, for later: the bus hand-off reconfigures SPI mode on every
switch (the ADCs need mode 1, most displays mode 0 or 3), and OLED frames go out
in chunks that fit the ADC scan's idle gaps. A full frame is ~1 KB, which is
longer than one scan step at the current clock.

**To verify on the bench:** a long or noisy cable on J43 adds load to the ADC
bus. The 33 Ω series resistors and the slow clock should cover it.

### Satellite chain on USART2 (hybrid analog)
Decided 2026-10-04. The two ADCs and their 24 pot connectors stay. Anything
beyond them, and every button, goes on a **daisy-chain of smart satellites**
instead of more motherboard channels. Each satellite carries a ~$0.10 MCU that
reads its control against its own supply, which is also its ADC reference, and
passes data along a UART ring. A satellite's position in the chain is its
address. Growth means adding satellites, not respinning the motherboard. The
frame format gets its own contract doc next to
[`RODEO_HUB_I2C_CONTRACT.md`](RODEO_HUB_I2C_CONTRACT.md). It is not written yet.

**Board side**
* **USART2: TX on pin 35 (PA2), RX on pin 23 (PA3), alternate function AF7.**
  Checked 2026-10-04 against libDaisy's `src/per/uart.cpp` pin table and ST's
  pin database (via Zephyr's generated `stm32h750` pinctrl). Both list exactly
  those pins. libDaisy's `seed::D28`/`seed::D16` are PA2/PA3, which matches
  this pin map. Nothing on the module uses them. libDaisy only touches them
  for SAI2 (rejected below) or when they are set up as ADC inputs.
* One connector: chain 3V3, GND, TX and RX. Its pinout, and how the ring
  closes, come with the contract doc. The ring closes either with a return
  cable from the last satellite or with a return wire through every satellite
  plus a terminator plug.
* A 33–100 Ω series resistor on TX for the cable, and ESD protection on both
  lines.
* **The chain gets its own filtered 3V3 feed:** a ferrite and bulk cap off U2,
  or its own regulator. U2's rail is the ADC reference and every analog pot's
  supply, and 20–30 satellite MCUs switching on it would show up as pot jitter
  on the analog channels. Budget ~3 mA per satellite, about 100 mA for 30. U2
  is rated 1 A. Size the filter once the satellite MCU is chosen.

**Firmware side: libDaisy cannot drive this as it stands**
* libDaisy's `UartHandler` uses one fixed RX DMA stream (`DMA1_Stream5`) and
  one TX stream (`DMA2_Stream4`) for every UART. MIDI's `MidiUartHandler` keeps
  the RX stream running permanently in circular listen mode, so starting a DMA
  listen on USART2 would take the stream away from MIDI. Interrupt-only receive
  is not viable at ~1 Mbaud.
* libDaisy also defines `USART2_IRQHandler` itself, so a Rodeo driver cannot
  own that interrupt without a duplicate-symbol link error.
* **Plan:** write a small USART2 driver in `rodeo-sdk`, built on the HAL and on
  DMA streams libDaisy leaves free: `DMA1_Stream7` for RX and `DMA2_Stream5` for
  TX. `DMA2_Stream6`/`7` are also free; `DMA2_Stream0`/`1` belong to the DAC,
  which the CV header uses. Ring frames are a fixed length, so RX can be one
  normal (non-circular) DMA transfer of exactly a frame, finished by the DMA
  stream's own interrupt. The USART interrupt is never needed. The fallback is
  to patch the libDaisy submodule so each UART gets its own streams, which is a
  TODO in libDaisy already ("dynamic DMA Stream acquisition").
* **Bench testing on Rev A:** pin 23 is already `DIG_8`, but pin 35 is
  unconnected, so it needs a bodge wire. The easier option is UART4 on
  `DIG_2`/`DIG_1` (PC10 TX, PC11 RX), which libDaisy also supports.

### 1 MΩ pull-downs on all 24 ADC inputs
Any channel can be unused on a given build, and a floating input reads noise.
With a 10 K satellite the loading error is under 0.3% at worst (mid-travel, zero
at the ends), well inside the pot's own tolerance.
*Replaces RODEO_CONTROL_MAP.md's rodeo-001-specific "tie AIN0/AIN3 to GND".*

### I²C: pull-ups move to the motherboard, 4–6 taps
* Pull-ups go on the motherboard, so the bus works with no hub attached. The
  Rev B encoder hub drops its own 2× 2.2 kΩ. **Hub and motherboard revisions
  must match:** a Rev A hub on a Rev B motherboard puts ~1.1 kΩ on the bus,
  which is at the limit. The value gets sized once the tap count and cable
  lengths are settled.
* 4–6 tap connectors on the one bus. No pins needed.
* No multiplexer footprint. There are only four hub addresses (0x30–0x33) plus
  the OLED (0x3C), so nothing collides. An off-the-shelf Qwiic multiplexer can
  plug into a tap if that ever changes.

*From: RODEO_CONTROL_MAP.md (I²C pull-ups) and RODEO_HUB_I2C_CONTRACT.md.*

### SD card slot (native mode)
For samples and wavetables, so it uses the Seed's 4-bit SDMMC interface, which
is fixed to pins 2–7.
* The slot goes **on the motherboard**, close to pins 2–7, at a board edge
  reachable through the case. 25–50 MHz SDMMC does not tolerate a ribbon to a
  daughterboard well.
* Pull-ups on CMD and DAT0–3 as the SD spec requires, plus ESD protection.
* **No card-detect switch.** It would cost a digital jack, and the bootloader
  and firmware check for a card when they need one.
* libDaisy has SDMMC and FatFS built in. Samples load into the Seed's external
  SDRAM.
* **Bonus:** the Daisy bootloader also updates firmware from an SD card at
  boot, and that needs all four data lines.

**To verify:** that the Seed3 has the same 64 MB SDRAM as the classic Seed.

### USB data on the panel USB-C, including firmware flashing
Wire J1's D+/D− to Seed pins 36/37, with ESD protection on the data lines. The
CC pull-downs (R6/R8) stay as they are.
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

### CV header, pins 29/30 reserved
The Seed's only DACs. They go to a small header (DAC 1, DAC 2, GND, +3V3, VIN)
for a future CV daughterboard. The DACs only output 0–3.3 V, so useful CV ranges
need an op-amp stage, and that belongs on the daughterboard. Both pins can also
read analog voltages, so the same header can take CV inputs.

### Removed
* **Status LED** (D5, R10). The Seed's own LED stays. Pin 28 becomes a jack.

### Housekeeping
* **Fix the schematic hierarchy.** The root sheet instantiates its subsheets,
  so ERC and "update PCB from schematic" work and the schematic becomes the
  source of truth again. *Closes hardware-map.md Issue 3.*

### Not happening
* **Power jumpers.** That was a miscommunication. The five power taps J45–J49
  as on Rev A are all that's needed.

---

## Open — audio and power

These hang together through the grounding question. The bench tests in
[`hardware-map.md` Issue 4](hardware-map.md) decide how much of it is needed,
so they come first.

* **The hum on the line out that grows with each held key** (hardware-map.md
  Issue 4). Candidate fixes from there: bulk capacitance at the Seed's VIN/GND,
  a dedicated ground return for the J4/J5 sleeves, a series R on J5, a ferrite
  or LC between VBUS and VIN, a clean-supply input option.
* **More power filtering.** The same items as above, plus filtering ahead of the
  3.3 V regulator U2. That rail is both the ADC reference and every pot's
  supply, so its noise shows up as pot jitter. The satellite chain's separate
  filtered feed belongs to the same design pass. Chained controls read against
  their own supply, so this jitter only affects the 24 analog channels.
* **Filtering on audio I/O.** An RC on the outputs (J5 has no series R today),
  ESD protection on every jack contact, and a check of the input against hot
  line-level sources.
* **Headphone output with volume control**, probably as a daughterboard. Which
  amp, and whether volume is an analog pot after the amp (0 pins) or software.
  Optionally 1 pin for mute (pop suppression) and 1 for headphone detect, which
  would come out of the 10 digital jacks.
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
| Encoder hub as the satellite-ring master | Costs no Seed pins, but adds a hop, puts every reading through I²C (~60 bytes a scan) and changes the hub firmware. USART2 on the Seed is direct and DMA-capable. |
| Second SPI bus (SPI6 on pins 29/30 + `DIG_10`/`DIG_11`) | SPI6 is the only one that reaches the header (SPI2's clock isn't broken out, SPI3 sits on the SDMMC pins). It would take the DAC pins and 1–2 jacks, and libDaisy has no DMA for SPI6. SPI devices are uncommon in DIY synths, and the OLED is the likely one, which a shared SPI1 serves. |
| SD card over SPI | Too slow for samples and wavetables, and libDaisy's FatFS path is SDMMC. |
| Second hardware I²C bus | Per the STM32H750 datasheet (from memory, not re-checked), the only other I²C peripheral that reaches the header is on the MIDI pins. Several taps on one bus covers the need. |
| Multichannel audio (SAI2) | Not a goal. It would need pins 31–35, including 32/33 back from the ADC chip-selects. |
| SD card-detect switch | Costs a jack, see above. |

---

## Firmware consequences (when Rev B exists)

* `rodeo-sdk/common/board.h`: new digital-jack map (9 jacks), J43 chip-select
  on `seed::D0` (pin 1), satellite chain on `seed::D28` (TX) / `seed::D16` (RX),
  CV pins.
* `RodeoAdc`: still two chips and 24 channels. Park all three chip-selects high
  before probing (the pull-ups make it belt and braces).
* New: the satellite-chain driver (USART2 on `DMA1_Stream7`/`DMA2_Stream5`, see
  [Satellite chain](#satellite-chain-on-usart2-hybrid-analog)) and the ring
  protocol, plus the satellite firmware itself.
* SPI1 hand-off between the ADC scanner and a J43 device.
* Bootloader: `extdfu` build. App makefiles keep `BOOT_SRAM`.
* The hub firmware is unaffected. The Rev B hub only loses two resistors.
