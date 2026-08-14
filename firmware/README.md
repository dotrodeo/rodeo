# Rodeo Motherboard — Firmware

Boilerplate and PCB bring-up examples for the Rodeo motherboard (Electrosmith
Daisy Seed on the rodeo-001 board). This tree is a **starting point for Rodeo
firmware** plus **small standalone programs that validate each subsystem** of a
freshly assembled board — flash one, confirm the hardware, move on.

> The previous contents (a 12-voice subtractive synth) are preserved on the git
> tag `archive/synth-m2`. Restore with `git checkout archive/synth-m2 -- firmware/src`.

## Layout

```
firmware/
  common/
    board.h          # Rodeo pin map — the single source of truth (host-safe)
    rodeo.mk         # shared build rules; every app/example includes this
  template/          # copy-to-start skeleton for a new Rodeo app (blinks LEDs)
  examples/
    01_blink/        # onboard + carrier LED — boots-and-alive smoke test
    02_audio_tone/   # 440 Hz sine on the output jacks — codec / SAI path
    03_midi_mono_sine/ # MIDI IN -> monophonic sine voice with a short ADSR
    04_adc_1ch/      # one channel of the external ADS114S08 SPI ADC -> serial
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

## Notes

- `board.h` is the pin/topology contract for the whole board (SPI/ADC, MIDI,
  audio, LED, 24-channel ADC surface). Keep it in sync with the KiCad source.
- `04_adc_1ch` is a first bring-up draft for the external ADCs — the one
  subsystem not yet hardware-verified. Its reference configuration may need to
  match the board's actual REF wiring; see the header comment in its `main.cpp`.
