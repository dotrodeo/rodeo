// Rodeo DSP utilities — the small building blocks, host-safe and dependency-free.
//
// Same philosophy as adc_convert.h and pot_ui.h: pure math and state, no
// libDaisy, no hardware, so it builds and is tested on the host and the
// firmware calls THESE functions rather than a reimplementation of them.
//
// Written for rodeo-002 and moved here on 2026-10-01 because none of it is
// about a synthesiser. rodeo-001 had already hand-rolled its own one-pole
// smoother inline in main.cpp, which is the duplication this prevents the third
// instrument from repeating.
//
// A note on what is NOT here: anything that needs a sample rate at
// construction. Every type takes its rate in Init(), so one header serves an
// audio-rate and a block-rate user in the same build -- which matters, because
// getting that wrong is exactly how rodeo-002's LFO ended up running 48x slow
// (initialised at the audio rate, ticked once per block).

#pragma once

#include <cmath>
#include <cstdint>

namespace rodeo
{
constexpr float kPi    = 3.14159265358979f;
constexpr float kTwoPi = 2.f * kPi;

inline float Clamp(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

/** Exponential map of t in 0..1 onto lo..hi (lo > hi works too). Control rate. */
inline float ExpMap(float t, float lo, float hi)
{
    return lo * powf(hi / lo, Clamp(t, 0.f, 1.f));
}

/** One-pole coefficient for a time constant in seconds. Control rate. */
inline float TimeCoeff(float seconds, float sample_rate)
{
    return 1.f - expf(-1.f / (seconds * sample_rate));
}

/** One-pole coefficient for a cutoff frequency in Hz. Control rate. */
inline float FreqCoeff(float hz, float sample_rate)
{
    return 1.f - expf(-kTwoPi * hz / sample_rate);
}

/** Padé tanh: within ~2% of tanhf, exactly +/-1 beyond +/-3, and continuous
 *  there. Several times cheaper than tanhf, which matters per sample. */
inline float FastTanh(float x)
{
    if(x > 3.f)
        return 1.f;
    if(x < -3.f)
        return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

/** sin(2*pi*p) for p in [0, 1), parabolic approximation (~0.1% error). */
inline float FastSin01(float p)
{
    const float t  = p < 0.5f ? 2.f * p : 2.f * p - 2.f; // -1..1 over a cycle
    const float y  = 4.f * t * (1.f - fabsf(t));
    return 0.225f * (y * fabsf(y) - y) + y;
}

/** xorshift32 -- deterministic, lock-free, and far cheaper than rand(). */
struct Rng
{
    uint32_t state = 0x9E3779B9u;

    void Seed(uint32_t s) { state = s ? s : 0x9E3779B9u; }

    uint32_t Next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    /** 0..1 */
    float Uniform() { return (Next() >> 8) * (1.f / 16777216.f); }
    /** -1..1 */
    float Bipolar() { return Uniform() * 2.f - 1.f; }
};

/** Per-sample one-pole smoother for control values. Set() the target from the
 *  control rate, Next() once per sample. Kills zipper noise from pots that only
 *  update ~100 times a second. */
struct Smoother
{
    float cur = 0.f, target = 0.f, coeff = 0.01f;

    void  Init(float sample_rate, float seconds, float value)
    {
        coeff = TimeCoeff(seconds, sample_rate);
        cur = target = value;
    }
    void  Set(float v) { target = v; }
    void  Snap(float v) { cur = target = v; }
    float Next()
    {
        cur += (target - cur) * coeff;
        return cur;
    }
    bool Settled(float eps = 1e-4f) const { return fabsf(target - cur) < eps; }
};

/** One-pole low-pass. The high-pass is x - Lp(x). */
struct OnePole
{
    float y = 0.f, a = 1.f;

    void  SetFreq(float hz, float sample_rate) { a = FreqCoeff(hz, sample_rate); }
    void  Reset() { y = 0.f; }
    float Lp(float x)
    {
        y += a * (x - y);
        return y;
    }
    float Hp(float x) { return x - Lp(x); }
};

/** DC blocker (~7 Hz at 48 kHz). Waveshapers with a bias offset make DC. */
struct DcBlocker
{
    float x1 = 0.f, y1 = 0.f;

    void  Reset() { x1 = y1 = 0.f; }
    float Process(float x)
    {
        const float y = x - x1 + 0.999f * y1;
        x1            = x;
        y1            = y;
        return y;
    }
};

/** Two-phase LFO with the four Movement shapes. One phase accumulator; the
 *  second output is offset by a fraction of a cycle (stereo width, autopan). */
class Lfo
{
  public:
    enum Shape
    {
        kSine = 0,
        kTriangle,
        kSquare,
        kRandom,
        kNumShapes
    };

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        phase_       = 0.f;
        inc_         = 1.f / sample_rate;
        shape_       = kSine;
        rng_.Seed(0x1234567u);
        for(int c = 0; c < 2; c++)
        {
            smooth_[c] = 0.f;
            target_[c] = 0.f;
        }
        // Square edges are rounded over ~1 ms so tremolo chops don't click.
        edge_coeff_ = TimeCoeff(0.001f, sample_rate);
    }

    static const char* ShapeName(int s)
    {
        static const char* kNames[kNumShapes] = {"Sine", "Triangle", "Square", "Random"};
        return (s >= 0 && s < kNumShapes) ? kNames[s] : "?";
    }

    void SetFreq(float hz) { inc_ = hz / sample_rate_; }
    void SetShape(int s) { shape_ = (s >= 0 && s < kNumShapes) ? s : kSine; }

    /** Advance one sample. Writes bipolar (-1..1) values for phase and
     *  phase + offset (offset in cycles, 0..1). */
    void Process(float offset, float* a, float* b)
    {
        phase_ += inc_;
        bool wrapped = false;
        if(phase_ >= 1.f)
        {
            phase_ -= 1.f;
            wrapped = true;
        }
        float pb = phase_ + offset;
        if(pb >= 1.f)
            pb -= 1.f;

        switch(shape_)
        {
            case kSine:
                *a = FastSin01(phase_);
                *b = FastSin01(pb);
                break;
            case kTriangle:
                *a = Tri(phase_);
                *b = Tri(pb);
                break;
            case kSquare:
                *a = Edge(0, phase_ < 0.5f ? 1.f : -1.f);
                *b = Edge(1, pb < 0.5f ? 1.f : -1.f);
                break;
            default: // kRandom: smoothed sample-and-hold, new target per cycle
                if(wrapped)
                {
                    target_[0] = rng_.Bipolar();
                    target_[1] = rng_.Bipolar();
                }
                {
                    // Glide to the new value over roughly a quarter cycle.
                    const float k = Clamp(inc_ * 4.f, 0.f, 1.f);
                    smooth_[0] += (target_[0] - smooth_[0]) * k;
                    smooth_[1] += (target_[1] - smooth_[1]) * k;
                }
                *a = smooth_[0];
                *b = offset > 0.f ? smooth_[1] : smooth_[0];
                break;
        }
    }

  private:
    static float Tri(float p) { return p < 0.5f ? 4.f * p - 1.f : 3.f - 4.f * p; }

    float Edge(int c, float v)
    {
        smooth_[c] += (v - smooth_[c]) * edge_coeff_;
        return smooth_[c];
    }

    float sample_rate_ = 48000.f;
    float phase_       = 0.f;
    float inc_         = 0.f;
    int   shape_       = kSine;
    float smooth_[2]   = {0.f, 0.f};
    float target_[2]   = {0.f, 0.f};
    float edge_coeff_  = 0.1f;
    Rng   rng_;
};

} // namespace rodeo
