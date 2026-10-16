#pragma once
//
// rodeo-001 — INSTANCE control map (channel -> function binding).
//
// This header is the rodeo-001 instrument's channel->function contract. It is
// deliberately NOT in the generic rodeo-motherboard `common/board.h` (that stays
// pin-map/topology only); a specific instrument owns its own control semantics.
// Human source of truth: instruments/rodeo-001/docs/RODEO_CONTROL_MAP.md (frozen 2026-08-23).
//
// Voice v2 = "a less-complicated multi-engine poly": 12-voice, subtractive + FM + formant
// oscillator engines, poly SVF, Filter ADSR + Amp ADSR + 1 LFO (single dest).
//
// Channel index convention (from board.h): ch = chip*12 + AINindex.
//   ch  0..11 -> chip A = U3 (CS seed::D25) -> connectors J6..J17
//   ch 12..23 -> chip B = U4 (CS seed::D26) -> connectors J18..J29
//
#include <cstdint>
#include "board.h"

namespace rodeo001
{
// --- Analog pots: 22 live channels on the two ADS114S08 ADCs ----------------
// ch0 (J6) and ch3 (J9) are SPARE — the OSC1/OSC2 Wave selectors moved to the
// encoder-hub (slots 7/8), freeing those two ADS headers for future CV/mod.
namespace pot
{
constexpr int kOsc1Timbre   = 1;  // J7  model-dependent: PW / FM-index / formant-pos
constexpr int kOsc1Spread   = 2;  // J8
constexpr int kOsc2Timbre   = 4;  // J10 model-dependent: PW / FM-index / formant-pos
constexpr int kOsc2Spread   = 5;  // J11
constexpr int kOsc1Level    = 6;  // J12
constexpr int kOsc2Level    = 7;  // J13
constexpr int kNoiseLevel   = 8;  // J14
constexpr int kCutoff       = 9;  // J15
constexpr int kResonance    = 10; // J16
constexpr int kFilterEnvAmt = 11; // J17
constexpr int kVowel        = 12; // J18 formant/vowel filter (complementary to formant osc)
constexpr int kFiltEnvA     = 13; // J19
constexpr int kFiltEnvD     = 14; // J20
constexpr int kFiltEnvS     = 15; // J21
constexpr int kFiltEnvR     = 16; // J22
constexpr int kAmpEnvA      = 17; // J23
constexpr int kAmpEnvD      = 18; // J24
constexpr int kAmpEnvS      = 19; // J25
constexpr int kAmpEnvR      = 20; // J26
constexpr int kLfoRate      = 21; // J27
constexpr int kLfoAmount    = 22; // J28
constexpr int kMasterVol    = 23; // J29
} // namespace pot

// ADC scan mask: all 24 populated EXCEPT ch0/ch3 (spare). 22 channels scanned.
constexpr uint32_t kSpareMask     = (1u << 0) | (1u << 3);
constexpr uint32_t kPopulatedMask = 0x00FFFFFFu & ~kSpareMask;

// --- Digital cycle-buttons (momentary-to-GND, STM32 internal pull-up) --------
// FROZEN 2026-08-23, amended 2026-09-13 (RODEO_CONTROL_MAP.md): 5 buttons on
// DIG_2..6 = J31..J35 = Seed pins 3..7 (the SDMMC digital-only pins). DIG_1/J30
// is SPARE: hard-sync moved to a press on the OSC2 Octave encoder (see
// hub::kOsc2SyncPress). Signal-flow order. Each is a
// `rodeo-button` momentary-to-GND with 100 nF debounce; idle-high via the STM32
// internal pull-up (GPIO::Pull::PULLUP), so pressed reads LOW (active-low).
enum class Button
{
    kFilterType, // DIG_2/J31 — cycle SVF mode: LP / BP / HP / notch
    kKeytrack,   // DIG_3/J32 — cycle filter keytrack amount preset
    kLfoShape,   // DIG_4/J33 — cycle LFO waveform
    kLfoDest,    // DIG_5/J34 — cycle LFO single destination
    kVoiceMode,  // DIG_6/J35 — cycle poly / mono / unison
    kCount
};

// Button -> digital connector (physical Seed pin). Index by Button enum. The
// libDaisy seed::Dxx Pin is bound in target code (main.cpp), per board.h.
constexpr int kButtonDig[static_cast<int>(Button::kCount)] = {
    board::pin::kDig2, // kFilterType
    board::pin::kDig3, // kKeytrack
    board::pin::kDig4, // kLfoShape
    board::pin::kDig5, // kLfoDest
    board::pin::kDig6, // kVoiceMode
};

// --- Encoder-hub @0x30 (9-slot, detented, OLED feedback) ---------------------
// Firmware binds these slot indices; slot<->param table LOCKED 2026-08-21,
// amended 2026-09-13 (Master tune removed; OSC2 Octave press = hard-sync).
// Slots are 1-based in the doc; kept 1-based here to match the wire contract.
namespace hub
{
constexpr int kAddr        = board::i2c::kEncoderHubAddr; // 0x30
constexpr int kOsc2Detune  = 1;
constexpr int kUnisonDetune = 2;
constexpr int kGlide       = 3;
// slot 4 = spare (was Master tune, removed 2026-09-13 — left as a gap, not re-packed)
constexpr int kOsc1Octave  = 5;
constexpr int kOsc2Octave  = 6;
constexpr int kOsc2SyncPress = kOsc2Octave; // PRESS on slot 6 toggles OSC2 hard-sync
constexpr int kOsc1Model   = 7; // Option A: encoder steps OSC1 model list (Saw/Pulse/Tri/Sine/FM/Formant)
constexpr int kOsc2Model   = 8; // Option A: encoder steps OSC2 model list (Saw/Pulse/Tri/Sine/FM/Formant)
// slot 9 = spare
} // namespace hub

} // namespace rodeo001
