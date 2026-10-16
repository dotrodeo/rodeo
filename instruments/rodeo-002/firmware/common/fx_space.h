// rodeo-002 — SPACE slot: the granular block.
//
//   Amount pot   = Mix       (0 = transparent)
//   Shift pot    = Damp      (darkening in the feedback path)
//   Own pots     = Time, Feedback, Grain; shift: grain Pitch, Spread, (Tilt is
//                  the chain's output stage, not this slot's)
//   Encoder      = type:      Echo / Cloud / Reverse / Wash / Reverb
//   Shift+turn   = option:    grain-pitch quantize, free / semitones /
//                             fifths + octaves / octaves
//   Long-press   = Freeze, in every type
//
//            Time          Feedback      Grain
//   Echo     delay time    repeats       grain size (clean -> smeared)
//   Cloud    window        regeneration  density
//   Reverse  delay time    repeats       segment length
//   Wash     window        regeneration  diffusion
//   Reverb   pre-delay     decay         shimmer (pitched grains into the tail)
//
// BYPASS KEEPS THE TAIL: the input stops feeding the effect and the dry returns
// to unity, but echoes, clouds and reverb die away on their own instead of
// being cut. Type changes do NOT clear the SDRAM buffers -- zeroing megabytes
// in the audio callback would glitch it -- so old material can spill briefly
// into the new type.
//
// The big buffers are handed in (Buffers), not owned: on the Seed they live in
// SDRAM, in the host tests in ordinary memory. Reverb is DaisySP-LGPL's
// ReverbSc, whose ~396 KB internal buffer also has to live in SDRAM.

#pragma once

#include <cstddef>
#include <cstdint>

#include "daisysp.h"
#include "daisysp-lgpl.h"
#include "dsp_util.h"

namespace rodeo002
{
namespace fx
{
class Space
{
  public:
    enum Type
    {
        kEcho = 0,
        kCloud,
        kReverse,
        kWash,
        kReverb,
        kNumTypes
    };
    enum Quantize
    {
        kFree = 0,
        kSemitones,
        kFifths,
        kOctaves,
        kNumOptions
    };

    /** Grain buffer: 1 << 18 samples per channel, 5.46 s at 48 kHz. A power
     *  of two so the read wrap is a mask. Covers the longest Cloud/Wash window
     *  (4 s) plus a grain's travel. */
    static constexpr size_t kGrainLen = size_t(1) << 18;
    /** Reverb pre-delay buffer: 1 << 15 samples, 683 ms (Time maxes at 500). */
    static constexpr size_t kPreLen = size_t(1) << 15;
    /** Reverb input trim at the SHORTEST decay; longer decays scale it down
     *  (see rev_in). Chosen on the host so that a full-wet sustained chord
     *  matches the dry level it replaces rather than sitting far below it. */
    static constexpr float kRevInput = 1.75f;

    struct Buffers
    {
        float*             grain_l; // kGrainLen each
        float*             grain_r;
        float*             pre_l; // kPreLen each
        float*             pre_r;
        daisysp::ReverbSc* reverb;
    };

    static const char* TypeName(int t)
    {
        static const char* kNames[kNumTypes]
            = {"Echo", "Cloud", "Reverse", "Wash", "Reverb"};
        return (t >= 0 && t < kNumTypes) ? kNames[t] : "?";
    }
    static const char* OptionName(int o)
    {
        static const char* kNames[kNumOptions]
            = {"Free", "Semitones", "Fifths", "Octaves"};
        return (o >= 0 && o < kNumOptions) ? kNames[o] : "?";
    }

