// rodeo-002 — the four-slot texture chain, for cost measurement.
//
//   [CHARACTER] -> [MOVEMENT] -> [TEXTURE] -> [SPACE]
//    overdrive     chorus        decimator   granular delay (SDRAM)
//    + wavefolder  (per channel) (bitcrush)  + freeze
//
// Each panel slot offers several effect types; this implements ONE per slot --
// the one I expect to be the most expensive -- because the benchmark needs an
// upper bound on the chain's cost, not a feature-complete effect set. The
// cheaper alternates (vibrato instead of chorus, saturate instead of fold) can
// only come in under these numbers.
//
// SPACE is the exception: it is implemented properly, because it is the whole
// point of the instrument and nothing else would tell us what it costs. It is a
// real granular delay -- a circular SDRAM buffer, overlapping windowed grains
// with independent playback rates, freeze, and a feedback path.

#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "daisysp.h"
#include "daisy_seed.h"

namespace rodeo002
{
// ---------------------------------------------------------------------------
// SPACE — granular delay
// ---------------------------------------------------------------------------

static constexpr float kGrainBufSeconds = 4.f;
static constexpr size_t kGrainBufLen
    = static_cast<size_t>(48000.f * kGrainBufSeconds);
static constexpr int kMaxGrains = 12;

// The buffer lives in the Seed's 64 MB SDRAM. 4 s stereo float is 1.5 MB, so
// buffer length is nowhere near a constraint -- SDRAM is simply slower than
// internal SRAM, and putting it here is part of what the benchmark measures.
extern float DSY_SDRAM_BSS g_grain_buf_l[kGrainBufLen];
extern float DSY_SDRAM_BSS g_grain_buf_r[kGrainBufLen];

class GranularDelay
{
  public:
    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        write_       = 0;
        for(size_t i = 0; i < kGrainBufLen; i++)
        {
            g_grain_buf_l[i] = 0.f;
            g_grain_buf_r[i] = 0.f;
        }
        for(int i = 0; i < kMaxGrains; i++)
            grains_[i].active = false;
        // Hann window, sampled once -- a cosf per grain per sample would cost
        // more than the grain itself.
        for(int i = 0; i < kWindowLen; i++)
        {
            const float t = static_cast<float>(i)
                            / static_cast<float>(kWindowLen - 1);
            window_[i] = 0.5f - 0.5f * cosf(2.f * static_cast<float>(M_PI) * t);
        }
        SetTime(0.35f);
        SetFeedback(0.4f);
        SetGrain(0.5f);
        SetMix(0.5f);
        SetPitch(0.f);
    }

    void SetTime(float seconds)
    {
        delay_samples_ = daisysp::fclamp(seconds * sample_rate_,
                                         480.f,
                                         static_cast<float>(kGrainBufLen - 1));
    }
    void SetFeedback(float f) { feedback_ = daisysp::fclamp(f, 0.f, 1.1f); }
    /** One macro for size and density: small/sparse .. long/dense. */
    void SetGrain(float g)
    {
        g            = daisysp::fclamp(g, 0.f, 1.f);
        grain_len_   = (0.02f + 0.28f * g) * sample_rate_;
        spawn_period_ = grain_len_ / (1.f + 3.f * g); // overlap grows with g
    }
    void SetMix(float m) { mix_ = daisysp::fclamp(m, 0.f, 1.f); }
    /** Grain transposition in semitones (shimmer at +12). */
    void SetPitch(float semitones) { rate_ = powf(2.f, semitones / 12.f); }
    void SetSpread(float s) { spread_ = daisysp::fclamp(s, 0.f, 1.f); }
    void SetFreeze(bool f) { freeze_ = f; }

    void Process(float in_l, float in_r, float* out_l, float* out_r)
    {
        // Spawn on schedule.
        if(--spawn_countdown_ <= 0)
        {
            spawn_countdown_ = static_cast<int>(spawn_period_);
            if(spawn_countdown_ < 16)
                spawn_countdown_ = 16;
            SpawnGrain();
        }

        float wet_l = 0.f, wet_r = 0.f;
        for(int i = 0; i < kMaxGrains; i++)
        {
            Grain& g = grains_[i];
            if(!g.active)
                continue;

            const float t = g.age / g.len; // 0..1 through the window
            if(t >= 1.f)
            {
                g.active = false;
                continue;
            }
            const float env = WindowAt(t);
            wet_l += ReadInterp(g_grain_buf_l, g.pos) * env;
            wet_r += ReadInterp(g_grain_buf_r, g.pos) * env;

            g.pos += g.rate;
            if(g.pos >= static_cast<float>(kGrainBufLen))
                g.pos -= static_cast<float>(kGrainBufLen);
            g.age += 1.f;
        }

        // Normalise for overlap so density changes don't change level.
        wet_l *= 0.5f;
        wet_r *= 0.5f;

        if(!freeze_)
        {
            g_grain_buf_l[write_] = in_l + wet_l * feedback_;
            g_grain_buf_r[write_] = in_r + wet_r * feedback_;
            if(++write_ >= kGrainBufLen)
                write_ = 0;
        }

        *out_l = in_l + (wet_l - in_l) * mix_;
        *out_r = in_r + (wet_r - in_r) * mix_;
    }

