# rodeo-sdk: the Rev B rewrite

**Status: plan, 2026-10-18.** Rev A is frozen; nothing for Rev B is written yet. The SDK as it stands is the
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
4. **The encoder hub ceases to exist on Rev B.** Encoder banks on the satellite chain
   replace it; the hub board, its firmware and its I²C contract stay Rev A parts, and
   `encoder_hub.*` and `rodeo_encoders.*` don't come forward.
5. **rodeo-002 is the first instrument to move to Rev B**, after bring-up.
6. **USB needs exploring before it's designed** (see [USB](#usb-explore-first)).

## What happens to each part of the SDK

| Part | Rev B | Notes |
|---|---|---|
| `adc_convert.h`, `pot_ui.h`, `rodeo_dsp.h`, `rodeo_scope.h`, `rodeo_screen.h` | **Keep** | Pure logic, host-tested; nothing board-specific in them. |
| `oled_ssd130x_bulk.*` | **Keep** | The I²C bus is unchanged (I²C1, now with on-board pull-ups and four taps). |
| `encoder_hub.*`, `rodeo_encoders.*` | **Remove** | The encoder hub doesn't exist on Rev B (decision 4); encoders arrive on the satellite chain as encoder-bank slots. The Rev A versions stay in `rev-a/rodeo-sdk/`. |
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
  opening the case) and serial. How is an open question, explored before it's designed
  (below); it goes last in the bring-up order.
* **Battery gauge**: a MAX17048 driver (I²C 0x36) for charge %, and a save-and-dim hook
  before the cell runs out.
* **I/O connectors**: a small API over the 8 connectors and CV1/CV2: digital in/out,
  analog in (the Seed's ADC), DAC out on CV1/CV2.

## USB: explore first

What Rev B wants over its one USB port: **USB-MIDI, the SD card as a USB drive, and a
serial console, at the same time** (one composite device), plus DFU flashing through the
bootloader. libDaisy ships USB MIDI and CDC serial on ST's USB device library, but no
mass storage and no composite descriptor. Before choosing, a short exploration answers:

* **How far does libDaisy's stack go?** Can its ST middleware be extended with MSC and a
  composite MIDI + MSC + CDC descriptor, and what does that cost in flash and RAM next to
  the audio and the chain?
* **Is TinyUSB the better base?** It does composite devices (MIDI, MSC, CDC) natively
  and supports the STM32H7's OTG HS core; the questions are how cleanly it sits beside
  libDaisy (both want the USB peripheral and its interrupt) and whether it keeps up with
  audio-rate callbacks.
* **Does mass storage coexist with the instrument using the card?** While the host owns
  the SD card as a drive the firmware can't write to it; decide whether the drive is
  only offered in a "load samples" mode.
* **DFU:** the Daisy bootloader's `extdfu` handles flashing; confirm it still works
  when the app presents a composite device, and how the user gets into it.

Output: a short findings section here and a recommendation, from a test build on a bare
Daisy Seed (no Rev B board needed). Until then `08_usb_midi` uses libDaisy's existing
USB MIDI, and `09_usb_storage` waits.

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

1. ~~**Freeze Rev A**~~ done 2026-10-18 (below).
2. **`board.h` for Rev B** from `rev-b.md`'s pin map.
3. **`sat_frame.h` and its host tests**: frame build/parse, CRC, every slot kind,
   truncated and corrupted frames. Pure code; it needs no hardware.
4. **`rodeo-satellite/` skeleton** on ch32v003fun, built and flashed on a CH32V003
   development board: kind detection, the slot-forwarding loop, a fake input slot.
5. **The audio and SD examples' code**, written against libDaisy, ready to run when the
   motherboard arrives.
6. **The USB exploration** above, on a bare Daisy Seed.

## Freezing Rev A

**Done 2026-10-18.** The SDK as tagged `v0.2.0` is copied into
[`rev-a/rodeo-sdk/`](../../rev-a/rodeo-sdk/), with its own `libDaisy`/`DaisySP`
submodules pinned at the tag's commits, and rodeo-001's and rodeo-002's `RODEO_ROOT`
point there. The one edit from the tag is the encoder-hub include path in its
`common/rodeo.mk`, for its new location. Checked: rodeo-002's three firmwares and
rodeo-001's build clean against it, and rodeo-002's 181 host tests pass. The top-level
`rodeo-sdk/` is now free to change.

## Versioning

* The Rev B SDK starts at **`v0.3.0`**: a breaking change in a 0.x SDK is a minor bump.
  `version.h` keeps the changelog.
* **1.0** when the pot pipeline is settled and an instrument runs on Rev B.
* `rodeo-satellite/` versions itself the same way (`rodeo-satellite/vX.Y.Z`), and its
  major version is the one reported in discovery (contract §8): the Seed refuses a
  chain with a mismatched major.

## Open questions

* The pot conditioning split between satellite and Seed (above).
* USB: libDaisy's stack or TinyUSB, after the exploration above.
* Porting rodeo-002: its 16 pots and its encoders move from the Rev A ADCs and the hub
  onto satellites (four pot banks and two encoder banks, say), and its control map gets
  rewritten for that.
