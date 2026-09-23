// rodeo-002 — panel pot conditioning, including cables built the wrong way.
//
// A rodeo-potentiometer satellite feeds the pot's top leg through a 1 kOhm
// resistor with the bottom leg on ground, so wired correctly it sweeps
//
//     0.00 V  ->  3.3 * Rpot/(Rpot + 1k)  =  ~3.00 V for a 10k pot
//
// Built with a STRAIGHT cable instead of a reversed one, supply and ground swap
// and the SAME divider runs from the other end, so the travel becomes
//
//     3.3 * 1k/(Rpot + 1k)  ->  3.30 V   =  ~0.30 V .. 3.30 V
//
// Note what does and does not change. The RANGE shifts -- the pot can no longer
// reach 0 V, and it can now reach the full 3.3 V, which correct wiring cannot.
// The DIRECTION does not: which way the knob reads depends on the pot's pin
// orientation, and on rodeo-002's panel it comes out correct either way. An
// earlier version of this file inverted such channels and made every knob run
// backwards on hardware (2026-09-23) -- the range and the direction are
// independent, and only the range is wrong here.
//
// Nothing is harmed by the swap: the satellite is passive. Rebuilding 16 cables
// is a worse trade than rescaling here.
//
// WIRING IS DETECTED, NOT CONFIGURED. A correctly wired satellite CANNOT read
// above ~3.05 V -- the 1 kOhm resistor forbids it even for a +20% pot
// (3.3 * 12/13 = 3.046) -- so any channel ever seen above kSwappedThreshold is
// necessarily supply-swapped. Each channel then starts from that wiring's
// nominal travel and widens it from what it actually sees, so a knob reaches a
// true 0 and 1 without needing a full sweep first.

#pragma once

#include <cstdint>

#include "dsp_util.h"

namespace rodeo002
{
class PanelPots
{
  public:
    static constexpr int   kMaxChannels = 16;
    static constexpr float kVref        = 3.3f;
    /** A correct satellite tops out at ~3.05 V; above this, supply and ground
     *  are swapped (a straight cable on an analog run). */
    static constexpr float kSwappedThreshold = 3.15f;
    /** Pull in from each end so the last sliver of travel saturates. */
    static constexpr float kEdgeMargin = 0.02f;
    /** Nominal travel, correct wiring: 0 .. 3.3*Rpot/(Rpot+1k). */
    static constexpr float kNormalTop = 3.00f;
    /** Nominal travel, supply swapped: 3.3*1k/(Rpot+1k) .. 3.3. */
    static constexpr float kSwappedBottom = 0.30f;

    void Init()
    {
        for(int i = 0; i < kMaxChannels; i++)
        {
            ch_[i].lo      = 0.f;
            ch_[i].hi      = kNormalTop;
            ch_[i].swapped = false;
        }
    }

    /** Feed the TRUE measured voltage; returns the control value 0..1 with the
     *  orientation already applied. */
    float Normalize(int i, float volts)
    {
        if(i < 0 || i >= kMaxChannels)
            return 0.f;
        Channel& c = ch_[i];

        // Above what correct wiring can produce: supply and ground are
        // swapped. Sticky, and it re-seeds the travel to that wiring's nominal
        // range so the knob spans 0..1 straight away.
        if(volts > kSwappedThreshold && !c.swapped)
        {
            c.swapped = true;
            c.lo      = kSwappedBottom;
            c.hi      = kVref;
        }
        // Widen to what this pot actually does (tolerance moves the endpoint).
        if(volts < c.lo)
            c.lo = volts;
        if(volts > c.hi)
            c.hi = volts;

        const float lo = c.lo + kEdgeMargin;
        const float hi = c.hi - kEdgeMargin;
        const float n  = hi > lo ? (volts - lo) / (hi - lo) : 0.f;
        return fx::Clamp(n, 0.f, 1.f);
    }

    /** True once this channel has been seen above what correct wiring allows,
     *  i.e. its analog run was built with a straight cable. */
    bool IsSupplySwapped(int i) const
    {
        return i >= 0 && i < kMaxChannels && ch_[i].swapped;
    }
    /** Travel seen so far, for the bring-up dump. */
    float SpanSeen(int i) const
    {
        if(i < 0 || i >= kMaxChannels)
            return 0.f;
        const float s = ch_[i].hi - ch_[i].lo;
        return s > 0.f ? s : 0.f;
    }

  private:
    struct Channel
    {
        float lo      = 0.f;
        float hi      = kNormalTop;
        bool  swapped = false;
    };
    Channel ch_[kMaxChannels];
};

} // namespace rodeo002
