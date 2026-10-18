// Rodeo ADC — Layer 2 scanner. See rodeo_adc.h.

#include "rodeo_adc.h"
#include "adc_convert.h" // pure value pipeline (host-testable; see common/tests/)

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
    autorange_      = cfg.autorange;

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

        // Fold the fresh reading into this channel's learned travel, so a knob
        // swept to its stops teaches GetNorm() where its real endpoints are.
        if(autorange_)
            span_[ch].Observe(ClampAdcCode(cache_[ch]));

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
    return ClampAdcCode(cache_[ch]); // clamp unipolar negative tail (adc_convert.h)
}

float RodeoAdc::GetVolts(uint8_t ch) const
{
    return AdcCodeToVolts(GetCode(ch), Ads114s08::kVref);
}

float RodeoAdc::GetNorm(uint8_t ch) const
{
    if(ch >= kNumChannels)
        return 0.0f;
    // GetCode already clamps the low end to 0; the span maps the rest to 0..1.
    return AdcCodeToNormSpan(GetCode(ch), span_[ch]);
}

const PotSpan &RodeoAdc::GetSpan(uint8_t ch) const
{
    static const PotSpan kFallback;
    return ch < kNumChannels ? span_[ch] : kFallback;
}

void RodeoAdc::SetSpan(uint8_t ch, const PotSpan &span)
{
    if(ch < kNumChannels)
        span_[ch] = span;
}

} // namespace rodeo
