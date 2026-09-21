// rodeo-002 — CHARACTER slot: waveshaping.
//
//   Amount pot   = Drive  (0 = transparent; the first quarter fades the effect
//                          in, the rest raises the drive)
//   Shift pot    = Bias   (0.5 = symmetric; either side pushes the signal off
//                          centre into the shaper, trading odd harmonics for
//                          even ones -- true of every type, hence printable)
//   Encoder      = type:   Warm / Drive / Fuzz / Fold / Rectify
//   Shift+turn   = option: low cut before the shaper, off / 100 Hz / 300 Hz
//
// Processes a block in place. The type switch sits outside the sample loop, so
// each type's loop is tight. Levels are first-pass and meant to be tuned by
// ear on hardware.

#pragma once

#include <cstddef>

#include "daisysp.h"
#include "dsp_util.h"

namespace rodeo002
{
namespace fx
{
class Character
{
  public:
    enum Type
    {
        kWarm = 0,
        kDrive,
        kFuzz,
        kFold,
        kRectify,
        kNumTypes
    };
    enum LowCut
    {
        kCutOff = 0,
        kCut100,
        kCut300,
        kNumOptions
    };

    static const char* TypeName(int t)
    {
        static const char* kNames[kNumTypes]
            = {"Warm", "Drive", "Fuzz", "Fold", "Rectify"};
        return (t >= 0 && t < kNumTypes) ? kNames[t] : "?";
    }
    static const char* OptionName(int o)
    {
        static const char* kNames[kNumOptions] = {"Cut off", "Cut 100", "Cut 300"};
        return (o >= 0 && o < kNumOptions) ? kNames[o] : "?";
    }

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        for(int c = 0; c < 2; c++)
        {
            drive_[c].Init();
            fold_[c].Init();
        }
        wet_.Init(sample_rate, 0.01f, 0.f);
        gain_.Init(sample_rate, 0.01f, 1.f);
        bias_.Init(sample_rate, 0.01f, 0.f);
        SetType(kWarm);
        SetOption(kCutOff);
        SetAmount(0.f);
        SetBias(0.5f);
        Reset();
    }

    /** Clear filter/shaper state -- the chain calls this on a type change,
     *  while the slot is faded out. */
    void Reset()
    {
        for(int c = 0; c < 2; c++)
        {
            cut_[c].Reset();
            dc_[c].Reset();
        }
    }

    void SetType(int t) { type_ = (t >= 0 && t < kNumTypes) ? t : kWarm; UpdateGain(); }
    int  GetType() const { return type_; }

    void SetAmount(float a)
    {
        amount_ = Clamp(a, 0.f, 1.f);
        wet_.Set(Clamp(amount_ * 4.f, 0.f, 1.f));
        UpdateGain();
    }

    void SetBias(float b) { bias_.Set((Clamp(b, 0.f, 1.f) - 0.5f) * 2.f); }

    void SetOption(int o)
    {
        option_ = (o >= 0 && o < kNumOptions) ? o : kCutOff;
        const float hz = option_ == kCut300 ? 300.f : 100.f;
        for(int c = 0; c < 2; c++)
            cut_[c].SetFreq(hz, sample_rate_);
    }

    void ProcessBlock(float* l, float* r, size_t n)
    {
        switch(type_)
        {
            case kWarm: Run<kWarm>(l, r, n); break;
            case kDrive: Run<kDrive>(l, r, n); break;
            case kFuzz: Run<kFuzz>(l, r, n); break;
            case kFold: Run<kFold>(l, r, n); break;
            default: Run<kRectify>(l, r, n); break;
        }
    }

  private:
    // Drive range per type. Control rate: the smoother interpolates per sample.
    void UpdateGain()
    {
        const float a = amount_;
        switch(type_)
        {
            case kWarm: gain_.Set(1.f + 7.f * a); break;
            case kDrive:
                // DaisySP Overdrive, measured on the host: its pre-gain is zero
                // at drive 0 (it outputs silence), it holds peak output at 1.0
                // from 0.4 to 0.9, and at exactly 1.0 its make-up gain jumps
                // and the output hits 3.1x full scale. So: 0.3 to 0.9 only.
                for(int c = 0; c < 2; c++)
                    drive_[c].SetDrive(0.3f + 0.6f * a);
                gain_.Set(1.f);
                break;
            case kFuzz: gain_.Set(ExpMap(a, 2.f, 100.f)); break;
            case kFold: gain_.Set(1.f + 7.f * a); break;
            default: gain_.Set(1.f + 5.f * a); break; // kRectify
        }
    }

    template <int T>
    float Shape(int c, float x, float g, float b)
    {
        switch(T)
        {
            case kWarm:
            {
                // Subtracting tanh(bias) removes the static offset at once;
                // the DC blocker only has to catch what the signal adds.
                const float k = b * 0.5f;
                return (FastTanh(g * x + k) - FastTanh(k)) * 0.8f;
            }
            case kDrive:
                // Trimmed: Overdrive is ~2.8x louder (RMS) than its input.
                return drive_[c].Process(x + b * 0.3f) * 0.5f;
            case kFuzz:
            {
                // Hard clip. Large bias pins one side, so quiet passages drop
                // out -- the gated, sputtery end of the knob.
                const float k = b * 0.9f;
                return (Clamp(g * x + k, -1.f, 1.f) - Clamp(k, -1.f, 1.f)) * 0.7f;
            }
            case kFold:
                fold_[c].SetGain(g);
                fold_[c].SetOffset(b * 0.5f); // Wavefolder adds offset pre-gain
                return fold_[c].Process(x) * 0.8f;
            default: // kRectify: bias blends full-wave (octave up) into half-wave
            {
                const float y    = FastTanh(g * x);
                const float t    = (b + 1.f) * 0.5f;
                const float full = fabsf(y);
                const float half = y > 0.f ? y : 0.f;
                return (full + (half - full) * t) * 1.4f;
            }
        }
    }

    template <int T>
    void Run(float* l, float* r, size_t n)
    {
        const bool cut = option_ != kCutOff;
        for(size_t i = 0; i < n; i++)
        {
            const float wet = wet_.Next();
            const float g   = gain_.Next();
            const float b   = bias_.Next();

            float* ch[2] = {&l[i], &r[i]};
            for(int c = 0; c < 2; c++)
            {
                const float dry = *ch[c];
                float       x   = cut ? cut_[c].Hp(dry) : dry;
                const float y   = dc_[c].Process(Shape<T>(c, x, g, b));
                *ch[c]          = dry + (y - dry) * wet;
            }
        }
    }

    float                sample_rate_ = 48000.f;
    int                  type_        = kWarm;
    int                  option_      = kCutOff;
    float                amount_      = 0.f;
    Smoother             wet_, gain_, bias_;
    OnePole              cut_[2];
    DcBlocker            dc_[2];
    daisysp::Overdrive   drive_[2];
    daisysp::Wavefolder  fold_[2];
};

} // namespace fx
} // namespace rodeo002
