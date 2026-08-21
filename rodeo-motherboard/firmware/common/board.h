#pragma once
//
// Rodeo 001 — board pin map & platform constants.
//
// Host-safe: this header contains ONLY plain constants (no libDaisy types), so
// it can be included by native host unit tests as well as target firmware.
// The actual libDaisy `Pin` bindings live in target-only code that uses the
// `seed::Dxx` numbers documented here.
//
// Source of truth: kicad/rodeo-001.kicad_pcb, verified against the Daisy Seed
// (A1) pin functions. See docs/architecture.md.
//
namespace board
{
// --- Daisy Seed header pin numbers (physical A1 pins) ---------------------
namespace pin
{
// SPI1 to both ADS114S08 ADCs (series-R isolated). VERIFIED correct wiring.
constexpr int kSpiSck  = 9;  // seed::D8
constexpr int kSpiMiso = 10; // seed::D9  (PB4)  <- ADC DOUT
constexpr int kSpiMosi = 11; // seed::D10 (PB5)  -> ADC DIN
constexpr int kAdc1Cs  = 32; // seed::D25 (PA0)  U3 chip-select (soft NSS)
constexpr int kAdc2Cs  = 33; // seed::D26 (PD11) U4 chip-select (soft NSS)

// MIDI (USART1). RX is opto-isolated in; TX drives MIDI OUT / soft-thru.
constexpr int kMidiTx = 14; // seed::D13
constexpr int kMidiRx = 15; // seed::D14

// Audio codec (single-ended line level, to/from the 3.5 mm jacks).
constexpr int kAudioInL  = 16;
constexpr int kAudioInR  = 17;
constexpr int kAudioOutL = 18;
constexpr int kAudioOutR = 19;

// Carrier status LED (D5), active high.
constexpr int kStatusLed = 28; // seed::D21 (PC4)
} // namespace pin

// --- Control-surface topology --------------------------------------------
// All analog controls (pots AND resistor-ladder rotary selectors) are read
// through the two external ADS114S08 SPI ADCs — never the Daisy's onboard ADC.
//
// Physical channel index convention (contract — keep in lockstep with
// RODEO_CONTROL_MAP.md, the source of truth for channel -> connector -> function):
//     ch = chip*12 + AINindex
//     ch  0..11  -> chip A = U3 (kAdc1Cs, seed::D25) AIN0..AIN11 -> J6..J17
//     ch 12..23  -> chip B = U4 (kAdc2Cs, seed::D26) AIN0..AIN11 -> J18..J29
// Every connector is monotonic (ch0->J6 … ch23->J29), verified against KiCad.
//
// Single-ended reads use MUXN = AINCOM, which is board-tied to GND on both
// chips (verified in KiCad) — that is what gives the full 0..3.3 V span.
//
// Pin-alias guard: AIN6/AIN7 also alias REFP1/REFN1, and AIN8..AIN11 alias
// GPIO0..GPIO3. We use REFP0/REFN0 and no GPIO, so all 12 inputs/chip are free —
// but NEVER set REFSEL=01 or configure those pins as GPIO, or channels 6..11
// silently disappear.
constexpr int kNumAdcChannels = 24; // 2 chips x 12 single-ended channels
constexpr int kNumAdcPerChip  = 12;
constexpr int kNumChips       = 2;
constexpr int kNumDigitalIo   = 12; // DIG_1..12 (free for buttons/gates)

// --- Frozen ADC channel -> control binding (Path B) ----------------------
// FROZEN 2026-08-15 with Honey (panel L->R order) + Bumble (J6..J29 map).
// Source of truth: RODEO_CONTROL_MAP.md. Physical left-to-right panel order
// IS the channel order, 1:1 — do not renumber without re-freezing there.
//
// All 24 are read as single-ended pots EXCEPT ch0/ch3, which are 4-position
// resistor-ladder rotary switches (rodeo-rotary-switch): same ADC read, but
// quantize GetNorm() into 4 bands rather than treating as continuous.
//
// U3 = chip A (kAdc1Cs, seed::D25) -> J6..J17:
enum AdcCtrl
{
    // -- U3 / ch0..11 / J6..J17 --
    kOsc1Wave    = 0,  // J6  rotary (4-pos ladder)
    kOsc1Shape   = 1,  // J7  pot (Shape/PW)
    kOsc1Spread  = 2,  // J8  pot (supersaw spread)
    kOsc2Wave    = 3,  // J9  rotary (4-pos ladder)
    kOsc2Shape   = 4,  // J10 pot (Shape/PW)
    kOsc2Spread  = 5,  // J11 pot
    kMixOsc1     = 6,  // J12 pot (osc1 level)
    kMixOsc2     = 7,  // J13 pot (osc2 level)
    kMixNoise    = 8,  // J14 pot (noise level)
    kFilterCutoff    = 9,   // J15 pot
    kFilterReso      = 10,  // J16 pot
    kFilterEnvAmt    = 11,  // J17 pot
    // -- U4 / ch12..23 / J18..J29 -- (U3|U4 boundary lands here, no penalty)
    kFilterVowel = 12, // J18 pot (formant morph)
    kFiltEnvA    = 13, // J19 pot
    kFiltEnvD    = 14, // J20 pot
    kFiltEnvS    = 15, // J21 pot
    kFiltEnvR    = 16, // J22 pot
    kAmpEnvA     = 17, // J23 pot
    kAmpEnvD     = 18, // J24 pot
    kAmpEnvS     = 19, // J25 pot
    kAmpEnvR     = 20, // J26 pot
    kLfoRate     = 21, // J27 pot
    kLfoAmt      = 22, // J28 pot
    kMasterVolume = 23, // J29 pot
};
// The 2 Wave rotaries sit on ch0 (J6) and ch3 (J9), both on U3. Enum values
// ARE the channel index -> pass directly to RodeoAdc::GetCode/GetVolts/GetNorm.
//
// NOT on the ADC (handled elsewhere — do not look for these channels here):
//   Enum selectors  -> digital GPIO cycle-buttons: Filter Type, Keytrack,
//                      LFO Shape, LFO Dest, Voice Mode, + hard-sync toggle.
//   Fine trims/oct  -> I2C encoder-banks: 0x30 {OSC2 detune, unison detune,
//                      glide, master tune}, 0x31 {OSC1 octave, OSC2 octave}.
//   Pitch/mod       -> MIDI (desktop synth, no wheels on panel).

// --- Audio configuration --------------------------------------------------
constexpr int   kBlockSize  = 32;      // 0.67 ms @ 48 kHz; tune toward 16 later
constexpr float kSampleRate = 48000.0f;
} // namespace board
