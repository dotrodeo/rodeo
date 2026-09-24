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
// WIRING IS DETECTED, NOT CONFIGURED, and each end proves one of the two. A
// correctly wired satellite CANNOT read above ~3.05 V -- the 1 kOhm resistor
// forbids it even for a +20% pot (3.3 * 12/13 = 3.046) -- so anything above
// kSwappedThreshold is necessarily supply-swapped. A swapped one cannot read
// below ~0.25 V for the same reason, so anything under kCorrectThreshold is
// necessarily wired correctly.
//
// UNTIL ONE OF THEM IS PROVEN, ASSUME NEITHER. Assuming correct wiring is what
// broke pickup on hardware (2026-09-24): a swapped pot read 0.065 at its stop
// instead of 0, so any parameter sitting at 0 could never be caught and the
// screen showed DN for ever. The two wirings only overlap between 0.30 and
// 3.00 V, so no single mapping can be exact for both -- but one that saturates
// INSIDE both travels reaches a true 0 and 1 on either, at the cost of some
// dead knob until the wiring proves itself. Correctness at the ends beats
// accuracy in the middle, because the ends are where pickup lives.
//
// THE RANGE IS FIXED, NOT LEARNED. An earlier version learned each channel's
// true endpoints from what it saw and mapped from those. It is the obvious
// thing to do and it was worse in practice (measured on hardware 2026-09-24:
// "all but one is unable to reach 0 after calibration"), because a learned
// minimum is only ever as good as the worst sample ever taken. One reading from
// a settling ADC, a bad solder joint or a satellite being plugged in sets the
// bottom of the range for good, and every later reading is then measured
// against that mistake -- which persisting the calibration to flash made
// permanent rather than merely annoying.
//
// So: fixed endpoints per wiring, inset by kDeadZone at each end. The cost is a
// few degrees of dead travel at each stop; the gain is that every pot reaches a
// true 0 and 1 on the first turn, with nothing to calibrate, nothing to get
// wrong, and no state that can rot. Derek's call, and the right trade for an
// instrument: "I'd rather have a little dead zone and have access to the full
// value range than have perfectly calibrated pots."
//
// The one thing still worth remembering is the WIRING, which is a fact about
// the cable rather than a measurement that can drift -- see Export/Import.

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
    /** Given up at each end so every pot in tolerance reaches a true 0 and 1.
     *  4% of a ~3.0 V travel is 0.12 V, and the tightest case clears it: a -20%
     *  pot wired correctly tops out at 3.3 * 8/9 = 2.93 V against a 2.88 V
     *  ceiling, and swapped it bottoms out at 3.3 * 1/9 = 0.37 V against a
     *  0.43 V floor. On a 300-degree pot this costs about 12 degrees at each
     *  stop. */
    static constexpr float kDeadZone = 0.04f;
    /** A swapped satellite bottoms out at ~0.25 V even at +20% tolerance
     *  (3.3 / 13), so anything below this was wired correctly. */
    static constexpr float kCorrectThreshold = 0.20f;
    /** Consecutive readings past a threshold before the verdict sticks. One
     *  sample is not proof: the verdict is permanent and stored, so a lone
     *  spike must not be able to set it. */
    static constexpr uint8_t kSwappedConfirm = 3;
    /** The mapping used while the wiring is unproven. Saturates inside BOTH
     *  travels -- above the worst swapped bottom (0.37 V at -20%) and below the
     *  worst correct top (2.93 V at -20%) -- so every pot reaches a true 0 and
     *  1 whichever way its run was built. */
    static constexpr float kUnknownLo = 0.45f;
    static constexpr float kUnknownHi = 2.85f;

    enum class Wiring : uint8_t
    {
        kUnknown = 0,
        kCorrect,
        kSwapped
    };

    /** What is worth keeping between power-ups: the wiring of each run, which
     *  is a property of the cable, not a measurement. Nothing about the travel
     *  is stored, because nothing about the travel is learned. */
    struct Calibration
    {
        uint32_t magic;
        uint16_t version;
        uint16_t channels;
        uint32_t swapped_mask;
        uint32_t correct_mask; // proven correct, as opposed to merely unproven

        bool operator==(const Calibration& o) const
        {
            return magic == o.magic && version == o.version
                   && channels == o.channels && swapped_mask == o.swapped_mask
                   && correct_mask == o.correct_mask;
        }
        bool operator!=(const Calibration& o) const { return !(*this == o); }
    };
    static constexpr uint32_t kMagic = 0x52325043u; // "R2PC"
    /** 3: v1 stored learned endpoints, which are gone; v2 knew only "swapped
     *  or not", with unproven treated as correct. An old store is rejected and
     *  the wiring proves itself again, which takes one turn to either stop. */
    static constexpr uint16_t kVersion = 3;

    void Init()
    {
        for(int i = 0; i < kMaxChannels; i++)
            Forget(i);
        revision_ = 0;
    }

    /** Forget a channel's wiring, for a satellite or cable that has changed.
     *  The next reading above the threshold works it out again. */
    void Forget(int i)
    {
        if(i < 0 || i >= kMaxChannels)
            return;
        ch_[i]    = Channel{};
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
        // swapped. Sticky once confirmed -- the range genuinely differs, and
        // no later reading can disprove it (every swapped voltage is also a
        // legal correct-wiring voltage, except the ones above the threshold).
        if(volts > kSwappedThreshold)
        {
            c.low_run = 0;
            if(c.wiring != Wiring::kSwapped && ++c.high_run >= kSwappedConfirm)
            {
                c.wiring = Wiring::kSwapped;
                revision_++;
            }
        }
        else if(volts < kCorrectThreshold)
        {
            c.high_run = 0;
            if(c.wiring != Wiring::kCorrect && ++c.low_run >= kSwappedConfirm)
            {
                c.wiring = Wiring::kCorrect;
                revision_++;
            }
        }
        else
            c.high_run = c.low_run = 0;

        // Diagnostics only. Deliberately NOT fed back into the mapping: that
        // was the 2026-09-24 fault.
        if(volts < c.seen_lo)
            c.seen_lo = volts;
        if(volts > c.seen_hi)
            c.seen_hi = volts;

        float lo_e, hi_e;
        if(c.wiring == Wiring::kUnknown)
        {
            // Unproven: the mapping that reaches both ends on either wiring.
            lo_e = kUnknownLo;
            hi_e = kUnknownHi;
        }
        else
        {
            const float lo   = c.wiring == Wiring::kSwapped ? kSwappedBottom : 0.f;
            const float hi   = c.wiring == Wiring::kSwapped ? kVref : kNormalTop;
            const float dead = (hi - lo) * kDeadZone;
            lo_e             = lo + dead;
            hi_e             = hi - dead;
        }
        return fx::Clamp((volts - lo_e) / (hi_e - lo_e), 0.f, 1.f);
    }

    /** True once this channel has been seen above what correct wiring allows,
     *  i.e. its analog run was built with a straight cable. */
    bool IsSupplySwapped(int i) const
    {
        return i >= 0 && i < kMaxChannels && ch_[i].wiring == Wiring::kSwapped;
    }
    /** False while the run is still using the mapping that fits either wiring:
     *  usable, but with more dead travel than it needs. Turning the pot to
     *  either stop proves it. */
    bool WiringKnown(int i) const
    {
        return i >= 0 && i < kMaxChannels && ch_[i].wiring != Wiring::kUnknown;
    }
    /** Travel seen since boot. Diagnostic: it says whether a channel is alive,
     *  and it is what the dump prints. It does not affect the mapping. */
    float SpanSeen(int i) const
    {
        if(i < 0 || i >= kMaxChannels)
            return 0.f;
        const float s = ch_[i].seen_hi - ch_[i].seen_lo;
        return s > 0.f ? s : 0.f;
    }
    /** Bumped when a wiring verdict changes, i.e. when a store is out of date. */
    uint32_t Revision() const { return revision_; }

    void Export(Calibration& c) const
    {
        c.magic        = kMagic;
        c.version      = kVersion;
        c.channels     = kMaxChannels;
        c.swapped_mask = 0;
        c.correct_mask = 0;
        for(int i = 0; i < kMaxChannels; i++)
        {
            if(ch_[i].wiring == Wiring::kSwapped)
                c.swapped_mask |= (1u << i);
            else if(ch_[i].wiring == Wiring::kCorrect)
                c.correct_mask |= (1u << i);
        }
    }

    /** Restore a stored wiring map. Rejects anything that is not ours or is
     *  from an older layout; the wiring is then simply detected again. */
    bool Import(const Calibration& c)
    {
        if(c.magic != kMagic || c.version != kVersion || c.channels != kMaxChannels)
            return false;
        for(int i = 0; i < kMaxChannels; i++)
        {
            const bool sw   = (c.swapped_mask & (1u << i)) != 0;
            const bool ok   = (c.correct_mask & (1u << i)) != 0;
            ch_[i].wiring   = sw ? Wiring::kSwapped
                                 : (ok ? Wiring::kCorrect : Wiring::kUnknown);
            ch_[i].high_run = 0;
            ch_[i].low_run  = 0;
        }
        revision_ = 0;
        return true;
    }

  private:
    /** Sentinels for the diagnostic travel: no reading yet. */
    static constexpr float kUnseenLo = 99.f;
    static constexpr float kUnseenHi = -99.f;

    struct Channel
    {
        float   seen_lo  = kUnseenLo;
        float   seen_hi  = kUnseenHi;
        Wiring  wiring   = Wiring::kUnknown;
        uint8_t high_run = 0;
        uint8_t low_run  = 0;
    };
    Channel  ch_[kMaxChannels];
    uint32_t revision_ = 0;
};

} // namespace rodeo002
