#pragma once
//
// Rodeo ADC — Layer 2 scanner over the two ADS114S08 chips (U3/U4).
//
// Owns the shared SPI1 handle + two Ads114s08 device drivers and runs a
// non-blocking, pipelined round-robin scan across all populated channels,
// caching the latest value per channel. The synth app calls Init() once, pumps
// Update() from the main loop, and reads GetCode/GetVolts/GetNorm — it never
// touches SPI or the chip register map.
//
// Channel index convention (matches board.h + RODEO_CONTROL_MAP.md):
//     ch = chip*12 + AINindex
//     ch  0..11 -> chip A = U3 (CS seed::D25) AIN0..AIN11
//     ch 12..23 -> chip B = U4 (CS seed::D26) AIN0..AIN11
//
// Pipelining: the two chips convert in PARALLEL (independent converters, shared
// bus). Each settle window services one channel on EACH chip, so a 24-channel
// scan is two parallel 12-channel scans (~72 Hz/ch at 1000 SPS). See
// docs/RODEO_ADC_LIB_DESIGN.md.
//
#include "daisy_seed.h"
#include "board.h"
#include "adc_convert.h" // PotSpan + the value pipeline GetNorm delegates to
#include "ads114s08.h"

namespace rodeo
{
class RodeoAdc
{
  public:
    static constexpr uint8_t kNumChannels = 24; // board::kNumAdcChannels
    static constexpr uint8_t kNumChips    = 2;
    static constexpr uint8_t kChannelsPerChip = 12;

    struct Config
    {
        // bit i set => channel i is populated and gets scanned. Default: all 24.
        uint32_t populated_mask = 0x00FFFFFF;
        // per-hop settle window (µs). Default: 1000 SPS low-latency (1/1000 s +
        // ~156 µs overhead ≈ 1156 µs) + margin. Lower the data rate -> raise this.
        uint32_t settle_us = 1200;
        // Learn each channel's real travel as it is swept, so GetNorm() spans a
        // true 0..1 despite the satellite's 1 kΩ top-rail resistor (see PotSpan
        // in adc_convert.h). Turn off for absolute-referenced inputs (CV), where
        // the reading is a voltage rather than a knob position.
        bool autorange = true;
    };

    // Inits the shared SPI bus and both chips (each presence-checked), seeds the
    // scan cursors, and starts the first conversions. Returns true if at least
    // one chip answered. A missing chip's 12 channels simply never update.
    bool Init(const Config &cfg);
    bool Init(); // default Config (all 24 channels, 1000 SPS)

    // Pump from the main loop. Non-blocking: returns immediately until the
    // current settle window elapses, then reads both chips and advances.
    void Update();

    // latest value for channel ch (0..23). Codes are clamped to [0, 32767]:
    // unipolar single-ended pots can dip a few codes negative at the fully-off
    // position, which must not wrap to full-scale (design-doc value pipeline).
    uint16_t GetCode(uint8_t ch) const;
    // TRUE measured voltage — never span-corrected, so bring-up examples can
    // still see what the pin is actually doing.
    float    GetVolts(uint8_t ch) const; // 0..3.3 V (ratiometric)
    // Control value, normalized across the channel's learned travel (see
    // PotSpan). This is the one that reaches a real 1.0 at the knob's stop.
    float    GetNorm(uint8_t ch) const;  // 0..1

    // Learned travel, for logging or for seeding from stored calibration.
    const PotSpan &GetSpan(uint8_t ch) const;
    void           SetSpan(uint8_t ch, const PotSpan &span);

    bool ChannelPopulated(uint8_t ch) const;
    bool ChipPresent(uint8_t chip) const
    {
        return chip < kNumChips ? present_[chip] : false;
    }

  private:
    // lowest populated AIN on `chip` at or after `from_ain` (wraps); returns
    // 0xFF if the chip has no populated channels.
    uint8_t NextPopulatedAin(uint8_t chip, uint8_t from_ain) const;

    daisy::SpiHandle spi_;
    Ads114s08        adc_[kNumChips];
    int16_t          cache_[kNumChannels] = {0}; // signed; clamp on read
    PotSpan          span_[kNumChannels];        // per-channel learned travel
    bool             autorange_           = true;
    uint8_t          cur_ain_[kNumChips]  = {0}; // AIN each chip is converting
    bool             present_[kNumChips]  = {false};
    bool             active_[kNumChips]   = {false}; // present AND has channels
    uint32_t         populated_mask_      = 0;
    uint32_t         settle_us_           = 1200;
    uint32_t         t_start_us_          = 0;
};

} // namespace rodeo
