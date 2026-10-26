# rodeo-satellite

Firmware for the satellites on Rodeo's chain: the small boards (pot banks,
encoder banks, and the single-control boards to come) that each carry a
CH32V003 and pass their controls to the Seed along a UART ring. One image runs
on every satellite; an ID resistor tells it which board it is on.

The wire protocol is
[`RODEO_SATELLITE_CHAIN_CONTRACT.md`](../hardware/rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md),
and its byte layout is [`rodeo-sdk/common/sat_frame.h`](../rodeo-sdk/common/sat_frame.h),
which this firmware and the Seed's chain driver both include. Neither end
defines a frame on its own.

## Status (2026-10-19)

**Written and building; not yet run on a CH32V003.** The logic is host-tested;
the hardware glue (`firmware/satellite.c`) builds to 6.5 KB of the 16 KB flash
and 1 KB of the 2 KB RAM, and waits for a dev board and a WCH-LinkE.

| Part | | |
|---|---|---|
| `src/stream.*` | done, tested | Passes a frame along a byte at a time, with the lag the contract requires, matching `sat_satellite_pass()` byte for byte and never gapping |
| `src/kind.*` | done, tested | Board kind from the ID resistor, reading twice only where it's safe (below) |
| `src/encoder.*` | done, tested | Quadrature decoding in detents, bounce-proof |
| `src/ladder.*` | done, tested | The encoder bank's four push switches on one ADC pin |
| `src/pot.h` | done, tested | 10-bit reading to the POT slot's 0–65535, lightly smoothed |
| `src/boards.h` | done | Each board's pins, from its schematic |
| `firmware/satellite.c` | **builds, not run** | GPIO, ADC, USART1 on DMA with its pin remap, PRESENT, the LED, commands; on [ch32v003fun](https://github.com/cnlohr/ch32v003fun), a submodule here |

To check on the first board: that the main loop keeps a byte out for every byte
in (nothing in it may block for more than a few microseconds), that the return
tap is released between frames, the encoders' direction, and the ID reads on
both banks.

## Two things the boards taught the firmware

**The ID divider is fed two ways.** On the pot bank its 10 kΩ comes from PC2,
driven high for the read; on the encoder bank it's tied to 3V3 and PC2 is
encoder 2's A line, which must never be driven (a closed contact would short
it). So the firmware reads first with PC2 left alone, and drives it only when
that reads near ground, which is what a GPIO-fed divider reads until it's fed.
New boards should pick one convention; 3V3 is the simpler.

**A satellite can't forward with a 4-byte lag.** In a SCAN the first byte it
forwards comes after its own slots, so it can't send until that byte has
arrived: 5 byte times for a one-slot satellite, 17 for a four-slot bank. The
contract said 4 bytes and 40 µs; it now says this, and the Seed's ring timeout
counts it (`sat_lag_bytes()`, `sat_ring_time_us()`).

## Build and flash

```sh
git submodule update --init rodeo-satellite/ch32v003fun
brew install riscv64-elf-gcc riscv64-elf-binutils   # macOS
cd rodeo-satellite/firmware
make           # satellite.bin
make flash     # with a WCH-LinkE on SWIO (PD1), 3V3 and GND
```

Homebrew's RISC-V GCC has no C library; ch32v003fun needs only newlib's headers,
and the Makefile takes them from the Arm GNU Toolchain the SDK already uses
(`NEWLIB=` to point elsewhere). Every satellite gets the same image.

## Tests

```sh
cd rodeo-satellite/tests
make test
```

The C sources build as strict C99, as the satellite's toolchain builds them;
googletest comes from the copy vendored in `rodeo-sdk/libDaisy`.

## Pins (CH32V003F4P6)

| | Single pot (kind 1) | Pot bank (kind 5) | Encoder bank (kind 6) |
|---|---|---|---|
| Chain TX, normal | PD5 (USART1 map 0) | PD5 (USART1 map 0) | PD5 |
| Chain RX | PD6, PC1 | PD6, PC1 | PD6, PC1 |
| Return tap TX, when last | PC0 (map 3) | PC0 (map 3) | PC0 |
| `PRESENT` | PC3 | PC3 | PD2 |
| ID divider | PD3 (A4), fed from PC2 | PD3 (A4), fed from PC2 | PD3 (A4), fed from 3V3 |
| LED | PC6 | PC6 | PD0 |
| Controls | pot on PA2 | pots on PA2, PD2, PC4, PA1 | encoders A/B on PA1/PA2, PC2/PC3, PC4/PC5, PC6/PC7; switches on PD4 (A7) |
| Programming | SWIO on PD1 | SWIO on PD1 | SWIO on PD1 |

The single pot is the pot bank cut to slot 1: one `POT` slot, the same pins.