  private:
    struct Grain
    {
        float pos;
        float rate;
        float len;
        float age;
        bool  active;
    };

    void SpawnGrain()
    {
        for(int i = 0; i < kMaxGrains; i++)
        {
            if(grains_[i].active)
                continue;
            const float jitter
                = spread_ * RandUnit() * 0.25f * delay_samples_;
            float start = static_cast<float>(write_) - delay_samples_ + jitter;
            while(start < 0.f)
                start += static_cast<float>(kGrainBufLen);
            grains_[i].pos    = start;
            grains_[i].rate   = rate_;
            grains_[i].len    = grain_len_;
            grains_[i].age    = 0.f;
            grains_[i].active = true;
            return;
        }
        // All grains busy: drop this one. Density is bounded by kMaxGrains,
        // which is also what bounds the worst-case cost measured here.
    }

    static float RandUnit()
    {
        return 2.f * (static_cast<float>(rand()) / static_cast<float>(RAND_MAX))
               - 1.f;
    }

    float WindowAt(float t) const
    {
        const float x = t * static_cast<float>(kWindowLen - 1);
        const int   i = static_cast<int>(x);
        const float f = x - static_cast<float>(i);
        const int   j = (i + 1 < kWindowLen) ? i + 1 : kWindowLen - 1;
        return window_[i] + (window_[j] - window_[i]) * f;
    }

    static float ReadInterp(const float* buf, float pos)
    {
        const size_t i = static_cast<size_t>(pos);
        const float  f = pos - static_cast<float>(i);
        const size_t j = (i + 1 < kGrainBufLen) ? i + 1 : 0;
        return buf[i] + (buf[j] - buf[i]) * f;
    }

    static constexpr int kWindowLen = 512;

    Grain  grains_[kMaxGrains];
    float  window_[kWindowLen];
    size_t write_           = 0;
    float  sample_rate_     = 48000.f;
    float  delay_samples_   = 16800.f;
    float  feedback_        = 0.4f;
    float  grain_len_       = 4800.f;
    float  spawn_period_    = 2400.f;
    int    spawn_countdown_ = 1;
    float  mix_             = 0.5f;
    float  rate_            = 1.f;
    float  spread_          = 0.5f;
    bool   freeze_          = false;
};

// ---------------------------------------------------------------------------
// The whole chain
// ---------------------------------------------------------------------------

class TextureChain
{
  public:
    void Init(float sample_rate)
    {
        for(int c = 0; c < 2; c++)
        {
            drive_[c].Init();
            fold_[c].Init();
            chorus_[c].Init(sample_rate);
            chorus_[c].SetLfoFreq(0.3f + 0.2f * c); // decorrelate L/R
            chorus_[c].SetLfoDepth(0.6f);
            chorus_[c].SetDelayMs(12.f);
            chorus_[c].SetFeedback(0.25f);
            crush_[c].Init();
            crush_[c].SetDownsampleFactor(0.35f);
            crush_[c].SetBitsToCrush(6);
        }
        for(int c = 0; c < 2; c++)
        {
            drive_[c].SetDrive(0.5f);
            fold_[c].SetGain(2.f);
        }
        space_.Init(sample_rate);
        SetAmounts(0.5f, 0.5f, 0.5f, 0.5f);
    }

    void SetAmounts(float character, float movement, float texture, float space)
    {
        character_ = character;
        movement_  = movement;
        texture_   = texture;
        space_.SetMix(space);
    }

    GranularDelay& Space() { return space_; }

    void Process(float in_l, float in_r, float* out_l, float* out_r)
    {
        float l = in_l, r = in_r;

        // CHARACTER — drive into a fold, blended by amount.
        {
            const float dl = fold_[0].Process(drive_[0].Process(l));
            const float dr = fold_[1].Process(drive_[1].Process(r));
            l += (dl - l) * character_;
            r += (dr - r) * character_;
        }
        // MOVEMENT — chorus per channel.
        {
            const float cl = chorus_[0].Process(l);
            const float cr = chorus_[1].Process(r);
            l += (cl - l) * movement_;
            r += (cr - r) * movement_;
        }
        // TEXTURE — decimate / bitcrush.
        {
            const float tl = crush_[0].Process(l);
            const float tr = crush_[1].Process(r);
            l += (tl - l) * texture_;
            r += (tr - r) * texture_;
        }
        // SPACE — granular delay (has its own wet/dry).
        space_.Process(l, r, out_l, out_r);
    }

  private:
    daisysp::Overdrive   drive_[2];
    daisysp::Wavefolder  fold_[2];
    daisysp::ChorusEngine chorus_[2];
    daisysp::Decimator   crush_[2];
    GranularDelay        space_;
    float                character_ = 0.5f;
    float                movement_  = 0.5f;
    float                texture_   = 0.5f;
};

} // namespace rodeo002
