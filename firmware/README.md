# Rodeo 001 — Firmware

12-voice digital subtractive synth on the Electrosmith Daisy Seed 3.
Architecture and roadmap: see the approved firmware plan (control-plane / audio-plane
split, external-ADC control surface, per-voice filter). This README covers building.

**Current milestone: M2 — 12-voice polyphony.** MIDI IN (J2) plays a 12-voice poly
synth (2 osc → per-voice resonant filter → amp, with per-voice amp + filter
envelopes; FIFO voice allocation + stealing). Parameters are fixed placeholder
defaults until the external-ADC control surface (M4). Omni MIDI. No panel/ADC yet.

Done: **M0** (boots, tone on J5, LEDs) · **M1** (one MIDI voice, mono) · **M2** (12-voice poly).

## Toolchain

- **ARM GNU Toolchain** (`arm-none-eabi-gcc`) **with newlib.**
  On macOS the Homebrew `arm-none-eabi-gcc` ships *without* newlib and will fail with
  `stdint.h: No such file or directory`. Install the official
  [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
  and either put it on `PATH` ahead of Homebrew, or set `GCC_PATH` in `firmware/config.mk`:
  ```make
  GCC_PATH := /Applications/ArmGNUToolchain/14.3.rel1/arm-none-eabi/bin
  ```
  `config.mk` is gitignored (machine-specific). It is auto-included by the Makefile.
- `dfu-util` for flashing; a host C++17 compiler for the unit tests.
- Submodules: `git submodule update --init --recursive`.

## Build

```sh
cd firmware
make deps        # build libDaisy + DaisySP static libs (first time / after submodule bump)
make             # -> build/rodeo-001.{elf,bin,hex}
make host-test   # native unit tests
make clean
```

## Flash

Put the Daisy in DFU (hold BOOT, tap RESET) over its **onboard micro-USB**
(the panel USB-C is power-only), then:

```sh
make program-dfu
```

Expected after flash: LEDs blinking at ~1 Hz; a MIDI keyboard on J2 (MIDI IN)
plays up to 12 notes. CPU load + active-voice count print over the onboard USB
serial — open a serial monitor (115200) to watch the audio-callback budget.

## Layout

```
src/platform/  board pin map (host-safe) + MCU setup (FTZ)
src/main.cpp   M0 audio bring-up
tests/         host-native unit tests
libDaisy/ DaisySP/   submodules
```
