// Rodeo Encoders — Layer 2 scanner. See rodeo_encoders.h.

#include "rodeo_encoders.h"

using namespace daisy;

namespace rodeo
{
namespace
{
// Extract encoder `slot` (0..8) press bit from a LO/HI switch byte pair, using
// the shared header's bit layout (enc 0..7 -> LO bit0..7, enc 8 -> HI bit0).
inline bool SwitchBit(uint8_t lo, uint8_t hi, uint8_t slot)
{
    uint8_t byte = RODEO_HUB_SW_IS_LO(slot) ? lo : hi;
    return (byte & RODEO_HUB_SW_BIT(slot)) != 0;
}
} // namespace

bool RodeoEncoders::Init()
{
    return Init(Config{});
}

bool RodeoEncoders::Init(const Config &cfg)
{
    poll_interval_us_ = cfg.poll_interval_us;

    i2c_.Init(EncoderHub::DefaultI2cConfig(cfg.speed));

    uint8_t n_present = 0;
    for(uint8_t h = 0; h < kMaxHubs; ++h)
    {
        if(cfg.hub_mask & (1u << h))
            present_[h] = hub_[h].Init(&i2c_, HubAddress(h));
        else
            present_[h] = false;
        if(present_[h])
            ++n_present;
    }

    // Seed the round-robin cursor on the first present hub (if any).
    cur_hub_    = 0;
    t_last_us_  = System::GetUs();
    return n_present > 0;
}

uint8_t RodeoEncoders::NextPresentHub(uint8_t from) const
{
    for(uint8_t i = 0; i < kMaxHubs; ++i)
    {
        uint8_t h = static_cast<uint8_t>((from + i) % kMaxHubs);
        if(present_[h])
            return h;
    }
    return 0xFF;
}

void RodeoEncoders::ServiceHub(uint8_t hub)
{
    uint8_t tel[RODEO_HUB_TELEMETRY_LEN];
    if(!hub_[hub].ReadTelemetry(tel))
        return; // transient bus hiccup — leave caches, retry next sweep

    // Deltas 0x10..0x18 -> tel[0..8]; fold into the read-clear accumulator and
    // the running position, with int16 saturation on the accumulator.
    for(uint8_t s = 0; s < kEncPerHub; ++s)
    {
        int8_t d = static_cast<int8_t>(tel[s]);
        if(d == 0)
            continue;
        uint8_t idx = FlatIndex(hub, s);
        position_[idx] += d;
        int32_t acc = static_cast<int32_t>(delta_[idx]) + d;
        if(acc > 32767)
            acc = 32767;
        else if(acc < -32768)
            acc = -32768;
        delta_[idx] = static_cast<int16_t>(acc);
    }

    // Live switch level: 0x19 -> tel[9] (lo), 0x1A -> tel[10] (hi).
    const uint8_t off_lo = RODEO_HUB_REG_SW_STATE_LO - RODEO_HUB_REG_ENC_DELTA_BASE;
    uint8_t       lvl_lo = tel[off_lo];
    uint8_t       lvl_hi = tel[off_lo + 1];
    for(uint8_t s = 0; s < kEncPerHub; ++s)
        pressed_[FlatIndex(hub, s)] = SwitchBit(lvl_lo, lvl_hi, s);

    // Press-edge latches (0x20/0x21, read-to-clear): OR into the sticky per-slot
    // edge cache so a tap between polls survives until the app PopPress()es it.
    uint8_t pl_lo = 0, pl_hi = 0;
    if(hub_[hub].ReadPressLatch(&pl_lo, &pl_hi))
    {
        for(uint8_t s = 0; s < kEncPerHub; ++s)
            if(SwitchBit(pl_lo, pl_hi, s))
                press_edge_[FlatIndex(hub, s)] = true;
    }
}

void RodeoEncoders::Update()
{
    // Non-blocking: wait out the poll interval (unsigned math handles the µs
    // counter wrap correctly).
    if((System::GetUs() - t_last_us_) < poll_interval_us_)
        return;

    uint8_t h = NextPresentHub(cur_hub_);
    if(h == 0xFF)
        return; // no hubs present — nothing to poll

    ServiceHub(h);
    cur_hub_   = static_cast<uint8_t>((h + 1) % kMaxHubs);
    t_last_us_ = System::GetUs();
}

// --- friendly API: (hub, slot) forms --------------------------------------
int16_t RodeoEncoders::GetDelta(uint8_t hub, uint8_t slot)
{
    if(!ValidHubSlot(hub, slot))
        return 0;
    return GetDelta(FlatIndex(hub, slot));
}

int32_t RodeoEncoders::GetPosition(uint8_t hub, uint8_t slot) const
{
    if(!ValidHubSlot(hub, slot))
        return 0;
    return GetPosition(FlatIndex(hub, slot));
}

bool RodeoEncoders::IsPressed(uint8_t hub, uint8_t slot) const
{
    if(!ValidHubSlot(hub, slot))
        return false;
    return IsPressed(FlatIndex(hub, slot));
}

bool RodeoEncoders::PopPress(uint8_t hub, uint8_t slot)
{
    if(!ValidHubSlot(hub, slot))
        return false;
    return PopPress(FlatIndex(hub, slot));
}

// --- friendly API: flat-index forms ---------------------------------------
int16_t RodeoEncoders::GetDelta(uint8_t idx)
{
    if(idx >= kMaxEncoders)
        return 0;
    int16_t d   = delta_[idx];
    delta_[idx] = 0; // read-clear
    return d;
}

int32_t RodeoEncoders::GetPosition(uint8_t idx) const
{
    return idx < kMaxEncoders ? position_[idx] : 0;
}

bool RodeoEncoders::IsPressed(uint8_t idx) const
{
    return idx < kMaxEncoders ? pressed_[idx] : false;
}

bool RodeoEncoders::PopPress(uint8_t idx)
{
    if(idx >= kMaxEncoders)
        return false;
    bool e           = press_edge_[idx];
    press_edge_[idx] = false; // read-clear
    return e;
}

// --- topology --------------------------------------------------------------
bool RodeoEncoders::HubPresent(uint8_t hub) const
{
    return hub < kMaxHubs ? present_[hub] : false;
}

uint8_t RodeoEncoders::NumHubsPresent() const
{
    uint8_t n = 0;
    for(uint8_t h = 0; h < kMaxHubs; ++h)
        if(present_[h])
            ++n;
    return n;
}

} // namespace rodeo
