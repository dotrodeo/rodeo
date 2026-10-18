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

#pragma once

namespace rodeo
{
constexpr int  kSdkVersionMajor = 0;
constexpr int  kSdkVersionMinor = 2;
constexpr int  kSdkVersionPatch = 0;
constexpr char kSdkVersionString[] = "0.2.0";

} // namespace rodeo

/** True when the SDK is at least the version given. Usable in a static_assert,
 *  which is the point: an app that needs something new should fail to build
 *  rather than fail on the bench. */
#define RODEO_SDK_VERSION_NUM(major, minor, patch) \
    ((major) * 10000 + (minor) * 100 + (patch))

#define RODEO_SDK_VERSION                                  \
    RODEO_SDK_VERSION_NUM(rodeo::kSdkVersionMajor,         \
                          rodeo::kSdkVersionMinor,         \
                          rodeo::kSdkVersionPatch)

#define RODEO_SDK_AT_LEAST(major, minor, patch) \
    (RODEO_SDK_VERSION >= RODEO_SDK_VERSION_NUM(major, minor, patch))
