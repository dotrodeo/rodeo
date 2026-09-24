// rodeo-002 — panel pot conditioning: wiring, travel and calibration.
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
// necessarily supply-swapped.
//
// THE NOMINAL RANGE IS A STARTING GUESS, NOT THE ANSWER. Component tolerance
// moves both ends: a 10k pot at -20% tops out at 2.93 V correctly wired, and
// bottoms out at 0.37 V swapped. An earlier version only ever WIDENED from the
// nominal range, so a pot whose real travel was narrower than nominal could
// never reach 0 or 1 -- reported from hardware on 2026-09-24 as "once in a
// while, after a full sweep, a pot still won't go to zero". So each channel
// also learns its true endpoints, and once it has seen a sweep worth believing
// (kSweptSpan) it maps from those instead of from the guess. Before that the
// nominal range still applies, so a knob is usable the moment it is plugged in.
//
// The learned endpoints are worth keeping: PanelPots does not own any storage,
// but Export/Import move the whole calibration through a POD the caller can put
// in flash, so a sweep only has to be done once.

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
    /** Nominal travel, correct wiring: 0 .. 3.3*Rpot/(Rpot+1k). */
    static constexpr float kNormalTop = 3.00f;
    /** Nominal travel, supply swapped: 3.3*1k/(Rpot+1k) .. 3.3. */
    static constexpr float kSwappedBottom = 0.30f;
    /** Travel that counts as a real sweep rather than a nudge. Every wiring
     *  spans ~2.93-3.05 V, so this is generous enough to trust and far above
     *  anything a knob does by accident. */
    static constexpr float kSweptSpan = 2.60f;
    /** Given up at each end of the learned travel so the last sliver of knob
     *  saturates instead of stopping just short. 1.5% is about 5 degrees. */
    static constexpr float kEdgeFraction = 0.015f;
    /** An endpoint has to move by this much to be worth a flash write. */
    static constexpr float kSaveEpsilon = 0.005f;

    /** The whole calibration as plain data, for the caller to store. Endpoints
     *  are volts; an unswept channel keeps its sentinels and restores as
     *  unswept. */
    struct Calibration
    {
        uint32_t magic;
        uint16_t version;
        uint16_t channels;
        uint32_t swapped_mask;
        float    lo[kMaxChannels];
        float    hi[kMaxChannels];

        bool operator==(const Calibration& o) const
        {
            if(magic != o.magic || version != o.version || channels != o.channels
               || swapped_mask != o.swapped_mask)
                return false;
            for(int i = 0; i < kMaxChannels; i++)
                if(lo[i] != o.lo[i] || hi[i] != o.hi[i])
                    return false;
            return true;
        }
        bool operator!=(const Calibration& o) const { return !(*this == o); }
    };
    static constexpr uint32_t kMagic   = 0x52325043u; // "R2PC"
    static constexpr uint16_t kVersion = 1;

    void Init()
    {
        for(int i = 0; i < kMaxChannels; i++)
            Forget(i);
        revision_ = 0;
    }

    /** Throw away what a channel has learned: its next readings start the
     *  sweep again from the nominal range. Wiring detection is cleared too,
     *  since the whole point is a satellite that has been changed. */
    void Forget(int i)
    {
        if(i < 0 || i >= kMaxChannels)
            return;
        ch_[i].seen_lo = kUnseenLo;
        ch_[i].seen_hi = kUnseenHi;
        ch_[i].swapped = false;
        revision_++;
    }
    void Forget()
    {
        for(int i = 0; i < kMaxChannels; i++)
            Forget(i);
    }

    /** Feed the TRUE measured voltage; returns the control value 0..1. */
    float Normalize(int i, float volts)
    {
        if(i < 0 || i >= kMaxChannels)
            return 0.f;
        Channel& c = ch_[i];

        // Above what correct wiring can produce: supply and ground are
        // swapped. Sticky -- one reading is proof, and it cannot be undone by
        // later readings that merely fit both wirings.
        if(volts > kSwappedThreshold && !c.swapped)
        {
            c.swapped = true;
            revision_++;
        }
        if(volts < c.seen_lo)
        {
            if(c.seen_lo - volts > kSaveEpsilon)
                revision_++;
            c.seen_lo = volts;
        }
        if(volts > c.seen_hi)
        {
            if(volts - c.seen_hi > kSaveEpsilon)
                revision_++;
            c.seen_hi = volts;
        }

        float lo = c.seen_lo, hi = c.seen_hi;
        if(hi - lo < kSweptSpan)
        {
            // Not swept yet: fall back on what this wiring nominally does, so
            // the knob spans roughly 0..1 from the first turn.
            lo = fminf(lo, c.swapped ? kSwappedBottom : 0.f);
            hi = fmaxf(hi, c.swapped ? kVref : kNormalTop);
        }

        const float margin = (hi - lo) * kEdgeFraction;
        const float lo_e   = lo + margin;
        const float hi_e   = hi - margin;
        const float n      = hi_e > lo_e ? (volts - lo_e) / (hi_e - lo_e) : 0.f;
        return fx::Clamp(n, 0.f, 1.f);
    }

    /** True once this channel has been seen above what correct wiring allows,
     *  i.e. its analog run was built with a straight cable. */
    bool IsSupplySwapped(int i) const
    {
        return i >= 0 && i < kMaxChannels && ch_[i].swapped;
    }
    /** True once the channel has seen a sweep worth mapping from, so its ends
     *  are its own rather than the nominal guess. */
    bool IsCalibrated(int i) const { return SpanSeen(i) >= kSweptSpan; }
    /** Travel seen so far, for the bring-up dump. */
    float SpanSeen(int i) const
    {
        if(i < 0 || i >= kMaxChannels)
            return 0.f;
        const float s = ch_[i].seen_hi - ch_[i].seen_lo;
        return s > 0.f ? s : 0.f;
    }
    /** Bumped whenever a channel learns something worth storing. The caller
     *  watches it to decide when a flash write has earned itself. */
    uint32_t Revision() const { return revision_; }

    void Export(Calibration& c) const
    {
        c.magic        = kMagic;
        c.version      = kVersion;
        c.channels     = kMaxChannels;
        c.swapped_mask = 0;
        for(int i = 0; i < kMaxChannels; i++)
        {
            c.lo[i] = ch_[i].seen_lo;
            c.hi[i] = ch_[i].seen_hi;
            if(ch_[i].swapped)
                c.swapped_mask |= (1u << i);
        }
    }

    /** Restore a stored calibration. Rejects anything that is not ours, and
     *  any endpoint that is not a voltage this hardware can produce -- a
     *  corrupt store must not be able to make a knob unusable. */
    bool Import(const Calibration& c)
    {
        if(c.magic != kMagic || c.version != kVersion || c.channels != kMaxChannels)
            return false;
        for(int i = 0; i < kMaxChannels; i++)
        {
            const bool sane = c.lo[i] >= -0.1f && c.hi[i] <= kVref + 0.1f
                              && c.hi[i] > c.lo[i];
            if(sane)
            {
                ch_[i].seen_lo = c.lo[i];
                ch_[i].seen_hi = c.hi[i];
            }
            else
            {
                ch_[i].seen_lo = kUnseenLo; // unswept, or nonsense: start over
                ch_[i].seen_hi = kUnseenHi;
            }
            ch_[i].swapped = (c.swapped_mask & (1u << i)) != 0;
        }
        revision_ = 0;
        return true;
    }

  private:
    /** Sentinels: no reading yet. Chosen outside any real voltage so the first
     *  reading replaces them and the span reads as negative until then. */
    static constexpr float kUnseenLo = 99.f;
    static constexpr float kUnseenHi = -99.f;

    struct Channel
    {
        float seen_lo = kUnseenLo;
        float seen_hi = kUnseenHi;
        bool  swapped = false;
    };
    Channel  ch_[kMaxChannels];
    uint32_t revision_ = 0;
};

} // namespace rodeo002
