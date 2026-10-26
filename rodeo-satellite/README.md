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
| `src/ladder.*` | done, tested | The encoder bank's four push switches on one ADC pin (and the single encoder's one) |
| `src/buttons.*` | done, tested | The button bank's four switches on their own pins, debounced, with press counters |
| `src/lights.*` | done, tested | The button bank's four RGB LEDs: output slots to colours, blink and `IDENTIFY`, and a current cap on chain power |
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

| | Single pot (kind 1) | Single encoder (kind 3) | Pot bank (kind 5) | Encoder bank (kind 6) | Button bank (kind 7) |
|---|---|---|---|---|---|
| Chain TX, normal | PD5 (USART1 map 0) | PD5 | PD5 | PD5 | PD5 |
| Chain RX | PD6, PC1 | PD6, PC1 | PD6, PC1 | PD6, PC1 | PD6, PC1 |
| Return tap TX, when last | PC0 (map 3) | PC0 | PC0 | PC0 | PC0 |
| `PRESENT` | PC3 | PD2 | PC3 | PD2 | PC3 |
| ID divider | PD3 (A4), fed from PC2 | PD3 (A4), fed from 3V3 | PD3 (A4), fed from PC2 | PD3 (A4), fed from 3V3 | PD3 (A4), fed from 3V3 |
| LED | PC6 | PD0 | PC6 | PD0 | four WS2812B-2020 from PC6 (SPI1 MOSI) |
| Controls | pot on PA2 | encoder A/B on PA1/PA2; switch on PD4 (A7), a one-rung ladder | pots on PA2, PD2, PC4, PA1 | encoders A/B on PA1/PA2, PC2/PC3, PC4/PC5, PC6/PC7; switches on PD4 (A7) | switches on PA1, PA2, PD4, PD0 (internal pull-ups); `AUX_SENSE` on PC4 (A2) |
| Programming | SWIO on PD1 | SWIO on PD1 | SWIO on PD1 | SWIO on PD1 | SWIO on PD1 |

Each single-control board is its bank cut to slot 1: one slot, the same pins. The
single encoder's push switch is the bank's ladder with one rung; the others are
given as `LADDER_ABSENT`, so the bank's decoder serves it unchanged.

## The button bank's LEDs

Each of the button bank's four output slots drives one WS2812B-2020: an `RGB` slot
(§3.2 of the contract) sets its colour, an `LED` slot sets white at its brightness
and blink, `NONE` keeps it. `IDENTIFY` blinks all four white.

* **Driver:** ch32v003fun's `ws2812b_dma_spi_led_driver.h` clocks the bit stream out
  of SPI1's MOSI (PC6) by DMA channel 3; USART1 uses channels 4 and 5. Its interrupt
  refills the buffer, so a new picture is started only between frames and never
  lands while a frame streams through. The four LEDs take about 200 µs.
* **Current cap:** every channel draws 12 mA at full scale, 144 mA for all four at
  white. On the chain's 3V3 the four are scaled together to 24 mA in all
  (`LIGHTS_CAP_CHAIN_MA`); with the aux 5 V plugged in (`AUX_SENSE` above a third of
  full scale) they run uncapped. The colour stays true; only its brightness drops.
* **To check on the first board:** the LEDs' colour order (the driver sends G, R, B),
  the cap's brightness on chain power, and that a frame's bytes still go out on time
  while the LEDs update.