    /** Zeroes the buffers and initialises the reverb -- call once at startup,
     *  never from the audio callback. */
    void Init(float sample_rate, const Buffers& b)
    {
        sample_rate_ = sample_rate;
        buf_         = b;
        for(size_t i = 0; i < kGrainLen; i++)
            buf_.grain_l[i] = buf_.grain_r[i] = 0.f;
        for(size_t i = 0; i < kPreLen; i++)
            buf_.pre_l[i] = buf_.pre_r[i] = 0.f;
        buf_.reverb->Init(sample_rate);
        for(int i = 0; i < kWindowLen; i++)
            window_[i] = 0.5f
                         - 0.5f * cosf(kTwoPi * static_cast<float>(i)
                                       / static_cast<float>(kWindowLen - 1));
        rng_.Seed(0x5EED5EEDu);
        shim_ref_rel_ = TimeCoeff(1.0f, sample_rate);
        shim_env_atk_ = TimeCoeff(0.005f, sample_rate);
        shim_env_rel_ = TimeCoeff(0.2f, sample_rate);
        dry_.Init(sample_rate, 0.02f, 1.f);
        wet_.Init(sample_rate, 0.02f, 0.f);
        input_.Init(sample_rate, 0.02f, 1.f);
        write_ = pre_write_ = 0;
        type_              = kEcho;
        option_            = kFree;
        SetMix(0.f);
        SetTime(0.35f);
        SetFeedback(0.4f);
        SetGrain(0.5f);
        SetPitch(0.5f);
        SetSpread(0.5f);
        SetDamp(0.3f);
        Reset();
    }

    /** Kill grains and clear the small DTCM state. Leaves the SDRAM buffers
     *  and the reverb alone (see the header note). */
    void Reset()
    {
        n_grains_ = 0;
        for(int c = 0; c < 2; c++)
        {
            damp_[c].Reset();
            for(int s = 0; s < kDiffStages; s++)
                diff_[c][s].Reset();
        }
        spawn_countdown_ = 1;
        rev_out_[0] = rev_out_[1] = 0.f;
        shim_ref_ = shim_env_ = 0.f;
        for(int c = 0; c < 2; c++)
        {
            dc_in_[c].Reset();
            dc_rev_[c].Reset();
        }
    }

    /** Snap the dry/wet/input smoothers to their targets -- for the chain's
     *  immediate (non-faded) configuration. */
    void SnapGains()
    {
        dry_.Snap(dry_.target);
        wet_.Snap(wet_.target);
        input_.Snap(input_.target);
    }

    void SetType(int t)
    {
        type_ = (t >= 0 && t < kNumTypes) ? t : kEcho;
        UpdateGrainShape();
    }
    int GetType() const { return type_; }

    void SetOption(int o)
    {
        option_ = (o >= 0 && o < kNumOptions) ? o : kFree;
        SetPitch(pitch_pot_);
    }

    /** Amount pot. Dry falls and wet rises on complementary curves, so the
     *  middle of the knob is ~0.75 of each rather than a dip. */
    void SetMix(float m)
    {
        mix_ = Clamp(m, 0.f, 1.f);
        UpdateGains();
    }

    /** Same as SetMix -- every slot takes its panel "amount" the same way. */
    void SetAmount(float m) { SetMix(m); }

    void SetBypass(bool b)
    {
        bypass_ = b;
        UpdateGains();
    }
    bool GetBypass() const { return bypass_; }

    void SetFreeze(bool f) { freeze_ = f; }
    bool GetFreeze() const { return freeze_; }

    void SetTime(float t)
    {
        time_ = Clamp(t, 0.f, 1.f);
        UpdateGrainShape();
    }
    void SetFeedback(float f) { feedback_ = Clamp(f, 0.f, 1.f); }
    void SetGrain(float g)
    {
        grain_ = Clamp(g, 0.f, 1.f);
        UpdateGrainShape();
    }
    void SetSpread(float s) { spread_ = Clamp(s, 0.f, 1.f); }

    /** Pitch pot: 0.5 is unison, the ends are +/- two octaves, quantized per
     *  the option. Free still snaps near the centre so unison is findable. */
    void SetPitch(float p)
    {
        pitch_pot_ = Clamp(p, 0.f, 1.f);
        float st   = (pitch_pot_ - 0.5f) * 48.f;
        switch(option_)
        {
            case kFree:
                if(fabsf(st) < 0.4f)
                    st = 0.f;
                break;
            case kSemitones: st = roundf(st); break;
            case kFifths:
            {
                static const float kSteps[] = {-24, -19, -12, -7, 0, 7, 12, 19, 24};
                st = Nearest(st, kSteps, 9);
                break;
            }
            default:
            {
                static const float kSteps[] = {-24, -12, 0, 12, 24};
                st = Nearest(st, kSteps, 5);
                break;
            }
        }
        semitones_ = st;
        rate_      = powf(2.f, st / 12.f);
        UpdateGrainShape();
    }
    float GetSemitones() const { return semitones_; }

