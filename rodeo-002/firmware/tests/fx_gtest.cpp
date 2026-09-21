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
#include "fx_movement.h"

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

// Sweeps every type x option x amount/secondary corner of a slot. `Fx` needs
// Init/SetType/SetOption/SetAmount/ProcessBlock and static TypeName;
// `set_secondary` applies the slot's secondary (Bias, Rate, ...).
template <typename Fx, typename SetSecondary>
void SweepSlot(int n_types,
               int n_options,
               SetSecondary set_secondary,
               float peak_limit,
               float tail_limit)
{
    for(int type = 0; type < n_types; type++)
        for(int opt = 0; opt < n_options; opt++)
            for(float amount : kCorners)
                for(float sec : kCorners)
                {
                    Fx fx;
                    fx.Init(kSr);
                    fx.SetType(type);
                    fx.SetOption(opt);
                    fx.SetAmount(amount);
                    set_secondary(fx, sec);
                    const Result res = RunFx(
                        [&](float* l, float* r, size_t n) { fx.ProcessBlock(l, r, n); },
                        0.5f);
                    SCOPED_TRACE(testing::Message()
                                 << Fx::TypeName(type) << " opt " << opt << " amount "
                                 << amount << " secondary " << sec);
                    EXPECT_TRUE(res.finite);
                    EXPECT_LT(res.peak, peak_limit);
                    EXPECT_LT(res.tail_peak, tail_limit);
                }
}

template <typename Fx, typename SetSecondary>
void CheckTransparentAtZero(int n_types, int option, SetSecondary set_secondary)
{
    for(int type = 0; type < n_types; type++)
    {
        Fx fx;
        fx.Init(kSr);
        fx.SetType(type);
        fx.SetOption(option);
        set_secondary(fx, 1.f);
        fx.SetAmount(0.f);
        SCOPED_TRACE(Fx::TypeName(type));
        EXPECT_LT(MaxDeviation([&](float* l, float* r, size_t n) {
                      fx.ProcessBlock(l, r, n);
                  }),
                  1e-5f);
    }
}

// The converse of transparency: at full amount every type must audibly change
// the signal -- a stability test alone would pass an effect that did nothing.
template <typename Fx, typename SetSecondary>
void CheckAudibleAtFull(int n_types, int option, float secondary, SetSecondary set_secondary)
{
    for(int type = 0; type < n_types; type++)
    {
        Fx fx;
        fx.Init(kSr);
        fx.SetType(type);
        fx.SetOption(option);
        set_secondary(fx, secondary);
        fx.SetAmount(1.f);
        SCOPED_TRACE(Fx::TypeName(type));
        EXPECT_GT(MaxDeviation([&](float* l, float* r, size_t n) {
                      fx.ProcessBlock(l, r, n);
                  }),
                  0.05f);
    }
}

// ---------------------------------------------------------------- CHARACTER

auto kSetBias = [](Character& fx, float v) { fx.SetBias(v); };

// Memoryless shaper + DC blocker: silence in, silence out.
TEST(Character, StableAndQuietAfterInputStops)
{
    SweepSlot<Character>(Character::kNumTypes, Character::kNumOptions, kSetBias, 2.f, 1e-3f);
}

TEST(Character, AudibleAtFullAmount)
{
    CheckAudibleAtFull<Character>(Character::kNumTypes, Character::kCutOff, 0.5f, kSetBias);
}

TEST(Character, TransparentAtZeroAmount)
{
    CheckTransparentAtZero<Character>(Character::kNumTypes, Character::kCut300, kSetBias);
}

// ---------------------------------------------------------------- MOVEMENT

auto kSetRate = [](Movement& fx, float v) { fx.SetRate(v); };

// Flanger feedback and the phaser's feedback loop must both die away.
TEST(Movement, StableAndQuietAfterInputStops)
{
    SweepSlot<Movement>(Movement::kNumTypes, Movement::kNumOptions, kSetRate, 2.f, 1e-3f);
}

TEST(Movement, AudibleAtFullAmount)
{
    CheckAudibleAtFull<Movement>(Movement::kNumTypes, Lfo::kSine, 0.7f, kSetRate);
}

TEST(Movement, TransparentAtZeroAmount)
{
    CheckTransparentAtZero<Movement>(Movement::kNumTypes, Lfo::kRandom, kSetRate);
}
