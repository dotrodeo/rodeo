# Rodeo Motherboard — Rev B

**Status: planning.** Brainstormed with Derek on 2026-10-04. Nothing here is laid
out yet. The board in hand is Rev A, described in [`hardware-map.md`](hardware-map.md).

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
| 8 | PG10 | unconnected | **U5 (3rd ADC) chip-select** (10 kΩ pull-up) |
| 9–11 | PG11, PB4, PB5 | SPI1 (ADCs) | SPI1, shared bus (unchanged) |
| 12–13 | PB8, PB9 | I²C1 | I²C1, 4–6 taps, pull-ups on the motherboard |
| 14–15 | PB6, PB7 | MIDI (USART1) | MIDI (unchanged) |
| 16–19 | — | audio in/out | audio in/out (see open items) |
| 22–27 | PC0, PA3, PB1, PA7, PA6, PC1 | `DIG_7`–`DIG_12` | digital jacks |
| 28 | PC4 | status LED | **digital jack** (LED removed) |
| 29–30 | PA5, PA4 | unconnected | **CV header** (DAC 2, DAC 1) |
| 31 | PA1 | unconnected | **digital jack** |
| 32–33 | PA0, PD11 | ADC A/B chip-selects | ADC A/B chip-selects, **now with pull-ups** |
| 34–35 | PG9, PA2 | unconnected | **digital jacks** |
| 36–37 | PB14, PB15 | USB D−/D+, dead-end | **USB data to J1** |

**Digital jacks: 10** (Rev A had 12). They are pins 22–28, 31, 34 and 35. All
of them except pin 34 (PG9) are ADC-capable. Jack numbering is left to layout.

---

## Decided

### Chip-select pull-ups on every SPI device
10 kΩ to +3V3 on all four chip-selects: ADC A (pin 32), ADC B (pin 33),
U5 (pin 8) and J43 (pin 1). Without them a chip-select floats during boot, reset
or reflashing, which can enable a chip and make two devices drive the shared
data line. Firmware parks them high (`611f48a`), but only once it is running.
*From: RODEO_CONTROL_MAP.md Issue 6.*

### One SPI bus, made shareable (no second bus)
Three ADCs use about 3% of SPI1. Each scan step is ~1.16 ms of conversion at
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

### Third ADS114S08 (U5) for 36 analog channels
Same SPI1 bus with 33 Ω series resistors matching R13/R14/R16, chip-select on
pin 8 (PG10), 0.1 µF + 10 µF local decoupling, and the same reference and
`AINCOM` wiring as U3/U4. Twelve more pot connectors (proposed J50–J61). The
three chips convert in parallel, so the scan rate per channel barely changes.
*From: RODEO_CONTROL_MAP.md, "REV-B UPGRADE PATH — 36-channel expansion".*

### 1 MΩ pull-downs on all 36 ADC inputs
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
  supply, so its noise shows up as pot jitter.
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
| Second SPI bus (SPI6 on pins 29/30 + `DIG_10`/`DIG_11`) | SPI6 is the only one that reaches the header (SPI2's clock isn't broken out, SPI3 sits on the SDMMC pins). It would take the DAC pins and 1–2 jacks, and libDaisy has no DMA for SPI6. SPI devices are uncommon in DIY synths, and the OLED is the likely one, which a shared SPI1 serves. |
| SD card over SPI | Too slow for samples and wavetables, and libDaisy's FatFS path is SDMMC. |
| Second hardware I²C bus | Per the STM32H750 datasheet (from memory, not re-checked), the only other I²C peripheral that reaches the header is on the MIDI pins. Several taps on one bus covers the need. |
| Multichannel audio (SAI2) | Not a goal. It would need pins 31–35, including 32/33 back from the ADC chip-selects. |
| SD card-detect switch | Costs a jack, see above. |

---

## Firmware consequences (when Rev B exists)

* `rodeo-sdk/common/board.h`: new digital-jack map (10 jacks), U5 chip-select on
  `seed::D7` (pin 8), J43 chip-select on `seed::D0` (pin 1), CV pins.
* `RodeoAdc`: third chip and 36 channels. Park all four chip-selects high before
  probing (the pull-ups make it belt and braces).
* SPI1 hand-off between the ADC scanner and a J43 device.
* Bootloader: `extdfu` build. App makefiles keep `BOOT_SRAM`.
* The hub firmware is unaffected. The Rev B hub only loses two resistors.
