#pragma once
//
// Rodeo 001 — board pin map & platform constants.
//
// Host-safe: this header contains ONLY plain constants (no libDaisy types), so
// it can be included by native host unit tests as well as target firmware.
// The actual libDaisy `Pin` bindings live in target-only code that uses the
// `seed::Dxx` numbers documented here.
//
// Source of truth: kicad/rodeo-motherboard.kicad_pcb, verified against the Daisy Seed
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

// I2C1 to the Qwiic/STEMMA-QT header (J42): SSD1309 OLED @ 0x3C + encoder
// hub(s) @ 0x30. NO on-board pull-ups (Issue 2) — the module/breakout or a
// harness stuffing must supply them. These match libDaisy's I2C_1 defaults.
constexpr int kI2cScl = 12; // seed::D11 (PB8)
constexpr int kI2cSda = 13; // seed::D12 (PB9)
} // namespace pin

// I2C addresses on the J42 bus.
namespace i2c
{
constexpr int kOledAddr       = 0x3C; // SSD1309 128x64, feedback-only display
constexpr int kEncoderHubAddr = 0x30; // rodeo-encoder-hub (STM32C031C6)
} // namespace i2c

// --- Control-surface topology --------------------------------------------
// All analog controls (pots) are read through the two external ADS114S08 SPI
// ADCs — never the Daisy's onboard ADC.
//
// Physical channel index convention (fixed motherboard wiring — the ch ->
// connector map below is a hardware fact of this board. Which *control* lands
// on each connector is a per-instance decision, defined by the instance build,
// NOT here — see the note after the ADC constants):
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

// --- Channel -> control binding is PER-INSTANCE, not here ----------------
// This header is generic rodeo-motherboard boilerplate: pin map, ADC/I2C
// topology, connector wiring. It deliberately does NOT assign controls to
// channels (which pot is "OSC1 cutoff", which encoder slot is "OSC1 wave",
// etc.) — that is a specific instrument's concern.
//
// A build like rodeo-001 defines its own channel->function map in its own
// implementation, with RODEO_CONTROL_MAP.md as the human source of truth.
// Keep application semantics out of this file so the boilerplate stays reusable
// across instruments.

// --- Audio configuration --------------------------------------------------
constexpr int   kBlockSize  = 32;      // 0.67 ms @ 48 kHz; tune toward 16 later
constexpr float kSampleRate = 48000.0f;
} // namespace board