    void SetDamp(float d)
    {
        damp_pot_ = Clamp(d, 0.f, 1.f);
        const float hz = ExpMap(damp_pot_, 18000.f, 1000.f);
        for(int c = 0; c < 2; c++)
            damp_[c].SetFreq(hz, sample_rate_);
        buf_.reverb->SetLpFreq(ExpMap(damp_pot_, 18000.f, 1500.f));
    }

    /** Holds the wet at zero (smoothly) without touching the mix setting --
     *  the chain uses it to fade Space out across a type change. */
    void SetWetHold(bool h)
    {
        wet_hold_ = h;
        UpdateGains();
    }
    bool WetIsSilent() const { return wet_.cur < 1e-4f; }

    /** Grains currently playing -- for profiling and the OLED, not control. */
    int ActiveGrains() const { return n_grains_; }

    /** Bypassed with the tail fully decayed: ProcessBlock does nothing (the
     *  dry is already at unity), so an unused Space costs no CPU. */
    bool IsIdle() const
    {
        return bypass_ && input_.cur < 1e-4f && tail_env_ < 1e-5f;
    }

    /** Zero every buffer and re-initialise the reverb: a true silence. Takes
     *  milliseconds -- only ever call it while the audio callback is NOT
     *  running this slot (the benchmark does, between measurements). */
    void Silence()
    {
        for(size_t i = 0; i < kGrainLen; i++)
            buf_.grain_l[i] = buf_.grain_r[i] = 0.f;
        for(size_t i = 0; i < kPreLen; i++)
            buf_.pre_l[i] = buf_.pre_r[i] = 0.f;
        buf_.reverb->Init(sample_rate_);
        SetDamp(damp_pot_); // Init resets the reverb's damping
        Reset();
        tail_env_ = 0.f;
    }

    void ProcessBlock(float* l, float* r, size_t n)
    {
        if(IsIdle())
            return;
        // Mix at zero: nobody can hear the wet, so don't render it. Recording
        // continues, so raising the mix brings in the audio that just played.
        // Measured on hardware: a full Echo cost the same at mix 0 as at mix 1.
        if(wet_.cur < 1e-4f && wet_.target < 1e-4f)
        {
            RecordOnly(l, r, n);
            return;
        }
        switch(type_)
        {
            case kEcho: Run<kEcho>(l, r, n); break;
            case kCloud: Run<kCloud>(l, r, n); break;
            case kReverse: Run<kReverse>(l, r, n); break;
            case kWash: Run<kWash>(l, r, n); break;
            default: Run<kReverb>(l, r, n); break;
        }
    }

  private:
    static constexpr int      kMaxGrains = 12;
    static constexpr int      kFracBits  = 14;
    static constexpr uint32_t kFracOne   = 1u << kFracBits;
    static constexpr float    kFracScale = static_cast<float>(kFracOne);
    static constexpr float    kFracInv   = 1.f / kFracScale;
    static constexpr int    kWindowLen  = 512;
    static constexpr int    kDiffStages = 4;
    static constexpr size_t kGrainMask  = kGrainLen - 1;
    static constexpr size_t kPreMask    = kPreLen - 1;

    // Position is FIXED POINT: 18 bits of sample index (the buffer is 1<<18)
    // and 14 bits of fraction. That removes a floorf and two float->int
    // conversions per channel per sample; the hardware profile put the grain
    // loop at ~250 cycles/sample/grain, and this arithmetic was most of it.
    // 14 fractional bits is ~-84 dB of interpolation error -- inaudible here.
    struct Grain
    {
        uint32_t pos;   // 18.14 fixed point, wraps with kGrainMask
        int32_t  step;  // playback rate in the same fixed point; negative = reverse
        float    t;     // 0..1 through the window
        float    t_inc; // 1/len, so no division per sample
        float    gl, gr;
    };

