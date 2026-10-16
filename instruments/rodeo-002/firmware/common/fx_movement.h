// rodeo-002 — MOVEMENT slot: modulation.
//
//   Amount pot   = Depth  (0 = transparent)
//   Shift pot    = Rate   (0.05 - 12 Hz, exponential)
//   Encoder      = type:   Chorus / Vibrato / Phaser / Flanger / Tremolo
//   Shift+turn   = option: LFO shape, sine / triangle / square / random
//
// Every type runs from the slot's own LFO, which is what makes the LFO-shape
// option work across all five. DaisySP's Chorus/Flanger/Phaser each carry a
// fixed internal LFO, so the delay-based types are built on DaisySP's
// DelayLine instead, and the phaser and tremolo are small loops of their own.
// One delay-line pair is shared by Chorus, Vibrato and Flanger -- only one
// type runs at a time.

#pragma once

#include <cstddef>

#include "daisysp.h"
#include "dsp_util.h"

namespace rodeo002
{
namespace fx
{
class Movement
{
  public:
    enum Type
    {
        kChorus = 0,
        kVibrato,
        kPhaser,
        kFlanger,
        kTremolo,
        kNumTypes
    };
    static constexpr int kNumOptions = Lfo::kNumShapes;

    static const char* TypeName(int t)
    {
        static const char* kNames[kNumTypes]
            = {"Chorus", "Vibrato", "Phaser", "Flanger", "Tremolo"};
        return (t >= 0 && t < kNumTypes) ? kNames[t] : "?";
    }
    static const char* OptionName(int o)
    {
        static const char* kNames[kNumOptions] = {"Sine", "Triangle", "Square", "Random"};
        return (o >= 0 && o < kNumOptions) ? kNames[o] : "?";
    }

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        ms_          = sample_rate / 1000.f;
        lfo_.Init(sample_rate);
        depth_.Init(sample_rate, 0.02f, 0.f);
        wet_.Init(sample_rate, 0.02f, 0.f);
        SetType(kChorus);
        SetAmount(0.f);
        SetRate(0.3f);
        SetOption(Lfo::kSine);
        Reset();
    }

    void Reset()
    {
        for(int c = 0; c < 2; c++)
        {
            dl_[c].Reset();
            for(int s = 0; s < kPhaserStages; s++)
                ap_[c][s] = 0.f;
            fb_[c] = 0.f;
        }
    }

    void SetType(int t) { type_ = (t >= 0 && t < kNumTypes) ? t : kChorus; }
    int  GetType() const { return type_; }

    void SetAmount(float a)
    {
        a = Clamp(a, 0.f, 1.f);
        depth_.Set(a);
        wet_.Set(Clamp(a * 4.f, 0.f, 1.f));
    }

    void SetRate(float r) { lfo_.SetFreq(ExpMap(r, 0.05f, 12.f)); }
    void SetOption(int o) { lfo_.SetShape(o); }

    void ProcessBlock(float* l, float* r, size_t n)
    {
        switch(type_)
        {
            case kChorus: RunDelay<kChorus>(l, r, n); break;
            case kVibrato: RunDelay<kVibrato>(l, r, n); break;
            case kFlanger: RunDelay<kFlanger>(l, r, n); break;
            case kPhaser: RunPhaser(l, r, n); break;
            default: RunTremolo(l, r, n); break;
        }
    }

  private:
    static constexpr int    kPhaserStages = 6;
    static constexpr size_t kDelayLen     = 1024; // 21 ms; power of two

    // Chorus, Vibrato, Flanger: one modulated delay per channel.
    template <int T>
    void RunDelay(float* l, float* r, size_t n)
    {
        // Per type: base delay and modulation span (ms), stereo LFO offset,
        // feedback, and how much of the wet replaces the dry at full amount.
        const float base_ms = T == kChorus ? 10.f : (T == kVibrato ? 4.f : 1.f);
        const float span_ms = T == kChorus ? 5.f : (T == kVibrato ? 3.f : 4.f);
        const float offset  = T == kVibrato ? 0.f : 0.25f;
        const float fb      = T == kFlanger ? 0.65f : 0.f;
        const float mix     = T == kVibrato ? 1.f : 0.5f;

        for(size_t i = 0; i < n; i++)
        {
            const float d   = depth_.Next();
            const float wet = wet_.Next() * mix;
            float       m[2];
            lfo_.Process(offset, &m[0], &m[1]);

            float* ch[2] = {&l[i], &r[i]};
            for(int c = 0; c < 2; c++)
            {
                const float dry = *ch[c];
                // (m + 1) / 2 keeps the delay one-sided of the base, so the
                // base is the shortest the delay gets -- no zero-delay combing.
                const float delay
                    = (base_ms + span_ms * d * 0.5f * (m[c] + 1.f)) * ms_;
                const float y = dl_[c].ReadHermite(delay);
                dl_[c].Write(dry + y * fb);
                *ch[c] = dry + (y - dry) * wet;
            }
        }
    }

    // Six first-order all-passes per channel, swept by the LFO, with feedback.
    void RunPhaser(float* l, float* r, size_t n)
    {
        for(size_t i = 0; i < n; i++)
        {
            const float d   = depth_.Next();
            const float wet = wet_.Next() * 0.5f;
            float       m[2];
            lfo_.Process(0.25f, &m[0], &m[1]);

            float* ch[2] = {&l[i], &r[i]};
            for(int c = 0; c < 2; c++)
            {
                const float dry = *ch[c];
                // Squaring the 0..1 sweep approximates an exponential sweep in
                // Hz without a powf per sample.
                const float u = 0.5f * (m[c] + 1.f);
                const float f = 200.f + 4000.f * d * u * u;
                // First-order all-pass coefficient; tan(w) ~= w is within 3%
                // at the top of the sweep.
                const float w = kPi * f / sample_rate_;
                const float k = (w - 1.f) / (w + 1.f);

                float x = dry + fb_[c] * 0.5f;
                for(int s = 0; s < kPhaserStages; s++)
                {
                    const float y = k * x + ap_[c][s];
                    ap_[c][s]     = x - k * y;
                    x             = y;
                }
                fb_[c] = x;
                *ch[c] = dry + (x - dry) * wet;
            }
        }
    }

    // Amplitude modulation; L and R half a cycle apart, so it pans.
    void RunTremolo(float* l, float* r, size_t n)
    {
        for(size_t i = 0; i < n; i++)
        {
            const float d = depth_.Next();
            float       m[2];
            lfo_.Process(0.5f, &m[0], &m[1]);
            l[i] *= 1.f - d * 0.5f * (1.f - m[0]);
            r[i] *= 1.f - d * 0.5f * (1.f - m[1]);
        }
    }

    float                                  sample_rate_ = 48000.f;
    float                                  ms_          = 48.f;
    int                                    type_        = kChorus;
    Lfo                                    lfo_;
    Smoother                               depth_, wet_;
    daisysp::DelayLine<float, kDelayLen>   dl_[2];
    float                                  ap_[2][kPhaserStages] = {};
    float                                  fb_[2]                = {};
};

} // namespace fx
} // namespace rodeo002
