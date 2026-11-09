# rodeo-sdk

The firmware side of Rodeo: a starting point for instrument code, the drivers for
everything on the boards, and small standalone programs that validate one
subsystem each — flash one, confirm the hardware, move on. Built on
[libDaisy](https://github.com/electro-smith/libDaisy) and
[DaisySP](https://github.com/electro-smith/DaisySP), both vendored here.

Published documentation is in [`docs/`](docs/) and appears at
<https://rodeooh.rodeo/sdk/>. Everything in that folder is a page on the website,
so a change to the code wants a change there too — CI enforces it. Three kinds of
page live there:

* **Written pages** (`index`, `install`, `first-app`, `reference/*.mdx`): edit them
  directly.
* **The API reference** (`reference/api/`) is **generated** from the headers'
  comments by `tools/docs/gen_reference.py`. Change a header's comments, run
  `make docs`, commit both; CI runs `make docs-check` and fails if they disagree. A
  header joins the reference by being added to the script's `HEADERS` list.
* **Design drafts** (`guides/`, `draft: true`) describe APIs that aren't written
  yet. `astro dev` shows them and the published site leaves them out; when the
  code lands, bring the page in line with it and delete the `draft` line.

> **Mid-rewrite.** `v0.2.0` is the last release for the prototype boards, and the
> tree is being rewritten for the hardware that was ordered on 18 October 2026:
> a satellite chain in place of the I²C hubs, a second codec, a new pin map. The
> plan is in [`planning/REV_B_PLAN.md`](../planning/REV_B_PLAN.md). Instruments
> built before the line stay on the frozen copy at `rev-a/rodeo-sdk/`; rodeo-002
> moves onto the rewritten SDK first, once the boards are brought up.

> The previous contents (a 12-voice subtractive synth) are preserved on the git
> tag `archive/synth-m2`. Restore with `git checkout archive/synth-m2 -- firmware/src`.

## Layout

```
rodeo-sdk/
  common/
    board.h              # Rev B pin map — the single source of truth (host-safe)
    board_daisy.h        # board.h's pins as libDaisy Pins (target only)
    audio_4x4.{h,cpp}    # Rev B audio: both codecs, four in and four out, one callback
    battery.{h,cpp}      # the battery, through the MAX17048 fuel gauge (target only)
    max17048.h           # the gauge's registers and their decoding (host-safe)
    tinyusb.mk, usb_port.{h,cpp} # TinyUSB on either port: RODEO_USB=fs (micro-USB) or hs (Rev B)
    host.{h,cpp}, host_usb.h # the host protocol: parameters and chain to a computer over USB-MIDI
    version.h            # SDK version + RODEO_SDK_AT_LEAST(); the real changelog
    rodeo.mk             # shared build rules; every app/example includes this
    ads114s08.{h,cpp}    # per-chip ADS114S08 SPI ADC driver
    rodeo_adc.{h,cpp}    # the prototype's two-chip scanner: NOT YET PORTED to Rev B
    adc_convert.h        # pure value pipeline + PotSpan knob-travel calibration
    oled_ssd130x_bulk.{h,cpp} # faster SSD130x I2C transport (page-at-a-time + yield hook)
    pot_ui.h             # pure panel logic: stepped pots, "which knob is moving"
    rodeo_dsp.h          # pure DSP blocks: Smoother, OnePole, DcBlocker, Lfo, Rng
    rodeo_scope.h        # triggered, normalised waveform for a small display
    rodeo_screen.h       # 128x64 screen primitives: title bar, list rows, scroll bar
    tests/               # host-side GoogleTest: the pure-logic headers and board.h
  host-js/           # the host protocol's computer side in TypeScript (@dotrodeo/rodeo-host)
  template/          # copy-to-start skeleton for a new Rodeo app (blinks the Seed's LED)
  examples/
    blink_power/       # Rev B 01: boots on the motherboard; the cell from the fuel gauge
    audio_4x4/         # Rev B 02: all eight audio channels, a tone out and a meter in on each
    midi/              # Rev B 03: MIDI out to in through the jack board, checked in order
    io_connectors/     # Rev B 04: the ten I/O connectors, self-testing (or on a bare Seed)
    sat_chain/         # Rev B 05: the chain and every satellite's controls and LEDs
    oled_i2c/          # Rev B 06: I2C scan, the OLED, bus checks at 100 and 400 kHz
    sd_card/           # Rev B 07: the internal microSD in 1-bit mode, checked and timed
    usb_midi_cdc/      # Rev B 08: TinyUSB MIDI + serial (RODEO_USB=hs; fs on a bare Seed)
    usb_storage/       # Rev B 09: the SD card as a USB drive, in a disk mode
    usb_audio/         # Rev B 10: UAC1 4x4, 24-bit, both codecs, + MIDI and serial
    spi_header/        # Rev B 11: the expansion header's SPI as a loop-back
    usb_device/        # TinyUSB on a bare Seed: stereo audio, MIDI, serial, RAM-disk mode
    host/              # the host protocol: a drone the console shows and sets over USB-MIDI
  tools/
    screens/           # render an instrument's real OLED screens on your laptop
    docs/              # gen_reference.py: the API reference from the headers
  docs/                # the published pages: guides/ (drafts), reference/api/ (generated)
  libDaisy/  DaisySP/  tinyusb/ # vendored submodules
  Makefile           # top-level helpers: `make deps`, `make all`, `make docs`
```

The prototype's bring-up examples (`01_blink` to `10_audio_levels`) and its
encoder-hub and status-LED drivers left this tree with the Rev B pin map; they
live on in the frozen copy at `rev-a/rodeo-sdk/`. The Rev B bring-up set replaces
them, one test per board (`planning/REV_B_PLAN.md`, Milestone 1, numbered as there).

Each app/example is a self-contained folder: its own `src/main.cpp` and a tiny
`Makefile` that just sets `TARGET` + `CPP_SOURCES` and includes
`common/rodeo.mk`. `cd` into one and run `make`.

## Toolchain

- **ARM GNU Toolchain** (`arm-none-eabi-gcc`) **with newlib.** On macOS the
  Homebrew `arm-none-eabi-gcc` ships *without* newlib and fails with
  `stdint.h: No such file or directory`. Install the official
  [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
  and set `GCC_PATH` in `rodeo-sdk/config.mk` (gitignored, machine-specific;
  auto-included by every build):
  ```make
  GCC_PATH := /Applications/ArmGNUToolchain/14.3.rel1/arm-none-eabi/bin
  ```
- `dfu-util` for flashing; submodules: `git submodule update --init --recursive`.

## Build & flash

```sh
cd rodeo-sdk
make deps                      # build libDaisy + DaisySP static libs (first time)

cd examples/usb_device
make                           # -> build/usb_device.{elf,bin,hex}
make program-dfu               # flash over the USB-C, with the Seed in its bootloader
```

`make all` from `rodeo-sdk/` builds every example.

Apps run from SRAM under the Daisy bootloader (`APP_TYPE=BOOT_SRAM`, set in
`common/rodeo.mk`), and flash over the USB-C. To get into the bootloader, send `B` on
any example's serial port: it reboots and waits there until a flash arrives. A tap on
RESET also works, flashing within the bootloader's 2.5 s window (it pulses the LED).
The `dfu-util` "get_status" error after "Submitting leave request" is normal.

**Installing the bootloader**, once per Seed: hold BOOT, tap RESET, then
`make program-boot` from any example, over the Seed's micro-USB. It writes libDaisy's
`dsy_bootloader_v6_4-extdfu-2000ms.bin`, whose DFU answers on pins 36/37, the USB-C.
Then tap RESET: with a computer on the USB-C, the STM32's ROM bootloader never starts
what it has just written. The ROM also listens for a USART1 host on PB14/PB15, which on
Rodeo are the USB-C's D−/D+. With a host attached there, the ROM's leave never reaches
the program, not even its first instruction. Software resets are unaffected, and so is
flashing with the USB-C unplugged. The same goes for `APP_TYPE=BOOT_NONE` builds, which
replace the bootloader in internal flash.

**Memory under the bootloader.** The code fills AXI SRAM, and statics live in DTCM with
the stack. SDMMC1's DMA reaches neither DTCM nor SRAM1, so the SD card's FatFS objects
(`FatFSInterface`, `FIL`) and buffers go in the SDRAM with `DSY_SDRAM_BSS`, the buffers
32-byte aligned for the driver's cache maintenance (`examples/sd_card`). The other
DMA (UARTs, SPI, ADC, audio) uses SRAM1 through `DMA_BUFFER_MEM_SECTION`.

**The bootloader reads the SD card at every start**, Rev B's 1-bit card included. It
looks in the root for a `.bin` and, by its docs, installs any valid program it finds;
it logs failures to `daisy_boot_log.txt` on the card. Keep stray `.bin` files off it.

## Adding a new app or bring-up example

1. Copy `template/` to `examples/<NN_name>/` (two levels below `rodeo-sdk/`, so
   the default `RODEO_ROOT = ../..` resolves — delete the `RODEO_ROOT` line the
   template carries for its own shallower location).
2. Edit `TARGET` and `CPP_SOURCES` in its `Makefile`.
3. `#include "board.h"` for the pin map; write `src/main.cpp`.

## Versioning

Tagged `rodeo-sdk/vX.Y.Z` — component first, same convention as
`rodeo-002/vX.Y.Z`, so a tag listing groups by component rather than by number.
`common/version.h` carries the number and the changelog, and an app can refuse
to build against an SDK that is too old:

```cpp
static_assert(RODEO_SDK_AT_LEAST(0, 1, 0), "this app needs a newer SDK");
```

**0.x on purpose.** Two instruments in, the shape is clear but the seams are
not all settled — `version.h` names what is still unresolved and what stands
between this and a 1.0.

## Notes

- **The pure-logic headers are the point.** `adc_convert.h`, `pot_ui.h`,
  `rodeo_dsp.h`, `rodeo_scope.h` and `rodeo_screen.h` have no libDaisy and no
  hardware in them: they build on the host, `common/tests/` exercises them
  there, and the firmware calls *these* functions rather than a second
  implementation. Anything that is just maths and state belongs here.
- **Pot cables are straight-through.** Both the motherboard connector and the
  `rodeo-potentiometer` satellite are `1 = supply, 2 = signal, 3 = GND`; the
  satellites are not mirrored. See `docs/hardware-map.md` for the pinout and for
  what a reversed cable does to the travel.

- `board.h` is the pin/topology contract for the whole board (SPI/ADC, MIDI,
  audio, LED, 24-channel ADC surface). Keep it in sync with the KiCad source.
  It holds only *generic* board facts — instance-specific control assignments
  (which pot is cutoff, which encoder slot is waveform) live in the instrument
  build and in that instrument's own control map: `instruments/rodeo-001/docs/RODEO_CONTROL_MAP.md`,
  `instruments/rodeo-002/docs/RODEO_002_CONTROL_MAP.md`. **This library serves every
  instrument, not the first one** — if something here names a specific knob or a
  specific sound, it is in the wrong place.
- **External ADC (ADS114S08) status:** the SPI orientation and conversion path
  are bench-confirmed on real hardware (`04_adc_1ch`, 2026-08-14); absolute-voltage
  accuracy is still unverified pending a jumper/pot test (needs JST connectors).
  See `planning/RODEO_ADC_LIB_DESIGN.md` for the driver design and scan-timing facts.
- **Encoder hub:** the master↔hub I²C wire contract is
  `rev-a/rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md` (locked, silicon-verified 2026-08-20).
- **I²C bus caveat:** the motherboard carries no on-board I²C pull-ups; the
  encoder hub is the Rev-A bus anchor. A blank OLED with no hub present is a
  missing-pull-up symptom, not firmware.
