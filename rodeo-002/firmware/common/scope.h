// rodeo-002 — the output as a picture: a triggered, normalised scope.
//
// Written from the audio callback and read from the main loop. One writer, one
// reader, and a torn sample is a pixel, so there is no locking: Trace() takes a
// copy and works from that.
//
// Three things make it readable on a strip twenty pixels tall:
//
//   * TRIGGERING on a rising zero crossing, so a held note stands still instead
//     of crawling sideways.
//   * NORMALISING to the region it is given, so a quiet patch reads as well as
//     a loud one -- with the gain following over a few frames rather than
//     snapping, so a decaying note does not pump, and a floor below which it
//     stops chasing rather than turning silence into a picture of its own noise.
//   * CLAMPING to that region. The gain trails the signal, so the attack of a
//     note arrives while the gain still belongs to what came before it; the
//     display's coordinates are unsigned, so an unclamped overshoot would wrap
//     and paint a band across the whole screen.

#pragma once

#include <cstdint>

namespace rodeo002
{
class Scope
{
  public:
    /** Samples kept: 128 are drawn, the rest give the trigger somewhere to look. */
    static constexpr int kLen   = 192;
    static constexpr int kWidth = 128;
    /** One sample kept per this many: 6 kHz, so the width is ~32 ms. */
    static constexpr int kDecimate = 8;
    /** Peak below which the gain stops chasing, of 127: about -30 dB. */
    static constexpr int kFloor = 4;
    /** How far the gain closes on its target each Trace. */
    static constexpr float kGainRate = 0.4f;

    void Init()
    {
        for(int i = 0; i < kLen; i++)
            buf_[i] = 0;
        write_ = 0;
        phase_ = 0;
        gain_  = 1.f / 127.f;
    }

    /** Every output sample; the decimation is done here. Audio-callback rate. */
    void Push(float x)
    {
        if(++phase_ < kDecimate)
            return;
        phase_   = 0;
        float v  = x * 127.f;
        v        = v > 127.f ? 127.f : (v < -127.f ? -127.f : v);
        buf_[write_] = static_cast<int8_t>(v);
        write_       = static_cast<uint16_t>((write_ + 1) % kLen);
    }

    /** Fill `y` with kWidth screen rows spanning y0..y1 inclusive. */
    void Trace(int y0, int y1, uint8_t* y)
    {
        int8_t         copy[kLen];
        const uint16_t w = write_;
        for(int i = 0; i < kLen; i++)
            copy[i] = buf_[(w + i) % kLen]; // oldest first

        int start = 0;
        for(int i = 1; i < kLen - kWidth; i++)
            if(copy[i - 1] <= 0 && copy[i] > 0)
            {
                start = i;
                break;
            }

        int peak = 0;
        for(int i = 0; i < kWidth; i++)
        {
            const int a = copy[start + i] < 0 ? -copy[start + i] : copy[start + i];
            if(a > peak)
                peak = a;
        }
        const float target = 1.f / static_cast<float>(peak > kFloor ? peak : 127);
        gain_ += (target - gain_) * kGainRate;

        const int cy  = (y0 + y1) / 2;
        const int amp = (y1 - y0) / 2;
        for(int i = 0; i < kWidth; i++)
        {
            int v = cy
                    - static_cast<int>(static_cast<float>(copy[start + i]) * gain_
                                       * static_cast<float>(amp));
            if(v < y0)
                v = y0;
            if(v > y1)
                v = y1;
            y[i] = static_cast<uint8_t>(v);
        }
    }

    /** What the picture would look like, cheaply: a still trace costs the bus
     *  nothing because the screen can tell it has not changed. The gain is in
     *  here too, since it is still converging when the samples repeat. */
    uint32_t Hash() const
    {
        uint32_t h = 2166136261u;
        for(int i = 0; i < kLen; i += 3)
            h = (h ^ static_cast<uint8_t>(buf_[i])) * 16777619u;
        return (h ^ static_cast<uint32_t>(gain_ * 512.f)) * 16777619u;
    }

    float Gain() const { return gain_; }

  private:
    volatile int8_t   buf_[kLen] = {};
    volatile uint16_t write_     = 0;
    int               phase_     = 0;
    float             gain_      = 1.f / 127.f;
};

} // namespace rodeo002
