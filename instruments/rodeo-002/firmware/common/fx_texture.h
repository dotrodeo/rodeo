// rodeo-002 — TEXTURE slot: degradation.
//
//   Amount pot   = Intensity (0 = transparent)
//   Shift pot    = Tone      (darker <-> brighter after the effect, 600 Hz -
//                             20 kHz; the only secondary true of all five)
//   Encoder      = type:      Crush / Downsample / Tape / Dust / Squash
//   Shift+turn   = option, per type (on the OLED, so it may vary):
//                    Crush      smooth / stepped
//                    Downsample (none)
//                    Tape       wow / flutter
//                    Dust       vinyl / radio
//                    Squash     soft / hard / pump
//
// Tape hiss and Dust crackle FOLLOW THE INPUT: they swell with the music and
// fade out ~1 s after it stops. A real vinyl bed crackles through silence, but
// on a synth that means hiss whenever nothing is held.

#pragma once

#include <cstddef>
#include <cstdint>

#include "daisysp.h"
#include "dsp_util.h"

namespace rodeo002
{
namespace fx
{
class Texture
{
  public:
    enum Type
    {
        kCrush = 0,
        kDownsample,
        kTape,
        kDust,
        kSquash,
        kNumTypes
    };
    static constexpr int kNumOptions = 3; // the most any type has

    static const char* TypeName(int t)
    {
        static const char* kNames[kNumTypes]
            = {"Crush", "Downsample", "Tape", "Dust", "Squash"};
        return (t >= 0 && t < kNumTypes) ? kNames[t] : "?";
    }
    /** How many options type `t` offers (1 = none to choose). */
    static int OptionCount(int t)
    {
        static const int kCounts[kNumTypes] = {2, 1, 2, 2, 3};
        return (t >= 0 && t < kNumTypes) ? kCounts[t] : 1;
    }
    static const char* OptionName(int t, int o)
    {
        static const char* kNames[kNumTypes][kNumOptions]
            = {{"Smooth", "Stepped", ""},
               {"-", "", ""},
               {"Wow", "Flutter", ""},
               {"Vinyl", "Radio", ""},
               {"Soft", "Hard", "Pump"}};
        return (t >= 0 && t < kNumTypes && o >= 0 && o < OptionCount(t))
                   ? kNames[t][o]
                   : "?";
    }

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        ms_          = sample_rate / 1000.f;
        for(int c = 0; c < 2; c++)
        {
            crush_[c].Init();
            crush_[c].SetDownsampleFactor(0.f); // Init defaults to heavy downsampling
            srr_[c].Init();
            band_hp_[c].SetFreq(300.f, sample_rate);
            band_lp_[c].SetFreq(3500.f, sample_rate);
        }
        rng_.Seed(0xC0FFEEu);
        wet_.Init(sample_rate, 0.02f, 0.f);
        intensity_.Init(sample_rate, 0.02f, 0.f);
        follow_rel_ = TimeCoeff(0.3f, sample_rate);
        type_       = kCrush;
        option_     = 0;
        SetAmount(0.f);
        SetTone(1.f);
        Reset();
    }

    void Reset()
    {
        for(int c = 0; c < 2; c++)
        {
            tape_[c].Reset();
            tone_[c].Reset();
            band_hp_[c].Reset();
            band_lp_[c].Reset();
            dc_[c].Reset();
            pop_[c] = 0.f;
        }
        follow_       = 0.f;
        comp_env_     = 0.f;
        comp_gain_    = 1.f;
        comp_target_  = 1.f;
        comp_counter_ = 0;
        wow_phase_ = flutter_phase_ = 0.f;
        drift_ = drift_target_ = 0.f;
    }

    void SetType(int t)
    {
        type_ = (t >= 0 && t < kNumTypes) ? t : kCrush;
        SetOption(option_); // re-clamp to the new type's range
    }
    int GetType() const { return type_; }

    void SetOption(int o)
    {
        const int count = OptionCount(type_);
        option_         = (o >= 0 && o < count) ? o : 0;
        UpdateControl();
    }
    int GetOption() const { return option_; }

    void SetAmount(float a)
    {
        amount_ = Clamp(a, 0.f, 1.f);
        wet_.Set(Clamp(amount_ * 4.f, 0.f, 1.f));
        intensity_.Set(amount_);
        UpdateControl();
    }

    void SetTone(float t)
    {
        const float hz = ExpMap(t, 600.f, 20000.f);
        for(int c = 0; c < 2; c++)
            tone_[c].SetFreq(hz, sample_rate_);
    }

    void ProcessBlock(float* l, float* r, size_t n)
    {
        switch(type_)
        {
            case kCrush: RunSimple<kCrush>(l, r, n); break;
            case kDownsample: RunSimple<kDownsample>(l, r, n); break;
            case kTape: RunTape(l, r, n); break;
            case kDust: RunDust(l, r, n); break;
            default: RunSquash(l, r, n); break;
        }
    }

