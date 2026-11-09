// Rodeo SDK version.
//
// Scoped to the SDK and nothing else. The repository holds the boards and the
// instruments too, and each of those versions itself, so tags are written
// `rodeo-sdk/v0.1.0` -- component first, so a listing groups by component rather
// than by number. Same convention as `rodeo-002/v5.0.0`.
//
// An instrument can check what it is building against:
//
//     static_assert(RODEO_SDK_AT_LEAST(0, 1, 0), "this app needs a newer SDK");
//
// Semantic versioning, and 0.x ON PURPOSE. The SDK's job is to be consumed by
// instruments, and with two of them built the shape is clear but the seams are
// not all settled. One is known and deliberate as of v0.1.0:
//
//   THE POT PIPELINE IS DIVERGENT. `adc_convert.h` / `RodeoAdc::GetNorm`
//   normalise a pot by LEARNING its travel. rodeo-002 abandoned that approach
//   on hardware -- a learned minimum is only ever as good as the worst sample
//   ever taken, and one bad reading sets the bottom of the range for good -- and
//   conditions its pots itself with fixed per-wiring ranges inset by a dead
//   zone (`instruments/rodeo-002/firmware/common/panel_pot.h`). It reads GetVolts() and
//   does its own mapping. The examples still demonstrate GetNorm, so an
//   instrument started from one of them inherits the approach that was tried
//   and rejected. Reconciling the two is the main thing standing between this
//   and a 1.0.
//
// v0.1.0 (2026-10-01) is the first version worth a number: the point at which
// the SDK stopped being "the motherboard's firmware folder" and became the thing
// both instruments are built on. In it:
//
//   * Board and drivers: board.h pin map, the ADS114S08 pair behind RodeoAdc's
//     24-channel pipelined scanner, the encoder-hub master, a bulk SSD130x I2C
//     transport that sends a page per transaction instead of a byte.
//   * Pure host-tested logic, no libDaisy and no hardware in any of it:
//     adc_convert.h, pot_ui.h, rodeo_dsp.h, rodeo_scope.h, rodeo_screen.h. The
//     firmware calls THESE, so the host tests guard the real path.
//   * tools/screens: an instrument's real OLED screens rendered on a laptop,
//     with its real fonts and its real draw calls.
//   * Nine bring-up examples, a copy-to-start template, and vendored
//     libDaisy/DaisySP.
//
// 56 host tests at this tag (`common/tests`).
//
// v0.2.0 (2026-10-18) is THE LAST REV A RELEASE, tagged before the Rev B rewrite
// so rodeo-001 and rodeo-002 have a fixed SDK to build against. Since v0.1.0:
//
//   * examples/10_audio_levels: a bench tool for measuring the Seed's audio
//     input and output levels (2026-10-07).
//   * Paths follow the repository's 2026-10-16 reorganisation (the hub's
//     register map is now under rev-a/rodeo-encoder-hub/).
//
// Still Rev A only: board.h is the Rev A motherboard's pin map, and RodeoAdc
// scans its two on-board ADS114S08s. 56 host tests, unchanged.
//
// Unreleased, towards v0.3.0 (the first Rev B release):
//
//   * board.h is the Rev B pin map (2026-10-19): the satellite chain, the
//     second codec, SD, SPI to the expansion header, USB, and the ten I/O
//     connectors by silkscreen. board_daisy.h converts its pins for libDaisy.
//     Host tests check it against libDaisy's own pin table.
//   * Removed with the Rev A map: examples 01-10, encoder_hub.*,
//     rodeo_encoders.*, status_led.h. They stay in rev-a/rodeo-sdk/.
//   * RodeoAdc is not yet ported (its chip-selects are SAI2 pins on Rev B).
//   * sat_frame.h: the satellite chain's wire format (2026-10-19), plain C so
//     the satellite firmware includes it too. Host tests play both ends, down
//     to a simulated ring; 78 host tests in all.
//   * sat_chain.h / sat_chain_port.h: the Seed's chain driver (2026-10-19). The
//     engine is pure and host-tested against a simulated ring (discovery,
//     scanning, breaks found and repaired); the port is USART3 on DMA1_Stream7
//     and DMA2_Stream5. examples/sat_chain runs it on a bare Seed: as a
//     loop-back on a Seed 2 it scanned at 250/s with none bad, and found and
//     recovered from a pulled wire. Not yet run with satellites. 90 host tests.
//   * io_plan.h / io.h: the ten I/O connectors (2026-10-19): digital in/out,
//     analog in on the Seed's ADC, CV out on its DAC, the configuration checked
//     before any pin is touched. examples/io_connectors self-tests on a bare
//     Seed 2 with two jumpers: DAC read back within 0.1 %, digital loop passes.
//     95 host tests.
//   * examples/usb_midi_cdc and usb_device: TinyUSB on a bare Seed.
//   * audio_4x4.h: both codecs in one callback, four channels in and out
//     (2026-10-26). Daisy Patch's dual-SAI setup with Rev B's pins; releases
//     the CS4272's reset once SAI2's clocks run. rodeo_dsp.h gains ToneMeter.
//     examples/audio_4x4 (a tone per output, each input measured) and
//     examples/sd_card (1-bit SDMMC: write, read back, check, time) build but
//     wait for the motherboard. 99 host tests.
//   * battery.h / max17048.h: the fuel gauge (2026-10-26); decoding
//     host-tested. Bring-up tests for every Rev B board, numbered as in the
//     plan: blink_power, midi, oled_i2c and spi_header are new; io_connectors
//     reads every free analog input; sat_chain names each satellite, shows
//     its controls and drives its LEDs from them. 105 host tests.
//   * USB on either port (2026-10-26): common/tinyusb.mk + usb_port.h pick
//     the Seed's micro-USB (RODEO_USB=fs) or Rev B's USB-C, OTG_HS on its
//     full-speed PHY (RODEO_USB=hs); UsbPortInit() sets up pins, clocks and
//     the interrupt for either. usb_midi_cdc and usb_device use it.
//     examples/usb_storage (the SD card as a USB drive, in a disk mode) and
//     examples/usb_audio (UAC1 4x4, 24-bit, both codecs) build for Rev B but
//     wait for the motherboard.
//   * host.h / host_usb.h: the host protocol (2026-11-09), from rodeo-demo's
//     MIDI protocol: a firmware's parameter table, state, chain and dfu over
//     USB-MIDI SysEx, so the console shows any firmware. Protocol 2. Pure and
//     host-tested; about 6 KB of code; RODEO_HOST=0 compiles it out.
//     examples/host ran on Rev B with four satellites (2026-11-09): every
//     command, a stream through full pot sweeps, dfu. 117 host tests.

#pragma once

namespace rodeo
{
constexpr int  kSdkVersionMajor = 0;
constexpr int  kSdkVersionMinor = 2;
constexpr int  kSdkVersionPatch = 0;
constexpr char kSdkVersionString[] = "0.2.0";

} // namespace rodeo

/** A version as one comparable number: 0.2.0 is 200, 1.0.0 is 10000. */
#define RODEO_SDK_VERSION_NUM(major, minor, patch) \
    ((major) * 10000 + (minor) * 100 + (patch))

/** This SDK's version, as RODEO_SDK_VERSION_NUM gives it. */
#define RODEO_SDK_VERSION                                  \
    RODEO_SDK_VERSION_NUM(rodeo::kSdkVersionMajor,         \
                          rodeo::kSdkVersionMinor,         \
                          rodeo::kSdkVersionPatch)

/** True when the SDK is at least the version given. Usable in a static_assert,
 *  which is the point: an app that needs something new should fail to build
 *  rather than fail on the bench. */
#define RODEO_SDK_AT_LEAST(major, minor, patch) \
    (RODEO_SDK_VERSION >= RODEO_SDK_VERSION_NUM(major, minor, patch))
