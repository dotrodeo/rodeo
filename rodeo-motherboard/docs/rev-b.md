# Rodeo Motherboard — Rev B

**Status: schematic in progress.** Brainstormed with Derek on 2026-10-04. Later
the same day, the hybrid satellite chain replaced the third ADC (see
[Satellite chain](#satellite-chain-on-usart2-hybrid-analog)). The power sheet was
drawn and reviewed on 2026-10-05/06 (see [Power](#power)). Everything else is
still a plan. The board in hand is Rev A, described in
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
* **The chain gets its own filtered 3V3 feed, `+3V3_SAT`:** FB4 (BLM21PG221SN1D)
  plus 10 µF and 100 µF electrolytic off `+3V3_D`. The ADC reference and the
  pots are on a separate rail, `+3V3_A` (see [Power](#power)), so 20–30
  satellite MCUs switching can't show up as pot jitter. Budget ~3 mA per
  satellite, about 100 mA for 30.

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
* No multiplexer footprint. There are only four hub addresses (0x30–0x33), the
  OLED (0x3C) and the MAX17048 fuel gauge (0x36), so nothing collides. An
  off-the-shelf Qwiic multiplexer can plug into a tap if that ever changes.

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
         ├─ FB1 ─ U8 TPS7A2033 LDO ────► +3V3_A   (ADC AVDD/REFP0 + the 24 pots only)
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
  `+3V3_SAT`), hubs, OLED, I²C taps, power taps, and the ADCs' DVDD/IOVDD.
* **`+3V3_A`, U8 TPS7A2033:** 7 µV RMS, 95 dB PSRR at 1 kHz, fed from +5V_SYS
  through FB1 (BLM18KG601SN1D) with 1 + 10 µF either side. Feeds only the ADCs'
  AVDD and REFP0 and the 24 pots (≈10 mA), so readings stay ratiometric.
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
and LED connectors at one end, and the Seed audio and ADCs at the other.

Every part has an `LCSC Part` field. All are stocked at JLC as of 2026-10-06.

### CV header, pins 29/30 reserved
The Seed's only DACs. They go to a small header (DAC 1, DAC 2, GND, `+3V3_D`, `+5V_A`)
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
  line-level sources.
* **Headphone output with volume control**, probably as a daughterboard. Which
  amp, and whether volume is an analog pot after the amp (0 pins) or software.
  Optionally 1 pin for mute (pop suppression) and 1 for headphone detect, which
  would come out of the 9 digital jacks. Its supply is `+5V_A`.
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
| USB-PD input (HUSB238 + 9–12 V) with 5 V and 3.3 V rails at 3 A each | 25 W needs a 30 W+ PD charger and leaves a laptop unable to run the board. LEDs were the only reason for 3 A, and they go on the power daughterboard. The HUSB238 also runs BC1.2 detection on D+/D−, which would fight the Seed's USB link. |
| Separate LED rail (TPS55288, firmware-enabled, current-limited) | Only needed with the PD design. Its output also stays off until I²C enables it, so it can't power the Seed. |
| Power daughterboard carrying the battery | Battery is wanted on the motherboard. The daughterboard is now only a 5 V barrel-jack supply. |
| Alps SPUJ190900 power switch | Right type and quality, but 7.5 × 15.2 × 8.8 mm. The TK-6580A-1 does the job in a quarter of the area. |
| Slide switch for power | Smallest and cheapest, but fiddly through a panel slot. Fallback if the panel turns out too thick for the TK-6580's reach. |
| Reverse-polarity PMOS on the battery | Doesn't protect a DW01A in the negative lead. |

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
* New: a MAX17048 driver (I²C 0x36) for battery % and a save-state-and-dim
  warning before the cell runs out.
* LED brightness defaults low, since the board can't tell a 500 mA USB-A port
  from a 1.5 A one. The charger's PGOOD/CHG and the mux's ST don't reach the
  Seed.
* The hub firmware is unaffected. The Rev B hub only loses two resistors.
