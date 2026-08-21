# rodeo-encoder-hub — firmware

Bare-metal firmware for the **STM32C031C6** (Cortex-M0+, 48 MHz, 32 KB flash /
12 KB RAM) encoder hub. The hub reads 9 EC11 encoders + push-switches and serves
them to the Rodeo motherboard (Daisy Seed) as an **I²C target**, implementing
[`RODEO_HUB_I2C_CONTRACT.md`](../../rodeo-001/docs/RODEO_HUB_I2C_CONTRACT.md).

Toolchain: **STM32Cube LL** (low-layer, vendored) + ARM GNU Toolchain. No HAL,
no CubeMX code-gen, no RTOS.

## Build

```sh
make GCC_PATH=/Applications/ArmGNUToolchain/14.3.rel1/arm-none-eabi/bin/
```

`arm-none-eabi-gcc` must come from a newlib-equipped toolchain (the ARM GNU
Toolchain, *not* Homebrew's `arm-none-eabi-gcc`, which ships without newlib).
Put the path in a gitignored `config.mk` (`GCC_PATH := .../bin/`) to avoid
passing it each time. Output: `build/rodeo-encoder-hub.{elf,bin}`.

Current footprint: ~3.4 KB flash, ~1.6 KB RAM — ~10 % of the part.

## Architecture

| File | Role |
|---|---|
| `src/main.c` | Boot: 48 MHz clock, read address jumpers, init, 1 kHz SysTick → poll, WFI idle |
| `src/board.h` | Pin map (contract §10) as an X-macro encoder table + I²C/jumper/LED pins |
| `src/encoder.{c,h}` | 9× Gray-code quadrature decode (4 steps/detent) + switch debounce |
| `src/hub.{c,h}` | The register-shadow model — read/write semantics, read-clear deltas, latches, CMD |
| `src/i2c_target.{c,h}` | I²C1 target-mode ISR: register-pointer protocol, auto-increment |
| `include/rodeo_hub_regs.h` | **Shared** C encoding of the register map (also used by the master) |
| `vendor/` | STM32Cube LL + CMSIS (device `cmsis_device_c0`, driver `stm32c0xx_hal_driver`) |

Concurrency: `encoder_poll()` runs in the SysTick ISR and pushes inputs into the
shadow via `hub_apply_inputs()`, which masks the I²C1 IRQ so a master read never
observes a half-applied update.

## Status

**Compiles + links clean. Not yet run on hardware** — the three boards aren't
fabbed. The following are structurally correct but need **bench validation**
when hardware lands, and are the known hardware-specific assumptions:

1. **I²C `TIMINGR`** = provisional 100 kHz @ 48 MHz (`0x10707DBC`) — validate
   SDA/SCL setup-hold on a logic analyzer, then generate the 400 kHz value.
2. **I²C1 alternate function** = AF6 on PB6/PB7 — confirm against the STM32C031
   datasheet AF table.
3. **EC11 steps/detent** = 4 (`ENC_STEPS_PER_DETENT`) — confirm the specific EC11
   variant; some are 2/detent.
4. **Jumper polarity** — code reads cut = HIGH = 1 bit (external 10 k to +3V3,
   jumper to GND). Confirm on the board.
5. **CW = positive** delta is a bench calibration; flip via `CONFIG.ENC_DIR_INVERT`.

## Not yet implemented (by design)

- **LED driver** (WS2812 on PB8/TIM16+DMA) — stubbed; `CAPABILITIES.bit2 = 0`,
  LED register writes are no-op'd. Wired up when the `rodeo-encoder-ring` variant
  exists. LEDs are currently a parked future upgrade (encoder feedback = OLED).
