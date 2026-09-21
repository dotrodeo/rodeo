// Stability and transparency guards for the rodeo-002 texture-chain effects.
//
// Every effect type is driven through every option and the corners of its
// amount/secondary range with a deliberately hostile signal -- a saw chord, a
// burst of full-scale noise, then silence -- and must:
//   * never produce NaN/inf (a NaN in a feedback path is permanent until reset),
//   * stay bounded (no runaway feedback),
//   * go quiet again after the input stops (no stuck DC, no self-oscillation
//     beyond what the type is supposed to hold),
//   * be transparent at amount 0, where the panel promises "off".
// The platform learned the first point the hard way: DaisySP's Svf diverges
// under drive (see rodeo-motherboard/firmware/common/tests/svf_stability_gtest).

#include "gtest/gtest.h"

#include <cmath>
#include <functional>
#include <vector>

#include "fx_character.h"

using namespace rodeo002::fx;

namespace
{
constexpr float  kSr    = 48000.f;
constexpr size_t kBlock = 48;

// Hostile but musical-ish input: 1 s of saw chord, 0.5 s of full-scale noise,
// then `silence_s` of silence. L and R differ slightly.
std::vector<float> MakeSignal(int ch, float silence_s)
{
    const size_t n_chord   = static_cast<size_t>(kSr * 1.0f);
    const size_t n_noise   = static_cast<size_t>(kSr * 0.5f);
    const size_t n_silence = static_cast<size_t>(kSr * silence_s);
    std::vector<float> s;
    s.reserve(n_chord + n_noise + n_silence);
    float ph[3] = {0, 0, 0};
    const float f[3] = {110.f, 165.f * (ch ? 1.003f : 1.f), 220.f};
    for(size_t i = 0; i < n_chord; i++)
    {
        float v = 0.f;
        for(int k = 0; k < 3; k++)
        {
            ph[k] += f[k] / kSr;
            if(ph[k] >= 1.f)
                ph[k] -= 1.f;
            v += (2.f * ph[k] - 1.f) * 0.2f;
        }
        s.push_back(v);
    }
    Rng rng;
    rng.Seed(ch ? 77u : 11u);
    for(size_t i = 0; i < n_noise; i++)
        s.push_back(rng.Bipolar());
    s.insert(s.end(), n_silence, 0.f);
    return s;
}

struct Result
{
    bool  finite     = true;
    float peak       = 0.f;
    float tail_peak  = 0.f; // peak over the final 50 ms
};

// Runs a stereo block processor over the signal.
Result RunFx(const std::function<void(float*, float*, size_t)>& process,
           float silence_s)
{
    std::vector<float> l = MakeSignal(0, silence_s);
    std::vector<float> r = MakeSignal(1, silence_s);
    Result             res;
    const size_t       tail_start = l.size() - static_cast<size_t>(kSr * 0.05f);
    for(size_t i = 0; i + kBlock <= l.size(); i += kBlock)
    {
        process(&l[i], &r[i], kBlock);
        for(size_t j = i; j < i + kBlock; j++)
        {
            for(float v : {l[j], r[j]})
            {
                if(!std::isfinite(v))
                    res.finite = false;
                const float a = fabsf(v);
                if(a > res.peak)
                    res.peak = a;
                if(j >= tail_start && a > res.tail_peak)
                    res.tail_peak = a;
            }
        }
    }
    return res;
}

// Max |out - in| once smoothers have settled (skips the first 100 ms).
float MaxDeviation(const std::function<void(float*, float*, size_t)>& process)
{
    std::vector<float> l = MakeSignal(0, 0.1f), r = MakeSignal(1, 0.1f);
    std::vector<float> l0 = l, r0 = r;
    float              dev  = 0.f;
    const size_t       skip = static_cast<size_t>(kSr * 0.1f);
    for(size_t i = 0; i + kBlock <= l.size(); i += kBlock)
    {
        process(&l[i], &r[i], kBlock);
        for(size_t j = i; j < i + kBlock; j++)
            if(j >= skip)
                dev = fmaxf(dev, fmaxf(fabsf(l[j] - l0[j]), fabsf(r[j] - r0[j])));
    }
    return dev;
}

const float kCorners[] = {0.f, 0.5f, 1.f};

} // namespace

// ---------------------------------------------------------------- CHARACTER

TEST(Character, StableAndQuietAfterInputStops)
{
    for(int type = 0; type < Character::kNumTypes; type++)
        for(int opt = 0; opt < Character::kNumOptions; opt++)
            for(float amount : kCorners)
                for(float bias : kCorners)
                {
                    Character fx;
                    fx.Init(kSr);
                    fx.SetType(type);
                    fx.SetOption(opt);
                    fx.SetAmount(amount);
                    fx.SetBias(bias);
                    const Result res = RunFx(
                        [&](float* l, float* r, size_t n) { fx.ProcessBlock(l, r, n); },
                        0.5f);
                    SCOPED_TRACE(testing::Message()
                                 << Character::TypeName(type) << " opt " << opt
                                 << " amount " << amount << " bias " << bias);
                    EXPECT_TRUE(res.finite);
                    EXPECT_LT(res.peak, 2.f);
                    // Memoryless shaper + DC blocker: silence in, silence out.
                    EXPECT_LT(res.tail_peak, 1e-3f);
                }
}

TEST(Character, TransparentAtZeroAmount)
{
    for(int type = 0; type < Character::kNumTypes; type++)
    {
        Character fx;
        fx.Init(kSr);
        fx.SetType(type);
        fx.SetOption(Character::kCut300);
        fx.SetBias(1.f);
        fx.SetAmount(0.f);
        SCOPED_TRACE(Character::TypeName(type));
        EXPECT_LT(MaxDeviation([&](float* l, float* r, size_t n) {
                      fx.ProcessBlock(l, r, n);
                  }),
                  1e-5f);
    }
}
