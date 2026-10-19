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
   [Freezing Rev A](#freezing-rev-a)) and get no further SDK fixes there. rodeo-002 is
   ported to Rev B after bring-up (decision 5); rodeo-001 is not.
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
* **USB device** (`rodeo_usb`), composite, on TinyUSB: MIDI and serial always, UAC2
  audio (2×2 or 4×4) per instrument, and mass storage on the SD card in a disk mode, so
  samples load without opening the case. See [USB](#usb-explore-first) for why TinyUSB
  and the order it's built in. Needs an interrupt priority table with audio highest.
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

Derek, 2026-10-18: the goal is **USB-MIDI, USB storage and USB audio**, audio
expected to be the hardest.

### Findings (2026-10-18)

**libDaisy's USB can't get there.** It vendors ST's USB device library (v2.11.2) as a
patched copy, and its USB-MIDI is ST's CDC (serial) class with the descriptors swapped:
a global `usbd_mode` flag makes the one class enumerate as either serial or MIDI, never
both (`Middlewares/Patched/.../CDC/Src/usbd_cdc.c`). It is class-compliant MIDI, but
nothing can share the port with it. ST's library does include a `CompositeBuilder`, an
MSC class and an AUDIO class, but ST has no USB-MIDI class at all, its AUDIO class is
UAC1 playback only (no recording), and libDaisy's patches would have to be redone inside
the composite. Building MIDI + MSC + two-way audio there is writing most of a USB stack.

**TinyUSB has every class we need**, MIT-licensed: MIDI, MSC, CDC and UAC2 (two-way
audio, multi-channel, asynchronous with a feedback endpoint), on the STM32H7's DWC2 USB
cores, composable in one configuration. It's the base most audio products on
non-ST stacks use.

**It already works on the Daisy, for audio.** A community port (Daisy forum,
["Floret, Downlink, interrupt priorities, and USB audio (UAC2) on the Daisy"](https://community.daisy.audio/t/floret-downlink-interrupt-priorities-and-usb-audio-uac2-on-the-daisy/9847),
code in `a7v7/libDaisy` branch `meadow`, `a7v7/tinyusb` branch `daisy`, both MIT) runs
TinyUSB beside libDaisy with:

* UAC2 stereo out and stereo in, 48 kHz, 24-bit, plus a CDC serial port, driverless on
  Windows 11 and macOS (Linux untested);
* asynchronous playback paced by the codec clock with TinyUSB's feedback endpoint;
  ~20 minutes of playback (7 with recording) on Windows with no drift or overruns and
  one inaudible 1 ms underrun;
* about **2.5 % CPU** for audio both ways, mostly in the USB interrupt; ~95 KB of the
  Seed's 128 KB internal flash for the whole test app.

Electro-Smith's own 2024 TinyUSB branch got serial and MIDI partly working and stopped
short of audio.

**Two traps it found, both of which apply to us:**

1. **The feedback value format differs by OS.** USB 2.0 says 10.14 in 3 bytes at full
   speed and macOS insists on it; Windows' built-in UAC2 driver needs 16.16 in 4 bytes;
   Linux takes either. TinyUSB sent 16.16 only, so macOS stopped the stream. The port
   tells them apart because only Windows asks for Microsoft OS 2.0 descriptors, and sends
   16.16 only after that request. Newer TinyUSB has a callback for choosing the format.
2. **libDaisy runs nearly every interrupt at priority 0**, so the audio DMA interrupt
   can't preempt the USB interrupt. Measured on a Pod: 24.9 % of audio callbacks late,
   worst period 1302 µs instead of 1000 µs. With one priority table (audio highest) it
   was 0 late, 1000.0 µs. A Rev B SDK with USB audio needs that table.

**MIDI inside a composite works**; the one widely reported failure (TinyUSB discussion
[#2162](https://github.com/hathach/tinyusb/discussions/2162)) was two classes given the
same endpoint address. Recent TinyUSB also fixed DWC2 data-cache alignment on the H7,
which matters with the H7's D-cache: use a current release.

**Bandwidth fits, even 4×4.** Rev B's port is the STM32H750's OTG_HS core on its internal
full-speed PHY (12 Mbit/s, Seed pins 36/37). Full speed gives periodic (audio) traffic at
most 90 % of each 1 ms frame, 1350 bytes:

| Audio | Bytes per frame each way | Share of the periodic budget | Left for bulk (MIDI, MSC) |
|---|---|---|---|
| 2 in / 2 out, 48 kHz, 24-bit | 294 | 46 % | ~880 kB/s |
| 4 in / 4 out, 48 kHz, 16-bit | 392 | 60 % | ~690 kB/s |
| **4 in / 4 out, 48 kHz, 24-bit** | 588 | **89 %** | ~290 kB/s |

(asynchronous: one extra sample per frame of headroom; 9 bytes of isochronous overhead per
transfer; the feedback endpoint included). All eight channels fit at 24-bit, MIDI is
untouched, and storage is slow only while audio is streaming.

**Storage and the instrument can't both own the card.** While the computer has the SD
card mounted as a drive, the firmware must not touch its file system. The usual answer,
and the proposal here: a **USB disk mode**. The instrument unmounts FatFs, offers MSC,
and remounts when the computer ejects the drive or the player leaves the mode. MSC isn't
in the composite at all outside that mode.

**DFU** stays with the Daisy bootloader's `extdfu` build (flashing over the external USB
pins), entered the bootloader's way at power-up. The app's composite device doesn't
affect it, since the bootloader runs before the app enumerates. To confirm on hardware.

### Recommendation

**TinyUSB, pinned as a submodule of `rodeo-sdk`, replacing libDaisy's USB entirely**
(the app never calls `UsbHandle` or `MidiUsbTransport`). A `rodeo_usb` module owns the
device: one composite configuration with **MIDI and CDC always on, UAC2 audio optional
per instrument (2×2 or 4×4), and MSC only in disk mode**. Alongside it, the interrupt
priority table from the community port, with audio highest. Start from the `a7v7` ports
as references (MIT) rather than as dependencies.

**Order, each step on hardware before the next:**

1. **MIDI + CDC** on a bare Seed's micro-USB (OTG_FS): replaces libDaisy's USB-MIDI, and
   proves the TinyUSB port, clocks and interrupt priorities. *Done.*
2. **USB audio, stereo 2×2**, still on the micro-USB (swapped ahead of the port move
   on 2026-10-18: it needs no extra hardware and is the hard part). *Done on macOS;
   Windows and Linux to try.*
3. **Move to the external pins** (OTG_HS, Seed pins 36/37): the Rev B port. Testable
   before Rev B with a USB breakout wired to a Seed.
4. **MSC disk mode**, first on a RAM disk, then on Rev B's SD card. *RAM disk done on
   macOS.*
5. **UAC2 4×4** on Rev B, both codecs.

**Step 1, 2026-10-18: works.** TinyUSB 0.21.0 is a submodule
at `rodeo-sdk/tinyusb`, and [`examples/usb_midi_cdc`](../examples/usb_midi_cdc/) is a
composite MIDI + CDC device on the Seed's micro-USB (OTG_FS, TinyUSB port 0): 67 KB of
flash, 13.6 KB of RAM, built with the SDK's usual rules. It uses libDaisy's `System`
rather than `DaisySeed`, because `DaisySeed` owns a `UsbHandle` that links libDaisy's
USB stack and its OTG_FS interrupt handler. Development VID/PID: pid.codes' test pair,
`1209:0001`; a product needs its own.

Tested on a Daisy Seed 2 against macOS 26.5.2: it enumerates as "Rodeo USB test" by DOT
RODEO (serial number from the chip's unique ID); CoreMIDI shows one source and one
destination; the Seed's notes arrive every 500 ms and a note sent to it comes straight
back; at the same time the serial port gives the banner, echoes, and answers `s`
(137 MIDI messages out, 13 in). macOS also sends a new MIDI device its MIDI-CI
discovery SysEx (`F0 7E 7F 0D 70 …`), which the echo returned, so SysEx passes both
ways. Not yet tried: Windows, Linux, unplug/replug cycles.

**Found for step 2:** libDaisy defines `OTG_HS_IRQHandler` (the external pins' core,
TinyUSB port 1) in `src/sys/system.cpp`, which every app links, so TinyUSB can't simply
define its own. Options: point the vector at TinyUSB at run time (copy the vector table
to RAM and set `SCB->VTOR`), or carry a one-line patch to libDaisy making that handler
weak. Decide when step 2 starts.

**USB audio, 2026-10-18: works on macOS.** [`examples/usb_device`](../examples/usb_device/)
(named `usb_audio` until disk mode joined it) is one composite device, "Rodeo": audio both ways, stereo, 48 kHz, 24-bit, asynchronous
(the Seed's codec clock leads, a feedback endpoint keeps the computer in step), plus
MIDI and the serial port; 101 KB of flash. Decisions and findings:

* **UAC1, not UAC2.** TinyUSB 0.21 (and its development branch) sends UAC2 feedback as
  16.16 in 4 bytes, which Windows needs and macOS rejects at full speed; UAC1's feedback
  is 10.14 in 3 bytes and every OS takes it. Rev B's port is full speed only, and UAC1
  at full speed carries 24-bit, multi-channel, two-way audio, so nothing is lost and
  TinyUSB needs no patch. TinyUSB's own `uac2_*` examples drop to UAC1 at full speed for
  the same reason.
* **The interrupt clash is solved** by pointing the vector at TinyUSB at run time: the
  vector table is copied to DTCM, the USB entry replaced, `SCB->VTOR` moved. That lets the
  app use `DaisySeed` (and its codec set-up) even though `DaisySeed` links libDaisy's
  own USB handler, and it is the same fix the external-pin core needs in step 3.
* **USB's interrupt priority is set below audio** (6 vs libDaisy's 0 for the SAI DMA).
* **TinyUSB's audio FIFOs overwrite their oldest data when full** and still report a
  full write, so the app checks the free space before writing and counts a real overrun.
* Measured on a Seed 2 against macOS 26.5: the codec runs exactly 1,000 callbacks a
  second; one IN packet per millisecond, 48 or 49 frames, 48.02 frames a millisecond on
  average (the Seed's crystal is ~0.07 % fast against the Mac's, which is what the
  asynchronous mode absorbs). **A 60 s full-duplex loopback** (440/660 Hz played to the
  Seed and recorded back) came back with **no glitches**, exact frequencies, the level to
  0.04 dB, and a 31 ms round trip (mostly the Mac's buffering). A few under/overruns
  happen at stream start and stop only.
* **Test-tool trap:** ffmpeg's macOS (`avfoundation`) audio capture drops about 32 frames
  every ~70 ms at 48 kHz. It looked like a Seed bug until a recording with Apple's own
  `AVAudioEngine` came back clean. Don't use ffmpeg to judge USB audio on a Mac.
* For development the test app reboots itself into the STM32's DFU bootloader on `B`
  over the serial port, so it can be reflashed without the buttons.

Not yet tried: Windows, Linux, the codec path on headphones, and 4×4.

**Disk mode on a RAM disk, 2026-10-18: works on macOS.** The same
[`examples/usb_device`](../examples/usb_device/) now has a disk mode: 16 MB of the Seed's
SDRAM is a FAT16 disk, formatted by FatFs at boot (with a README.TXT), and stands in for
Rev B's SD card. `d` on the serial port unmounts FatFs and re-enumerates the Seed as a
second configuration, "Rodeo (disk mode)" (PID `1209:0003`): serial + mass storage, no
audio or MIDI. Ejecting the disk on the computer (SCSI START STOP UNIT with eject) hands
it back: the Seed re-enumerates as the audio device, remounts, and reports every file
with its size, CRC-32 and a .TXT's first line on the serial port. 118 KB of flash, of
the 128 KB internal.

* **The model holds:** the computer and the instrument never have the file system at
  the same time, so there is no locking and nothing to corrupt. Switching costs a
  re-enumeration (about a second on macOS), which suits a mode the player chooses.
* **Separate PIDs per configuration.** The disk configuration uses its own PID so macOS
  doesn't apply a cached configuration of the other interface set. A product needs a
  PID for each mode, or a single configuration that is the same in both.
* Tested on a Seed 2 against macOS 26.5.2: macOS mounted "NO NAME" (16.8 MB, FAT16);
  README.TXT read back; a text file and 300,000 bytes of random data written from the Mac
  came back to the Seed with matching CRC-32s after the eject, and survived a second
  round trip. After each one, a 10 s full-duplex loopback was still glitch-free.
* `n` on the serial port also leaves disk mode, but pulls the disk out from under the
  computer (macOS warns that it wasn't ejected properly). The instrument's own way out
  should wait for the eject, or say on its screen that the player must eject first.
* **Timestamp trap:** ST's `diskio.c` has a weak `get_fattime` that returns 0, which
  macOS shows as 1969. With no RTC, the app defines a strong one with a fixed date.
* macOS writes `._*` AppleDouble files and `.fseventsd` to the disk; the report skips
  names starting with a dot, and the instrument's file browser should too.

Next for disk mode: the SD card on Rev B (bring-up step `usb_storage`), where read and
write go through the SDMMC driver instead of `memcpy`, at full speed's ~1 MB/s. Not yet
tried: Windows, Linux.

Risks to watch: the feedback-format split (solved once, must stay solved across TinyUSB
updates); USB interrupt load next to the satellite chain and audio; FIFO sizing on the
DWC2 core for 588-byte isochronous packets each way; Linux, which nobody has tried yet.

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
| 08 | `usb_midi` | USB-MIDI + serial through the power board, on TinyUSB | USB-C power board |
| 09 | `usb_storage` | The SD card as a USB drive in disk mode | an SD card |
| 10 | `usb_audio` | UAC1, 2×2 then 4×4 across both codecs, on macOS, Windows and Linux | the hardest piece; last |

Plus the satellites' own bring-up in `rodeo-satellite/`: kind detection from the ID
resistor, a pot bank and an encoder bank answering DISCOVER and SCAN, then the chain.

## Work that can start before the boards arrive

1. ~~**Freeze Rev A**~~ done 2026-10-18 (below).
2. ~~**`board.h` for Rev B**~~ done 2026-10-19: `rodeo-sdk/common/board.h`, host-tested
   against libDaisy's pin table. Found a second shared interrupt line (A7/CV1).
3. ~~**`sat_frame.h` and its host tests**~~ done 2026-10-19: plain C, so the satellites
   include it too; tests play both ends down to a simulated ring. Corrected the
   contract's pipelining lag (5 byte times a satellite, 17 for a bank, not 4).
4. **`rodeo-satellite/`** on ch32v003fun: the logic is written and host-tested, and the
   firmware builds (6.5 KB). Still to do: flash and test it on a CH32V003 dev board
   with a WCH-LinkE (not yet bought).
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
* USB: TinyUSB recommended (above); to confirm with step 1 on a bare Seed.
* Porting rodeo-002: its 16 pots and its encoders move from the Rev A ADCs and the hub
  onto satellites (four pot banks and two encoder banks, say), and its control map gets
  rewritten for that.
