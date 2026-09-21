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
#include "fx_texture.h"
#include "fx_space.h"

#include <memory>

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
               float tail_limit,
               float silence_s = 0.5f)
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
                        silence_s);
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

// ---------------------------------------------------------------- TEXTURE

auto kSetTone = [](Texture& fx, float v) { fx.SetTone(v); };

// Tape hiss and Dust crackle follow the input, so give them 3 s of silence to
// fade out; after that everything must be quiet. Squash's make-up gain is the
// peak risk, bounded by its soft clip.
TEST(Texture, StableAndQuietAfterInputStops)
{
    SweepSlot<Texture>(Texture::kNumTypes, Texture::kNumOptions, kSetTone, 2.f, 1e-3f, 3.f);
}

TEST(Texture, AudibleAtFullAmount)
{
    // Tone fully open, so any change is the effect's, not the filter's.
    CheckAudibleAtFull<Texture>(Texture::kNumTypes, 0, 1.f, kSetTone);
}

TEST(Texture, TransparentAtZeroAmount)
{
    CheckTransparentAtZero<Texture>(Texture::kNumTypes, 0, kSetTone);
}

TEST(Texture, OptionsClampToEachTypesRange)
{
    for(int type = 0; type < Texture::kNumTypes; type++)
        for(int opt = -1; opt <= Texture::kNumOptions; opt++)
        {
            Texture fx;
            fx.Init(kSr);
            fx.SetType(type);
            fx.SetOption(opt);
            EXPECT_GE(fx.GetOption(), 0);
            EXPECT_LT(fx.GetOption(), Texture::OptionCount(type));
            // Switching type re-clamps: Squash's "Pump" (2) is not valid for
            // Downsample, which has no options.
            fx.SetOption(2);
            fx.SetType(Texture::kDownsample);
            EXPECT_EQ(fx.GetOption(), 0);
        }
}

// ---------------------------------------------------------------- SPACE

namespace
{
// Space borrows its big buffers, as it does from SDRAM on the Seed. ReverbSc
// alone is ~396 KB, so it goes on the heap, not the stack.
struct SpaceRig
{
    std::vector<float>                 gl, gr, pl, pr;
    std::unique_ptr<daisysp::ReverbSc> reverb;
    Space                              fx;

    SpaceRig()
    : gl(Space::kGrainLen), gr(Space::kGrainLen), pl(Space::kPreLen),
      pr(Space::kPreLen), reverb(new daisysp::ReverbSc)
    {
        fx.Init(kSr, {gl.data(), gr.data(), pl.data(), pr.data(), reverb.get()});
    }
    Result Run(float silence_s)
    {
        return RunFx([&](float* l, float* r, size_t n) { fx.ProcessBlock(l, r, n); },
                     silence_s);
    }
};
} // namespace

// The corners that stress feedback: max feedback, both grain extremes, pitch
// down/unison/up, bright and dark. With feedback at max the tail is SUPPOSED
// to ring for a long time, so only boundedness is checked there; at moderate
// feedback it must also die away.
TEST(Space, StableAtEveryCorner)
{
    for(int type = 0; type < Space::kNumTypes; type++)
        for(float fb : {0.3f, 1.f})
            for(float grain : {0.f, 1.f})
                for(float pitch : {0.f, 0.5f, 1.f})
                    for(float damp : {0.f, 1.f})
                    {
                        SpaceRig rig;
                        rig.fx.SetType(type);
                        rig.fx.SetOption(Space::kSemitones);
                        rig.fx.SetMix(1.f);
                        rig.fx.SetTime(0.3f);
                        rig.fx.SetFeedback(fb);
                        rig.fx.SetGrain(grain);
                        rig.fx.SetPitch(pitch);
                        rig.fx.SetSpread(1.f);
                        rig.fx.SetDamp(damp);
                        const Result res = rig.Run(fb < 0.5f ? 8.f : 4.f);
                        SCOPED_TRACE(testing::Message()
                                     << Space::TypeName(type) << " fb " << fb << " grain "
                                     << grain << " pitch " << pitch << " damp " << damp);
                        EXPECT_TRUE(res.finite);
                        EXPECT_LT(res.peak, 3.f);
                        if(fb < 0.5f)
                            EXPECT_LT(res.tail_peak, 1e-2f);
                    }
}