    // Schroeder all-pass for Wash's diffuser.
    struct AllPass
    {
        daisysp::DelayLine<float, 256> line;
        float                          delay = 100.f;
        void                           Reset() { line.Reset(); }
        float                          Process(float x, float g)
        {
            const float d = line.Read(delay);
            const float y = d - g * x;
            line.Write(x + g * y);
            return y;
        }
    };

    static float Nearest(float v, const float* steps, int n)
    {
        float best = steps[0];
        for(int i = 1; i < n; i++)
            if(fabsf(v - steps[i]) < fabsf(v - best))
                best = steps[i];
        return best;
    }

    void UpdateGains()
    {
        dry_.Set(bypass_ ? 1.f : 1.f - mix_ * mix_);
        wet_.Set(wet_hold_ ? 0.f : 1.f - (1.f - mix_) * (1.f - mix_));
        input_.Set(bypass_ ? 0.f : 1.f);
    }

    // Grain length, overlap and read offset per type. Control rate.
    void UpdateGrainShape()
    {
        const float g = grain_, t = time_;
        switch(type_)
        {
            case kEcho:
                grain_len_ = ExpMap(g, 0.02f, 0.3f);
                overlap_   = 2.f + 2.f * g;
                offset_    = ExpMap(t, 0.02f, 2.f);
                break;
            case kCloud:
                grain_len_ = ExpMap(g, 0.04f, 0.25f);
                overlap_   = 1.f + 5.f * g;
                offset_    = ExpMap(t, 0.1f, 4.f); // the scatter window
                break;
            case kReverse:
                grain_len_ = ExpMap(g, 0.05f, 0.5f);
                overlap_   = 2.f;
                offset_    = ExpMap(t, 0.02f, 2.f);
                break;
            case kWash:
                grain_len_ = 0.15f;
                overlap_   = 4.f;
                offset_    = ExpMap(t, 0.2f, 4.f);
                break;
            default: // kReverb: a short pitch-shifting echo feeds the shimmer
                grain_len_ = 0.12f;
                overlap_   = 2.f;
                offset_    = 0.08f;
                break;
        }
        grain_len_ *= sample_rate_;
        offset_ *= sample_rate_;
        // A pitched-up grain travels len x rate; start far enough back that it
        // never overtakes the write head.
        const float min_offset = grain_len_ * rate_ + 64.f;
        if(type_ != kReverse && type_ != kCloud && offset_ < min_offset)
            offset_ = min_offset;
        // Overlapping Hann windows sum to ~overlap/2, so scale by 2/overlap --
        // but never ABOVE unity. Sparse grains (Cloud at low density) would
        // otherwise be boosted 2x, and inside the feedback loop that is a loop
        // gain over 1: the host tests caught exactly that runaway.
        norm_ = overlap_ > 2.f ? 2.f / overlap_ : 1.f;
    }

    template <int T>
    void Spawn()
    {
        // All slots busy: skip. kMaxGrains is what bounds the worst case.
        if(n_grains_ >= kMaxGrains)
            return;
        Grain& g = grains_[n_grains_++];

        float back;
        if(T == kCloud || T == kWash)
            back = rng_.Uniform() * offset_ + grain_len_ * rate_ + 64.f;
        else
            back = offset_ * (1.f + 0.1f * spread_ * rng_.Bipolar());
        float pos = static_cast<float>(write_) - back;
        while(pos < 0.f)
            pos += static_cast<float>(kGrainLen);

        g.pos   = static_cast<uint32_t>(pos * kFracScale);
        g.step  = static_cast<int32_t>((T == kReverse ? -rate_ : rate_) * kFracScale);
        g.t     = 0.f;
        g.t_inc = 1.f / grain_len_;
        // Balance, not constant-power pan: each side tops out at 1. A
        // constant-power law boosts a hard-panned grain by sqrt(2), which
        // inside the feedback loop made Echo's loop gain 0.95 x 1.41 = 1.34.
        const float p = 0.5f + 0.5f * spread_ * rng_.Bipolar();
        g.gl          = p < 0.5f ? 1.f : 2.f * (1.f - p);
        g.gr          = p > 0.5f ? 1.f : 2.f * p;
    }

