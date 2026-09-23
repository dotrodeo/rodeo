// rodeo-002 — panel pot conditioning, including cables built the wrong way.
//
// A rodeo-potentiometer satellite feeds the pot's top leg through a 1 kOhm
// resistor with the bottom leg on ground, so wired correctly it sweeps
//
//     0.00 V  ->  3.3 * Rpot/(Rpot + 1k)  =  ~3.00 V for a 10k pot
//
// Built with a STRAIGHT cable instead of a reversed one, supply and ground
// swap and the same divider runs the other way, so it sweeps
//
//     3.30 V  ->  3.3 * 1k/(Rpot + 1k)  =  ~0.30 V,  backwards.
//
// Nothing is harmed -- the satellite is passive -- but the pot reads inverted
// and never reaches zero. That is exactly what the panel showed on first
// assembly (2026-09-23), and rebuilding 16 cables is a worse trade than
// handling it here.
//
// ORIENTATION IS DETECTED, NOT CONFIGURED. A correctly wired satellite CANNOT
// read above ~3.05 V: the 1 kOhm resistor forbids it even for a +20% pot
// (3.3 * 12/13 = 3.046). So any channel ever seen above kInvertedThreshold is
// necessarily wired the other way, and there is no ambiguity to get wrong.
//
// Each channel also learns its own travel, because pot tolerance moves the
// endpoint; that is the same idea as the platform's PotSpan, but applied to the
// true measured voltage so it can express both orientations.

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
    /** A correct satellite tops out at ~3.05 V; above this the cable is
     *  straight where it should be reversed. */
    static constexpr float kInvertedThreshold = 3.15f;
    /** Ignore a learned span narrower than this -- the knob has not been swept
     *  far enough to trust it yet. */
    static constexpr float kMinSpan = 1.0f;
    /** Pull in from each end so the last sliver of travel saturates. */
    static constexpr float kEdgeMargin = 0.02f;

    void Init()
    {
        for(int i = 0; i < kMaxChannels; i++)
        {
            ch_[i].lo       = kVref;
            ch_[i].hi       = 0.f;
            ch_[i].inverted = false;
        }
    }

    /** Feed the TRUE measured voltage; returns the control value 0..1 with the
     *  orientation already applied. */
    float Normalize(int i, float volts)
    {
        if(i < 0 || i >= kMaxChannels)
            return 0.f;
        Channel& c = ch_[i];

        if(volts > kInvertedThreshold)
            c.inverted = true; // sticky: physically impossible when wired right
        if(volts < c.lo)
            c.lo = volts;
        if(volts > c.hi)
            c.hi = volts;

        // Not swept far enough to trust the learned span: assume the nominal
        // travel for this orientation (already the right way round).
        if(c.hi - c.lo < kMinSpan)
        {
            const float n = c.inverted ? (kVref - volts) / (kVref - 0.30f)
                                       : volts / 3.00f;
            return fx::Clamp(n, 0.f, 1.f);
        }

        const float lo = c.lo + kEdgeMargin;
        const float hi = c.hi - kEdgeMargin;
        float       n  = hi > lo ? (volts - lo) / (hi - lo) : 0.f;
        if(c.inverted)
            n = 1.f - n;
        return fx::Clamp(n, 0.f, 1.f);
    }

    /** True once this channel has been seen above what correct wiring allows. */
    bool IsInverted(int i) const
    {
        return i >= 0 && i < kMaxChannels && ch_[i].inverted;
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
        float lo       = kVref;
        float hi       = 0.f;
        bool  inverted = false;
    };
    Channel ch_[kMaxChannels];
};

} // namespace rodeo002
