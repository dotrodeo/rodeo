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
constexpr int kNumAdcChannels = 24; // 2 chips x 12 single-ended channels
constexpr int kNumAdcPerChip  = 12;
constexpr int kNumChips       = 2;
constexpr int kNumDigitalIo   = 12; // DIG_1..12 (free for buttons/gates)

// --- Audio configuration --------------------------------------------------
constexpr int   kBlockSize  = 32;      // 0.67 ms @ 48 kHz; tune toward 16 later
constexpr float kSampleRate = 48000.0f;
} // namespace board