    float WindowAt(float t) const
    {
        const float x = t * static_cast<float>(kWindowLen - 1);
        const int   i = static_cast<int>(x);
        const int   j = i + 1 < kWindowLen ? i + 1 : kWindowLen - 1;
        return window_[i] + (window_[j] - window_[i]) * (x - static_cast<float>(i));
    }

    /** Linear interpolation straight out of the fixed-point position: an
     *  index, a mask and a multiply -- no floorf, no float->int conversion. */
    static float ReadInterp(const float* buf, uint32_t pos)
    {
        const size_t i = (pos >> kFracBits) & kGrainMask;
        const float  f = static_cast<float>(pos & (kFracOne - 1)) * kFracInv;
        return buf[i] + (buf[(i + 1) & kGrainMask] - buf[i]) * f;
    }

    /** Mix 0: keep the buffers fed and the smoothers moving, render nothing.
     *  Regeneration stops too -- inaudible at mix 0, and the delay still has
     *  the input's own history when the mix comes back up. */
    void RecordOnly(float* l, float* r, size_t n)
    {
        for(size_t i = 0; i < n; i++)
        {
            const float dry_g = dry_.Next();
            wet_.Next();
            const float in_g = input_.Next() * (freeze_ ? 0.f : 1.f);
            const float in_l = l[i] * in_g, in_r = r[i] * in_g;

            if(type_ == kReverb)
            {
                buf_.pre_l[pre_write_] = in_l;
                buf_.pre_r[pre_write_] = in_r;
                pre_write_             = (pre_write_ + 1) & kPreMask;
            }
            if(!freeze_)
            {
                buf_.grain_l[write_] = in_l;
                buf_.grain_r[write_] = in_r;
                write_               = (write_ + 1) & kGrainMask;
            }
            const float lvl = fabsf(in_l) + fabsf(in_r);
            tail_env_       = lvl > tail_env_ ? lvl : tail_env_ * 0.9999f;

            l[i] *= dry_g;
            r[i] *= dry_g;
        }
        n_grains_ = 0;
    }

