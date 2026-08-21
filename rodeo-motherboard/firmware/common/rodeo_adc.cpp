// Rodeo ADC — Layer 2 scanner. See rodeo_adc.h.

#include "rodeo_adc.h"

using namespace daisy;

namespace rodeo
{
namespace
{
// Chip chip-select pins (board.h: U3 = seed::D25, U4 = seed::D26).
inline Pin ChipCsPin(uint8_t chip)
{
    return chip == 0 ? seed::D25 : seed::D26;
}
} // namespace

uint8_t RodeoAdc::NextPopulatedAin(uint8_t chip, uint8_t from_ain) const
{
    for(uint8_t i = 0; i < kChannelsPerChip; ++i)
    {
        uint8_t ain = (from_ain + i) % kChannelsPerChip;
        uint8_t ch  = static_cast<uint8_t>(chip * kChannelsPerChip + ain);
        if(populated_mask_ & (1u << ch))
            return ain;
    }
    return 0xFF; // none populated on this chip
}

bool RodeoAdc::Init()
{
    return Init(Config{});
}

bool RodeoAdc::Init(const Config &cfg)
{
    populated_mask_ = cfg.populated_mask;
    settle_us_      = cfg.settle_us;

    spi_.Init(Ads114s08::DefaultSpiConfig());

    // Park EVERY chip's CS idle-high before probing ANY of them. The CS nets
    // have no pull-ups, so a not-yet-initialized sibling's CS floats; if it
    // drifts low it selects that chip and both drive the shared MISO during the
    // first chip's presence read -> contention -> the first-initialized chip
    // spuriously fails presence. This pre-pass makes the probe order-safe.
    for(uint8_t c = 0; c < kNumChips; ++c)
        adc_[c].InitCsIdle(ChipCsPin(c));

    for(uint8_t c = 0; c < kNumChips; ++c)
    {
        present_[c] = adc_[c].Init(&spi_, ChipCsPin(c));
        uint8_t first = NextPopulatedAin(c, 0);
        active_[c]    = present_[c] && (first != 0xFF);
        if(active_[c])
        {
            cur_ain_[c] = first;
            adc_[c].SelectChannel(first); // begin converting the first channel
        }
    }

    t_start_us_ = System::GetUs();
    return present_[0] || present_[1];
}

void RodeoAdc::Update()
{
    // Non-blocking: wait out the settle window (unsigned arithmetic handles the
    // ~71-min µs-counter wrap correctly).
    if((System::GetUs() - t_start_us_) < settle_us_)
        return;

    for(uint8_t c = 0; c < kNumChips; ++c)
    {
        if(!active_[c])
            continue;

        // The channel selected last window has now settled — read it.
        uint8_t ch  = static_cast<uint8_t>(c * kChannelsPerChip + cur_ain_[c]);
        cache_[ch]  = adc_[c].ReadData();

        // Advance to the next populated channel on this chip and start it.
        uint8_t next = NextPopulatedAin(c, (cur_ain_[c] + 1) % kChannelsPerChip);
        cur_ain_[c]  = next; // != 0xFF: active chip has >=1 populated channel
        adc_[c].SelectChannel(next);
    }

    t_start_us_ = System::GetUs();
}

bool RodeoAdc::ChannelPopulated(uint8_t ch) const
{
    return ch < kNumChannels && (populated_mask_ & (1u << ch));
}

uint16_t RodeoAdc::GetCode(uint8_t ch) const
{
    if(ch >= kNumChannels)
        return 0;
    int16_t v = cache_[ch];
    return v < 0 ? 0 : static_cast<uint16_t>(v); // clamp unipolar negative tail
}

float RodeoAdc::GetVolts(uint8_t ch) const
{
    return (static_cast<float>(GetCode(ch)) / 32767.0f) * Ads114s08::kVref;
}

float RodeoAdc::GetNorm(uint8_t ch) const
{
    float n = static_cast<float>(GetCode(ch)) / 32767.0f;
    if(n > 1.0f)
        n = 1.0f;
    return n; // GetCode already clamps the low end to 0
}

} // namespace rodeo
