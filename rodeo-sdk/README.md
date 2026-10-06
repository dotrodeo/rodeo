# Rodeo Motherboard — Firmware

Boilerplate and PCB bring-up examples for the Rodeo motherboard (an Electrosmith
Daisy Seed carrier). This tree is a **starting point for Rodeo
firmware** plus **small standalone programs that validate each subsystem** of a
freshly assembled board — flash one, confirm the hardware, move on.

> The previous contents (a 12-voice subtractive synth) are preserved on the git
> tag `archive/synth-m2`. Restore with `git checkout archive/synth-m2 -- firmware/src`.

## Layout

```
firmware/
  common/
    board.h              # Rodeo pin map — the single source of truth (host-safe)
    version.h            # SDK version + RODEO_SDK_AT_LEAST(); the real changelog
    rodeo.mk             # shared build rules; every app/example includes this
    ads114s08.{h,cpp}    # Layer 1: per-chip ADS114S08 SPI ADC driver
    rodeo_adc.{h,cpp}    # Layer 2: 24-ch two-chip pipelined ADC scanner
    adc_convert.h        # pure value pipeline + PotSpan knob-travel calibration
    encoder_hub.{h,cpp}  # Layer 1: per-hub STM32C0 I2C target driver (master side)
    rodeo_encoders.{h,cpp} # Layer 2: multi-hub encoder scanner + friendly API
    status_led.h         # tiny onboard/carrier LED helper
    oled_ssd130x_bulk.{h,cpp} # faster SSD130x I2C transport (page-at-a-time + yield hook)
    pot_ui.h             # pure panel logic: stepped pots, "which knob is moving"
    rodeo_dsp.h          # pure DSP blocks: Smoother, OnePole, DcBlocker, Lfo, Rng
    rodeo_scope.h        # triggered, normalised waveform for a small display
    rodeo_screen.h       # 128x64 screen primitives: title bar, list rows, scroll bar
    tests/               # host-side GoogleTest for the pure-logic headers
  template/          # copy-to-start skeleton for a new Rodeo app (blinks LEDs)
  examples/
    01_blink/          # onboard + carrier LED — boots-and-alive smoke test
    02_audio_tone/     # 440 Hz sine on the output jacks — codec / SAI path
    03_midi_mono_sine/ # MIDI IN -> monophonic sine voice with a short ADSR
    04_adc_1ch/        # one channel of the external ADS114S08 SPI ADC -> serial
    05_adc_scan/       # full 24-ch control-surface scan via rodeo::RodeoAdc
    06_poly_synth/     # 6-voice poly subtractive synth, 10 live pots (first musical demo)
    07_encoder_scan/   # rodeo-encoder-hub scan via rodeo::RodeoEncoders, on the OLED
                       #   (also proves the hub's I2C pass-through + bus pull-ups)
    08_oled_i2c/       # SSD1309 128x64 feedback display smoke test (I2C @ 0x3C)
    09_poly_synth_oled/ # 06 + the OLED: knob-value popups and a live output scope
  tools/
    screens/           # render an instrument's real OLED screens on your laptop
  docs/
    RODEO_ADC_LIB_DESIGN.md  # design of the ADS114S08 / RodeoAdc driver stack
  libDaisy/  DaisySP/ # vendored submodules (static libs live in their build/)
  Makefile           # top-level helpers: `make deps`, `make all`, `make clean`
```

Each app/example is a self-contained folder: its own `src/main.cpp` and a tiny
`Makefile` that just sets `TARGET` + `CPP_SOURCES` and includes
`common/rodeo.mk`. `cd` into one and run `make`.

## Toolchain

- **ARM GNU Toolchain** (`arm-none-eabi-gcc`) **with newlib.** On macOS the
  Homebrew `arm-none-eabi-gcc` ships *without* newlib and fails with
  `stdint.h: No such file or directory`. Install the official
  [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
  and set `GCC_PATH` in `firmware/config.mk` (gitignored, machine-specific;
  auto-included by every build):
  ```make
  GCC_PATH := /Applications/ArmGNUToolchain/14.3.rel1/arm-none-eabi/bin
  ```
- `dfu-util` for flashing; submodules: `git submodule update --init --recursive`.

## Build & flash

```sh
cd firmware
make deps                      # build libDaisy + DaisySP static libs (first time)

cd examples/01_blink
make                           # -> build/blink.{elf,bin,hex}
make program-dfu               # flash: hold BOOT, tap RESET on the Daisy first
```

`make all` from `firmware/` builds every example.

## Adding a new app or bring-up example

1. Copy `template/` to `examples/<NN_name>/` (two levels below `firmware/`, so
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
  build and in that instrument's own control map: `rodeo-001/docs/RODEO_CONTROL_MAP.md`,
  `rodeo-002/docs/RODEO_002_CONTROL_MAP.md`. **This library serves every
  instrument, not the first one** — if something here names a specific knob or a
  specific sound, it is in the wrong place.
- **External ADC (ADS114S08) status:** the SPI orientation and conversion path
  are bench-confirmed on real hardware (`04_adc_1ch`, 2026-08-14); absolute-voltage
  accuracy is still unverified pending a jumper/pot test (needs JST connectors).
  See `docs/RODEO_ADC_LIB_DESIGN.md` for the driver design and scan-timing facts.
- **Encoder hub:** the master↔hub I²C wire contract is
  `rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md` (locked, silicon-verified 2026-08-20).
- **I²C bus caveat:** the motherboard carries no on-board I²C pull-ups; the
  encoder hub is the Rev-A bus anchor. A blank OLED with no hub present is a
  missing-pull-up symptom, not firmware.