    template <int T>
    void Run(float* l, float* r, size_t n)
    {
        // Reverb: feedback is the decay; frozen, it holds and takes no input.
        const float decay = freeze_ ? 0.999f : 0.6f + 0.39f * feedback_;
        if(T == kReverb)
            buf_.reverb->SetFeedback(decay);
        // A LONGER DECAY IS A LOUDER REVERB unless the input pays for it: the
        // steady-state level of a feedback network goes as 1/sqrt(1 - decay),
        // so a sustained chord at full decay arrived several times over full
        // scale and sat on the output limiter -- the "oversaturation" reported
        // from hardware on 2026-09-23. Normalising by that same factor holds
        // the wet level roughly constant as the knob lengthens the tail, which
        // is what turning a DECAY control should do. Referenced to the shortest
        // decay so the bottom of the knob is unchanged.
        const float rev_in = kRevInput * sqrtf((1.f - decay) / 0.4f);
        const float fb = feedback_ * 0.95f;
        // Wash: Grain is the diffusion coefficient.
        const float diffusion = 0.3f + 0.4f * grain_;
        const float pre_delay = time_ * 0.5f * sample_rate_ + 1.f;

        for(size_t i = 0; i < n; i++)
        {
            const float dry_g = dry_.Next();
            const float wet_g = wet_.Next();
            const float in_g  = input_.Next() * (freeze_ ? 0.f : 1.f);
            // DC IN IS DC AMPLIFIED, so none gets into the loops. The voices
            // carry a small offset -- ~-0.04 measured, ordinary for asymmetric
            // and sync'd oscillators -- and every Space type feeds itself back:
            // ReverbSc at full decay has a DC gain near 1/(1-0.99) = 100x and
            // the granular types about 20x. On hardware (2026-09-23) that
            // walked the output down onto the limiter at a constant -1.0. A
            // railed output has no AC left in it and the Seed's line out is
            // AC-coupled, so it is heard as the AUDIO CUTTING OUT, and it comes
            // back only once the feedback is lowered and the offset decays.
            // Blocked HERE, on the effect input, rather than at the chain's
            // input: the dry path past this slot stays bit-exact, so the chain
            // is still transparent with everything at zero.
            const float in_l  = dc_in_[0].Process(l[i] * in_g);
            const float in_r  = dc_in_[1].Process(r[i] * in_g);

            if(--spawn_countdown_ <= 0)
            {
                const int period  = static_cast<int>(grain_len_ / overlap_);
                spawn_countdown_ = period > 16 ? period : 16;
                if(T != kReverb || grain_ > 0.001f)
                    Spawn<T>();
            }

            // Only the live grains, packed at the front: an idle slot costs
            // nothing. A finished grain is swapped with the last one.
            float gl = 0.f, gr = 0.f;
            for(int k = 0; k < n_grains_;)
            {
                Grain& g = grains_[k];
                if(g.t >= 1.f)
                {
                    grains_[k] = grains_[--n_grains_];
                    continue;
                }
                const float w = WindowAt(g.t);
                gl += ReadInterp(buf_.grain_l, g.pos) * w * g.gl;
                gr += ReadInterp(buf_.grain_r, g.pos) * w * g.gr;
                g.pos += static_cast<uint32_t>(g.step); // wraps; masked on read
                g.t += g.t_inc;
                k++;
            }
            gl *= norm_;
            gr *= norm_;

            float wl, wr;      // the slot's wet signal
            float write_l, write_r; // what goes into the grain buffer
            if(T == kReverb)
            {
                // Pre-delay the input, add the shimmer, run the reverb. The
                // grain buffer records the reverb's output, so the shimmer
                // pitch-shifts the tail and feeds it back -- stacking octaves.
                buf_.pre_l[pre_write_] = in_l;
                buf_.pre_r[pre_write_] = in_r;
                const size_t rd
                    = (pre_write_ - static_cast<size_t>(pre_delay)) & kPreMask;
                pre_write_ = (pre_write_ + 1) & kPreMask;
                const float dl = buf_.pre_l[rd], dr = buf_.pre_r[rd];

                // SHIMMER IS BOUNDED BY THE DRY INPUT, NOT BY A GAIN SETTING.
                // A fixed feed is unstable: ReverbSc's broadband gain is modest
                // (~0.5 at this decay, measured), but its resonant modes are
                // far hotter, and host tests saw a 0.6 feed run to 12,000x.
                // So the injected shimmer may never exceed the recent dry level
                // (1 s release). The limit comes from outside the loop, so
                // the loop cannot feed itself: octaves stack while you play,
                // and once you stop the reference fades and the tail decays.
                const float lvl_in = fmaxf(fabsf(dl), fabsf(dr));
                shim_ref_ = lvl_in > shim_ref_ ? lvl_in
                                               : shim_ref_ + (lvl_in - shim_ref_) * shim_ref_rel_;
                const float lvl_sh = fmaxf(fabsf(gl), fabsf(gr));
                shim_env_ += (lvl_sh - shim_env_)
                             * (lvl_sh > shim_env_ ? shim_env_atk_ : shim_env_rel_);
                float shimmer = freeze_ ? 0.f : grain_ * 0.6f;
                if(shim_env_ * shimmer > shim_ref_)
                    shimmer = shim_ref_ / (shim_env_ + 1e-9f);

                // The shimmer takes a SHARE of the reverb's input rather than
                // adding to it: at max decay the reverb integrates whatever it
                // is fed, and dry + shimmer on top ran to 9x full scale. Since
                // the shimmer is already bounded by the dry level, dividing by
                // (1 + grain) keeps the total at or below dry alone.
                const float share = 1.f / (1.f + grain_);

                // DC-BLOCK WHAT GOES IN. The shimmer is a window-and-transpose
                // of the reverb's own output fed back into it, and a grain
                // window leaves an offset behind: measured on the host, shimmer
                // 0.8 walked the tail to -0.35 DC at feedback 0.6 and pinned it
                // at the -1.0 rail at full feedback -- silence, because the
                // line out is AC-coupled (2026-09-23). With no shimmer the same
                // reverb is clean at every feedback, so the loop is the source.
                // ReverbSc has no blocker of its own and a DC gain near 100x at
                // full decay, so the block belongs here, on its input.
                // rev_in trims the WHOLE input, shimmer included: trimming only
                // the dry left the shimmer to dominate at long decays.
                buf_.reverb->Process(dc_rev_[0].Process((dl + gl * shimmer) * share * rev_in),
                                     dc_rev_[1].Process((dr + gr * shimmer) * share * rev_in),
                                     &rev_out_[0],
                                     &rev_out_[1]);
                wl      = rev_out_[0];
                wr      = rev_out_[1];
                write_l = wl;
                write_r = wr;
            }
            else
            {
                wl = gl;
                wr = gr;
                if(T == kWash)
                    for(int s = 0; s < kDiffStages; s++)
                    {
                        wl = diff_[0][s].Process(wl, diffusion);
                        wr = diff_[1][s].Process(wr, diffusion);
                    }
                write_l = in_l + damp_[0].Lp(wl) * fb;
                write_r = in_r + damp_[1].Lp(wr) * fb;
            }

            if(!freeze_)
            {
                buf_.grain_l[write_] = write_l;
                buf_.grain_r[write_] = write_r;
                write_               = (write_ + 1) & kGrainMask;
            }

            l[i] = l[i] * dry_g + wl * wet_g;
            r[i] = r[i] * dry_g + wr * wet_g;

            // Tail level, for IsIdle(). Includes the input so a slot that is
            // still being fed never counts as idle.
            const float lvl = fmaxf(fmaxf(fabsf(wl), fabsf(wr)), fabsf(in_l) + fabsf(in_r));
            tail_env_ = lvl > tail_env_ ? lvl : tail_env_ * 0.9999f;
        }
    }