TEST(Space, FreezeHoldsWithoutRunningAway)
{
    for(int type = 0; type < Space::kNumTypes; type++)
    {
        SpaceRig rig;
        rig.fx.SetType(type);
        rig.fx.SetMix(1.f);
        rig.fx.SetFeedback(1.f);
        rig.fx.SetGrain(1.f);
        rig.fx.SetPitch(1.f); // shimmer up two octaves, the worst case for Reverb
        // Freeze 1.2 s in (during the noise burst), once there is enough
        // history: a reversed grain at +24 st reads 2 s back. The held material
        // must keep sounding through 4 s of silence, and stay bounded.
        size_t     blocks = 0;
        const auto res    = RunFx(
            [&](float* l, float* r, size_t n) {
                if(++blocks == 1200) // 1.2 s in
                    rig.fx.SetFreeze(true);
                rig.fx.ProcessBlock(l, r, n);
            },
            4.f);
        SCOPED_TRACE(Space::TypeName(type));
        EXPECT_TRUE(res.finite);
        EXPECT_LT(res.peak, 3.f);
        EXPECT_GT(res.tail_peak, 1e-3f); // still holding sound
    }
}

TEST(Space, TransparentAtZeroMix)
{
    for(int type = 0; type < Space::kNumTypes; type++)
    {
        SpaceRig rig;
        rig.fx.SetType(type);
        rig.fx.SetFeedback(1.f);
        rig.fx.SetMix(0.f);
        SCOPED_TRACE(Space::TypeName(type));
        EXPECT_LT(MaxDeviation([&](float* l, float* r, size_t n) {
                      rig.fx.ProcessBlock(l, r, n);
                  }),
                  1e-5f);
    }
}

TEST(Space, AudibleAtFullMix)
{
    for(int type = 0; type < Space::kNumTypes; type++)
    {
        SpaceRig rig;
        rig.fx.SetType(type);
        rig.fx.SetMix(1.f);
        SCOPED_TRACE(Space::TypeName(type));
        EXPECT_GT(MaxDeviation([&](float* l, float* r, size_t n) {
                      rig.fx.ProcessBlock(l, r, n);
                  }),
                  0.05f);
    }
}

// Bypassed from the start, the effect is never fed, so there is no tail and
// the output must be the input exactly -- even at full mix.
TEST(Space, BypassPassesDryAtUnity)
{
    for(int type = 0; type < Space::kNumTypes; type++)
    {
        SpaceRig rig;
        rig.fx.SetType(type);
        rig.fx.SetMix(1.f);
        rig.fx.SetBypass(true);
        rig.fx.ProcessBlock(nullptr, nullptr, 0);
        // Let the dry/input smoothers settle before comparing.
        std::vector<float> z(static_cast<size_t>(kSr * 0.2f), 0.f), z2 = z;
        rig.fx.ProcessBlock(z.data(), z2.data(), z.size());
        SCOPED_TRACE(Space::TypeName(type));
        EXPECT_LT(MaxDeviation([&](float* l, float* r, size_t n) {
                      rig.fx.ProcessBlock(l, r, n);
                  }),
                  1e-4f);
    }
}

TEST(Space, PitchQuantizeLandsOnItsIntervals)
{
    SpaceRig rig;
    for(float p = 0.f; p <= 1.f; p += 0.01f)
    {
        rig.fx.SetOption(Space::kOctaves);
        rig.fx.SetPitch(p);
        EXPECT_EQ(fmodf(rig.fx.GetSemitones(), 12.f), 0.f) << p;

        rig.fx.SetOption(Space::kFifths);
        rig.fx.SetPitch(p);
        const float st = fabsf(rig.fx.GetSemitones());
        EXPECT_TRUE(st == 0 || st == 7 || st == 12 || st == 19 || st == 24) << p;

        rig.fx.SetOption(Space::kSemitones);
        rig.fx.SetPitch(p);
        EXPECT_EQ(rig.fx.GetSemitones(), roundf(rig.fx.GetSemitones())) << p;
    }
    rig.fx.SetOption(Space::kFree);
    rig.fx.SetPitch(0.505f); // a hair off centre still means unison
    EXPECT_EQ(rig.fx.GetSemitones(), 0.f);
}