  private:
    static constexpr size_t kTapeLen = 1024; // 21 ms; power of two

    // Everything that needs a powf or a DaisySP setter: control rate only.
    void UpdateControl()
    {
        const float a = amount_;
        for(int c = 0; c < 2; c++)
        {
            if(option_ == 1) // stepped: whole bits, 0-14 removed
                crush_[c].SetBitsToCrush(static_cast<uint8_t>(a * 14.f));
            else // smooth: continuous through the bit depths
            {
                crush_[c].SetBitcrushFactor(a * 0.875f);
                crush_[c].SetSmoothCrushing(true);
            }
            srr_[c].SetFreq(ExpMap(a, 1.f, 0.01f)); // full rate down to ~480 Hz
        }
        // Squash: threshold falls with intensity; the option sets the character.
        //
        // kMakeup is the share of the theoretical make-up gain that is actually
        // applied. It was 0.5, which left a nominal -6 dB signal coming out 7.5
        // dB down on Soft and 11.3 dB down on Pump -- Squash was a volume
        // control with a compressor attached, and nobody would reach for it
        // twice. At 0.85 all three options land within half a decibel of unity,
        // and they land there TOGETHER, which is what says 0.85 is the right
        // number rather than a fudge: the compensation now tracks the ratio
        // instead of undershooting it (measured 2026-09-29, see
        // FxReview.SquashLosesLevelOnANominalSignal).
        static constexpr float kMakeup = 0.85f;
        const float thr_db = -6.f - 30.f * a;
        comp_ratio_        = option_ == 0 ? 3.f : (option_ == 1 ? 10.f : 20.f);
        comp_thr_          = powf(10.f, thr_db / 20.f);
        comp_makeup_ = powf(10.f, (-thr_db * (1.f - 1.f / comp_ratio_) * kMakeup) / 20.f);
        comp_atk_ = TimeCoeff(option_ == 2 ? 0.001f : (option_ == 1 ? 0.003f : 0.01f),
                              sample_rate_);
        comp_rel_ = TimeCoeff(option_ == 2 ? 0.3f : (option_ == 1 ? 0.1f : 0.15f),
                              sample_rate_);
    }

    // Tracks the input so noise can follow it.
    float Follow(float l, float r)
    {
        const float lvl = fmaxf(fabsf(l), fabsf(r));
        follow_ = lvl > follow_ ? lvl : follow_ + (lvl - follow_) * follow_rel_;
        return Clamp(follow_ * 4.f, 0.f, 1.f);
    }

    template <int T>
    void RunSimple(float* l, float* r, size_t n)
    {
        for(size_t i = 0; i < n; i++)
        {
            const float wet   = wet_.Next();
            float*      ch[2] = {&l[i], &r[i]};
            for(int c = 0; c < 2; c++)
            {
                const float dry = *ch[c];
                float       y   = T == kCrush ? crush_[c].Process(dry)
                                              : srr_[c].Process(dry);
                y      = dc_[c].Process(tone_[c].Lp(y));
                *ch[c] = dry + (y - dry) * wet;
            }
        }
    }

    // Wow/flutter through a short modulated delay, gentle saturation, hiss.
    void RunTape(float* l, float* r, size_t n)
    {
        const bool  flutter = option_ == 1;
        // Wow: slow, deep, drifting. Flutter: fast and shallow, with a little wow.
        const float wow_hz  = 0.7f, flutter_hz = 9.f;
        const float wow_ms  = flutter ? 0.3f : 2.5f;
        const float flt_ms  = flutter ? 0.25f : 0.03f;
        for(size_t i = 0; i < n; i++)
        {
            const float in  = intensity_.Next();
            const float wet = wet_.Next();

            wow_phase_ += wow_hz / sample_rate_;
            if(wow_phase_ >= 1.f)
            {
                wow_phase_ -= 1.f;
                drift_target_ = rng_.Bipolar(); // a new drift each wow cycle
            }
            flutter_phase_ += flutter_hz / sample_rate_;
            if(flutter_phase_ >= 1.f)
                flutter_phase_ -= 1.f;
            drift_ += (drift_target_ - drift_) * 0.00002f;

            // Same modulation on both channels -- one tape, one capstan.
            const float mod = wow_ms * (0.5f * (FastSin01(wow_phase_) + 1.f) + 0.5f * drift_)
                              + flt_ms * FastSin01(flutter_phase_);
            const float delay = (6.f + in * mod) * ms_;
            const float hiss  = in * 0.006f * Follow(l[i], r[i]);
            const float drive = 1.f + 1.5f * in;

            float* ch[2] = {&l[i], &r[i]};
            for(int c = 0; c < 2; c++)
            {
                const float dry = *ch[c];
                tape_[c].Write(dry);
                float y = tape_[c].ReadHermite(delay);
                y       = FastTanh(y * drive) / (1.f + 0.5f * in) + hiss * rng_.Bipolar();
                y       = dc_[c].Process(tone_[c].Lp(y));
                *ch[c]  = dry + (y - dry) * wet;
            }
        }
    }