  public:
    /** Wash diffuser lengths (samples), mutually prime, L and R different
     *  for width. Set in the constructor so Reset() can't lose them. */
    Space()
    {
        static const float kLen[2][kDiffStages] = {{113, 83, 197, 241}, {127, 89, 179, 229}};
        for(int c = 0; c < 2; c++)
            for(int s = 0; s < kDiffStages; s++)
                diff_[c][s].delay = kLen[c][s];
    }

  private:
    float    sample_rate_ = 48000.f;
    Buffers  buf_         = {};
    int      type_        = kEcho;
    int      option_      = kFree;
    float    mix_ = 0.f, time_ = 0.35f, feedback_ = 0.4f, grain_ = 0.5f;
    float    spread_ = 0.5f, pitch_pot_ = 0.5f, damp_pot_ = 0.3f;
    float    semitones_ = 0.f, rate_ = 1.f;
    bool     bypass_ = false, freeze_ = false, wet_hold_ = false;
    float    tail_env_ = 0.f;
    float    grain_len_ = 4800.f, overlap_ = 2.f, offset_ = 16800.f, norm_ = 1.f;
    int      spawn_countdown_ = 1;
    size_t   write_ = 0, pre_write_ = 0;
    Grain    grains_[kMaxGrains] = {};
    int      n_grains_           = 0;
    float    window_[kWindowLen]  = {};
    float    rev_out_[2]          = {};
    DcBlocker dc_in_[2];  // effect input; the dry path bypasses it
    DcBlocker dc_rev_[2]; // reverb input; the shimmer loop makes its own DC
    float    shim_ref_ = 0.f, shim_env_ = 0.f;
    float    shim_ref_rel_ = 0.f, shim_env_atk_ = 0.f, shim_env_rel_ = 0.f;
    Smoother dry_, wet_, input_;
    OnePole  damp_[2];
    AllPass  diff_[2][kDiffStages];
    Rng      rng_;
};

} // namespace fx
} // namespace rodeo002
