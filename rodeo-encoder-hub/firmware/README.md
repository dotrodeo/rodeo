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

## Flashing

The STM32C031C6 has no USB DFU, so firmware goes in over **SWD** through the
5-pin `J12` header — `GND / VDD / SWDIO / SWCLK / NRST`, pin 1 first. Any
**CMSIS-DAP** probe works — and if you don't own an ST-Link, a **Raspberry Pi
Pico or Pico 2 running Raspberry Pi's `debugprobe` firmware is a full CMSIS-DAP
probe**. That is the whole "what goes between the PC and the pogo probe":
USB → Pico → SWD.

*(Aside: the part does have ST's ROM bootloader in system memory — AN2606 covers
STM32C031xx with USART/I2C/SPI — but it needs BOOT0 asserted at reset, and BOOT0
is not on the programming header. SWD is the intended path.)*

### One-time: turn the Pico into a probe

1. Grab `debugprobe_on_pico2.uf2` (or `debugprobe_on_pico.uf2` for a Pico 1)
   from <https://github.com/raspberrypi/debugprobe/releases>.
2. Hold **BOOTSEL** while plugging the Pico in — it mounts as `RP2350`
   (Pico 2) or `RPI-RP2` (Pico 1).
3. Drag the `.uf2` onto it. It reboots as "Debugprobe on Pico (CMSIS-DAP)".

Nothing else is needed: the M4 Express / ItsyBitsy M0 *can* be made into probes
with third-party CMSIS-DAP builds, but the Pico route is first-party, actively
maintained, and takes one drag-and-drop.

### Wiring

Pin assignments are from `debugprobe`'s own `include/board_pico_config.h`, so
they hold for both Pico and Pico 2 (identical form factor):

**Find pin 1 before anything else.** `J12` has no silkscreen pin labels — the
only marking is that **pin 1 is the square pad**; the other four are round.
Counting from the wrong end puts 3V3 onto SWCLK and leaves VDD floating, and the
board then simply never answers on SWD — `probe-rs info` reports no target,
which reads exactly like bad pogo contact. Note that SWDIO sits dead centre, so
it lands correctly either way: it will *not* tip you off.

Rows are in header order, pin 1 → 5:

| J12 pin | Hub net | Pico physical pin | GPIO | Notes |
|---|---|---|---|---|
| **1** (square pad) | **GND** | 3 (or 8/13/…) | GND | Make this contact first |
| **2** | **VDD** | 36 | 3V3(OUT) | **Only** if the hub is not powered elsewhere |
| **3** | **SWDIO** | 5 | GP3 | |
| **4** | **SWCLK** | 4 | GP2 | |
| **5** | **NRST** | 2 | GP1 | Optional; wire it anyway — it is the rescue path |

Pitch is **1.27 mm**, not 2.54 mm — a pogo jig built for 0.1" headers will not
seat on it.

**Power it from exactly one source.** The hub has **no regulator of its own** —
`U3` (the STM32) is the only IC on the board, and header VDD is the same net as
Qwiic VDD, straight to the MCU rail. So it is powered by whatever you hand it,
and if you hand it nothing the debug port is simply dead. Either let the Pico's
3V3(OUT) power the hub (simplest: hub unplugged from the motherboard), *or*
power the hub from the Qwiic bus and leave the header's VDD **unconnected** —
never both, or you are
back-feeding two regulators into each other. No level shifting is needed either
way: the Pico's SWD is 3.3 V native, same as the target.

### Flash it

```sh
brew install probe-rs-tools
cd rodeo-encoder-hub/firmware

make probe-check                     # attach only — proves pogo contact + power
make                                 # build
make flash                           # probe-rs download --start
```

`make probe-check` first, always. It runs `probe-rs list` then
`probe-rs info --protocol swd --verbose`, which separates the two failures
cleanly: `list` proves the **probe** enumerates over USB, `info` proves the
**target** answers on SWD. With a hand-held pogo probe the usual fault is
contact, not configuration, and finding that out before a half-written flash
saves confusion.

`--verbose` on that second command is load-bearing. Without it, `probe-rs info`
tries to auto-detect the chip against its registry and gives up on an STM32C0
with *"The connected chip could not automatically be determined"* **even with
perfect wiring**. `--verbose` skips the registry and enumerates the debug port
directly. Flashing never relies on auto-detection — `make flash` passes
`--chip STM32C031C6` explicitly.

If the target still doesn't answer, `make probe-check-reset` re-runs the same
check with `--connect-under-reset`. That is what the NRST wire is for: it holds
the part in reset while the probe attaches, which is the rescue path if firmware
is ever holding the bus busy.

A target that *is* answering looks like this — `DPv1`/`MINDP` is the minimal
debug port every Cortex-M0+ uses, and the two ROM tables are ST's and ARM's:

```
Debug Port: DPv1, MINDP, Designer: ARM Ltd
└── V1(0) MemoryAP
    └── 0 MemoryAP (AmbaAhb3)
        ├── 0xf0000000 ROM Table (Class 1), Designer: STMicroelectronics
        └── 0xe00ff000 ROM Table (Class 1), Designer: ARM Ltd
```

The trailing *"Debug port version DPv1 does not support SWD multidrop"* is
informational, not a failure: multidrop is a DPv2 feature (it is how RP2040
addresses two cores on one SWD bus), so on a single-target DPv1 part probe-rs
just notes it and stops enumerating.

> **Use probe-rs, not Homebrew's OpenOCD.** STM32C0 flash support landed in
> OpenOCD *after* the 0.12.0 release: 0.12.0's `stm32l4x` driver advertises only
> `STM32G0/G4/L4/L4+/L5/U5/WB/WL`, and it ships no `target/stm32c0x.cfg`.
> Copying in the newer config does **not** rescue it — the config then resolves
> but `flash probe` fails to identify the device, because the driver itself has
> no STM32C0 IDs. `make flash-ocd` is kept for an OpenOCD built from master.

### You cannot lock yourself out

The classic bootloader-less footgun is firmware that reassigns the SWD pins:
you get exactly one flash, and then the debug port is gone. **This firmware does
not do that.** `src/board.h` puts the nine encoders on PA0–PA12, PB0–PB5,
PB10–PB15, PC6/PC7 and I2C1 on PB6/PB7 — **PA13 (SWDIO) and PA14 (SWCLK) are
untouched**, so SWD stays live after every flash. If a future revision ever does
need those pins, that is when the NRST wire above earns its keep (connect under
reset), which is why it is worth pogoing now.

### If it doesn't take

| Symptom | Likely cause |
|---|---|
| No probe found | `debugprobe` UF2 not loaded, or a charge-only USB cable |
| Probe found, no target | Pogo contact, the header counted from the wrong end, or the hub is unpowered — recheck pin 1 (square pad) and VDD/GND |
| Attaches then fails mid-write | Flaky pogo contact; a jig beats a steady hand |
| `stdint.h: No such file` | Homebrew's newlib-less gcc — see **Build** above |
| `Can't find target/stm32c0x.cfg` | Homebrew OpenOCD 0.12.0 predates STM32C0 — use `make flash` (probe-rs) |
| `The connected chip could not automatically be determined` | `probe-rs info` without `--verbose` — STM32C0 is not auto-detectable. Not a wiring fault on its own |
| Probe lists, but target never answers | Pogo contact, reversed header, or target power. Try `make probe-check-reset` |
| `error: unexpected argument '--start' found` | probe-rs 0.32 `download` takes `--reset`, not `--start` — already fixed in the Makefile |

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
