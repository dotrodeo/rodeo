# rodeo-sdk: the Rev B rewrite

**Status: plan, 2026-10-18.** Nothing here is written yet. The SDK as it stands is the
Rev A SDK, tagged [`rodeo-sdk/v0.2.0`](../common/version.h), the last Rev A release.
The hardware this targets is in
[`hardware/rodeo-motherboard/docs/rev-b.md`](../../hardware/rodeo-motherboard/docs/rev-b.md)
(its "Firmware consequences" section is the starting list) and the satellite protocol in
[`RODEO_SATELLITE_CHAIN_CONTRACT.md`](../../hardware/rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md).

## Decisions

Settled with Derek on 2026-10-18:

1. **The SDK becomes Rev B only.** No board switch, no Rev A code paths. Rev A is frozen
   at `v0.2.0`, and rodeo-001 and rodeo-002 keep building against that frozen copy (see
   [Freezing Rev A](#freezing-rev-a)). They get no further SDK fixes unless they are
   ported to Rev B, which is not part of this plan.
2. **Satellite firmware lives in a top-level `rodeo-satellite/`**, built with
   [ch32v003fun](https://github.com/cnlohr/ch32v003fun): open source, a plain Makefile
   and riscv-gcc, flashed with a WCH-LinkE over SWIO. One image for every satellite
   kind; the ID resistor picks the kind (contract §1.6).
3. **The first milestone is bring-up: every Rev B board proven** by an example of its
   own, before any instrument moves to Rev B (see [Milestone 1](#milestone-1-bring-up)).

## What happens to each part of the SDK

| Part | Rev B | Notes |
|---|---|---|
| `adc_convert.h`, `pot_ui.h`, `rodeo_dsp.h`, `rodeo_scope.h`, `rodeo_screen.h` | **Keep** | Pure logic, host-tested; nothing board-specific in them. |
| `oled_ssd130x_bulk.*` | **Keep** | The I²C bus is unchanged (I²C1, now with on-board pull-ups and four taps). |
| `encoder_hub.*`, `rodeo_encoders.*` | **Keep** | The hub and its contract are unchanged on Rev B (`rev-b.md`: "The hub firmware is unaffected"). Encoder banks on the satellite chain are the new route; the hub stays for I²C builds. |
| `tools/screens` | **Keep** | |
| `board.h` | **Rewrite** | Rev B's pin map (`rev-b.md`, "Seed pin map"): the 8 I/O connectors (`D7`, `D15`–`D21`, seven analog-capable), CV1/CV2 on `D23`/`D22` (DAC out or analog in), satellite chain on USART3 (`D2` TX, `D1` RX), expansion-header chip-select `D0`, second codec reset `D3` and SAI2, SD on `D4`–`D6`, USB on `D29`/`D30`. The status LED is gone. |
| `rodeo_adc.*`, `ads114s08.*` | **Change** | The two on-board ADS114S08s are gone; one optional ADS114S08 sits on the expansion header (`SPI_EXT_CS`). `RodeoAdc` goes from a fixed 2-chip, 24-channel scanner to N chips (0 or 1 on Rev B). An ADS124S08 needs `ReadData()` to take 3 bytes. |
| `status_led.h` | **Remove** | No status LED on Rev B. |
| `rodeo.mk` | **Change** | `extdfu` bootloader build; app Makefiles keep `BOOT_SRAM`; update-from-SD goes away (`rev-b.md`). |
| Examples 01–10 | **Replace** | With the Rev B bring-up set below. The Rev A ones stay in the frozen copy. |
| `template/` | **Update** | Rev B `board.h`, new example layout. |

## New in the SDK

* **4-in/4-out audio.** SAI1 (the Seed's codec) and SAI2 (the CS4272) together through
  `AudioHandle::Init(cfg, sai1, sai2)`, one callback for all eight channels, as
  `daisy_patch.cpp` does. Release `CODEC2_RST` (`D3`) only after SAI2's clocks run.
  Channel numbering follows the back panel: **1/2 = the Seed's codec, 3/4 = the CS4272**.
* **Satellite chain driver** (`sat_chain.*`): USART3 at 1 Mbaud on the DMA streams
  libDaisy leaves free (`DMA1_Stream7` RX, `DMA2_Stream5` TX), timer-paced scans (default
  4 ms), DISCOVER on start-up and after a PRESENT change, fault and break reporting
  (contract §§4–7).
* **Chain protocol, shared** (`sat_frame.h`): frame and slot layout, CRC-8/SMBUS, slot
  kinds, the parse/build functions. **Pure, no hardware**: the SDK's driver and the
  satellite firmware both include this one header, and host tests exercise it from both
  sides, so the two ends can't drift. It lives in the SDK (`common/`); `rodeo-satellite/`
  includes it by path.
* **SD card** through `SdmmcHandler` with `BusWidth::BITS_1`, and FatFs.
* **USB device**, composite: MIDI, mass storage on the SD card (samples load without
  opening the case) and serial. **Risk:** libDaisy ships USB MIDI and CDC but not mass
  storage or a composite descriptor; this is the piece most likely to need real work in
  the STM32 USB stack, so it goes last in the bring-up order.
* **Battery gauge**: a MAX17048 driver (I²C 0x36) for charge %, and a save-and-dim hook
  before the cell runs out.
* **I/O connectors**: a small API over the 8 connectors and CV1/CV2: digital in/out,
  analog in (the Seed's ADC), DAC out on CV1/CV2.

## The pot pipeline (the 1.0 blocker)

`v0.1.0` named it and it still stands: the SDK's `GetNorm` learns a pot's travel, which
rodeo-002 abandoned on hardware for fixed per-wiring ranges with a dead zone
(`instruments/rodeo-002/firmware/common/panel_pot.h`). Rev B moves most pots onto
satellites, so the rewrite is the moment to settle it once:

* **Proposal:** satellites send raw readings; the Seed conditions them with rodeo-002's
  fixed-range-plus-dead-zone approach, moved into the SDK as a pure, host-tested header.
  Learned travel is dropped. The same conditioning serves pots on the I/O connectors.
* Open: whether satellites smooth or deadband before sending (less traffic, less
  Seed-side work) or send everything raw (one place to tune). Decide once a pot bank is
  on the bench.

## Milestone 1: bring-up

One example per board, in the order the boards can be tested. Each proves its board and
nothing else, and is the reference for how an instrument uses it.

| # | Example | Proves | Needs |
|---|---|---|---|
| 01 | `blink_power` | Seed boots on the Rev B motherboard; battery %, charging and power-good from the MAX17048 over I²C | motherboard, a power board, a cell |
| 02 | `audio_4x4` | All eight audio channels: tone out on 1–4, loopback in 1–4, both codecs in one callback | jack board |
| 03 | `midi` | MIDI in and out through the jack board | MIDI board |
| 04 | `io_connectors` | Pots on the analog-capable connectors, digital in, CV DAC out | a Rev A pot satellite or bare pots |
| 05 | `satellite_chain` | Discovery, scans and faults with one pot bank and one encoder bank, then the ring broken and closed | satellites flashed (below) |
| 06 | `oled_i2c` | OLED and the I²C taps on Rev B | an SSD130x |
| 07 | `sd_card` | Read and write the internal SD in 1-bit mode | an SD card |
| 08 | `usb_midi` | USB-MIDI through the power board | USB-C power board |
| 09 | `usb_storage` | The SD card as a USB drive (composite with MIDI) | the riskiest piece; last |

Plus the satellites' own bring-up in `rodeo-satellite/`: kind detection from the ID
resistor, a pot bank and an encoder bank answering DISCOVER and SCAN, then the chain.

## Work that can start before the boards arrive

1. **Freeze Rev A** (below), so the rewrite can begin without breaking rodeo-001/002.
2. **`board.h` for Rev B** from `rev-b.md`'s pin map.
3. **`sat_frame.h` and its host tests**: frame build/parse, CRC, every slot kind,
   truncated and corrupted frames. Pure code; it needs no hardware.
4. **`rodeo-satellite/` skeleton** on ch32v003fun, built and flashed on a CH32V003
   development board: kind detection, the slot-forwarding loop, a fake input slot.
5. **The audio, SD and USB examples' code**, written against libDaisy, ready to run
   when the motherboard arrives.

## Freezing Rev A

rodeo-001 and rodeo-002 point `RODEO_ROOT` at the SDK by relative path, so the moment
the SDK changes they build against Rev B code. Before the first Rev B commit:

* **Proposal:** copy the SDK as tagged into `rev-a/rodeo-sdk/` (the Rev A archive),
  with its own pinned `libDaisy`/`DaisySP` submodules at `v0.2.0`'s commits, and point
  the instruments' `RODEO_ROOT` there. Self-contained: anyone who clones the repo can
  still build the Rev A instruments, with no tag checkout. Its README says it's frozen.
* The alternative, building against a worktree checked out at the tag, saves the copy
  but makes every Rev A build depend on a git step.

## Versioning

* The Rev B SDK starts at **`v0.3.0`**: a breaking change in a 0.x SDK is a minor bump.
  `version.h` keeps the changelog.
* **1.0** when the pot pipeline is settled and an instrument runs on Rev B.
* `rodeo-satellite/` versions itself the same way (`rodeo-satellite/vX.Y.Z`), and its
  major version is the one reported in discovery (contract §8): the Seed refuses a
  chain with a mismatched major.

## Open questions

* The pot conditioning split between satellite and Seed (above).
* Whether the hub (`encoder_hub.*`) is still worth carrying once encoder banks exist on
  the chain, or becomes a Rev A only part.
* USB mass storage: libDaisy's USB stack, or a different one (e.g. TinyUSB) if a
  composite MIDI + MSC + CDC device proves hard.
* Which instrument moves to Rev B first, once bring-up is done.