    // Crackle and a noise bed. Radio also band-limits the signal itself.
    void RunDust(float* l, float* r, size_t n)
    {
        const bool  radio   = option_ == 1;
        const float density = radio ? 0.003f : 0.0006f; // pops per sample at full
        const float pop_amp = radio ? 0.25f : 0.8f;
        const float bed     = radio ? 0.04f : 0.008f;
        for(size_t i = 0; i < n; i++)
        {
            const float in    = intensity_.Next();
            const float wet   = wet_.Next();
            const float level = Follow(l[i], r[i]);

            float* ch[2] = {&l[i], &r[i]};
            for(int c = 0; c < 2; c++)
            {
                const float dry = *ch[c];
                float       y   = dry;
                if(radio)
                    y = band_lp_[c].Lp(band_hp_[c].Hp(y));
                // A pop is an impulse that decays over a few samples: a click.
                if(rng_.Uniform() < density * in)
                    pop_[c] = pop_amp * rng_.Bipolar();
                pop_[c] *= 0.7f;
                y += (pop_[c] + bed * rng_.Bipolar()) * in * level;
                y      = dc_[c].Process(tone_[c].Lp(y));
                *ch[c] = dry + (y - dry) * wet;
            }
        }
    }

    // Stereo-linked compressor. The gain law needs a powf, so it is evaluated
    // every 16 samples and smoothed in between; a soft clip keeps attack
    // overshoot bounded.
    void RunSquash(float* l, float* r, size_t n)
    {
        for(size_t i = 0; i < n; i++)
        {
            const float wet = wet_.Next();
            const float lvl = fmaxf(fabsf(l[i]), fabsf(r[i]));
            comp_env_ += (lvl - comp_env_) * (lvl > comp_env_ ? comp_atk_ : comp_rel_);

            if(++comp_counter_ >= 16)
            {
                comp_counter_ = 0;
                comp_target_  = comp_env_ > comp_thr_
                                    ? powf(comp_env_ / comp_thr_, 1.f / comp_ratio_ - 1.f)
                                    : 1.f;
            }
            comp_gain_ += (comp_target_ - comp_gain_) * 0.1f;
            const float g = comp_gain_ * comp_makeup_;

            float* ch[2] = {&l[i], &r[i]};
            for(int c = 0; c < 2; c++)
            {
                const float dry = *ch[c];
                const float y   = dc_[c].Process(tone_[c].Lp(FastTanh(dry * g)));
                *ch[c]          = dry + (y - dry) * wet;
            }
        }
    }

    float                                 sample_rate_ = 48000.f;
    float                                 ms_          = 48.f;
    int                                   type_        = kCrush;
    int                                   option_      = 0;
    float                                 amount_      = 0.f;
    Smoother                              wet_, intensity_;
    OnePole                               tone_[2], band_hp_[2], band_lp_[2];
    /** THE WET PATH IS DC-BLOCKED, the dry path is not -- the same arrangement
     *  the Character slot has had all along, and for a sharper reason here.
     *
     *  Crush quantises by an arithmetic shift, which truncates toward minus
     *  infinity rather than rounding, so every sample is rounded DOWN by up to
     *  a step. At full crush the step is 0.25 of full scale and the mean error
     *  is half of that: measured -0.125 of DC at the chain output, which
     *  nothing downstream removed. The output stage is tilt, volume and the
     *  limiter, so that went to the codec -- and into Space's feedback, where
     *  DC is amplified, and where a railed output is heard as SILENCE because
     *  the line out is AC-coupled. That is the fault we already fixed once
     *  inside Space; this is the same fault arriving from upstream
     *  (2026-09-29). */
    DcBlocker                             dc_[2];
    Rng                                   rng_;
    daisysp::Decimator                    crush_[2];
    daisysp::SampleRateReducer            srr_[2];
    daisysp::DelayLine<float, kTapeLen>   tape_[2];
    float                                 pop_[2] = {};
    float follow_ = 0.f, follow_rel_ = 0.f;
    float wow_phase_ = 0.f, flutter_phase_ = 0.f, drift_ = 0.f, drift_target_ = 0.f;
    float comp_env_ = 0.f, comp_gain_ = 1.f, comp_target_ = 1.f;
    float comp_thr_ = 1.f, comp_ratio_ = 3.f, comp_makeup_ = 1.f;
    float comp_atk_ = 0.f, comp_rel_ = 0.f;
    int   comp_counter_ = 0;
};

} // namespace fx
} // namespace rodeo002
