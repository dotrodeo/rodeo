// rodeo-002 — does each effect DO WHAT IT IS CALLED?
//
// The existing fx tests ask whether each type is stable, transparent at zero and
// audible at full. That is necessary and it is not enough: a phaser that is
// really a low-pass passes all three, and so does a tremolo that never
// modulates. Derek, 2026-09-29: "I suspected some of them may not be working as
// expected."
//
// So these measure CHARACTER. Waveshapers must add harmonics; modulation types
// must modulate the thing their name says, at the rate they were given; the
// degradation types must degrade in their own particular way. Every number is
// printed as well as asserted, so the table itself is reviewable.

#include "gtest/gtest.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include "fx_character.h"
#include "fx_movement.h"
#include "fx_space.h"
#include "fx_texture.h"

using namespace rodeo002;
using namespace rodeo002::fx;

namespace
{
constexpr float  kSr    = 48000.f;
constexpr size_t kBlock = 48;
constexpr float  kF0    = 220.f;

/** Goertzel magnitude at one frequency over a whole buffer. */
double Mag(const std::vector<float>& x, double hz)
{
    const double w = 2.0 * M_PI * hz / kSr, c = 2.0 * cos(w);
    double       s1 = 0, s2 = 0;
    for(float v : x)
    {
        const double s = v + c * s1 - s2;
        s2             = s1;
        s1             = s;
    }
    return sqrt(fmax(0.0, s1 * s1 + s2 * s2 - c * s1 * s2));
}

double Rms(const std::vector<float>& x)
{
    double e = 0;
    for(float v : x)
        e += double(v) * v;
    return x.empty() ? 0.0 : sqrt(e / x.size());
}

double Dc(const std::vector<float>& x)
{
    double s = 0;
    for(float v : x)
        s += v;
    return x.empty() ? 0.0 : s / x.size();
}

bool Finite(const std::vector<float>& x)
{
    for(float v : x)
        if(!std::isfinite(v))
            return false;
    return true;
}

/** Envelope min/max over the window, from a fast follower. Tells amplitude
 *  modulation (tremolo, and the pumping in Squash) from anything else. */
void EnvRange(const std::vector<float>& x, double* lo, double* hi)
{
    double env = 0;
    *lo = 1e9;
    *hi = 0;
    // ~2 ms attack, ~20 ms release: fast enough for 12 Hz, slow enough to
    // ignore the 220 Hz carrier.
    const double a = 1.0 - exp(-1.0 / (0.002 * kSr));
    const double r = 1.0 - exp(-1.0 / (0.020 * kSr));
    for(size_t i = 0; i < x.size(); i++)
    {
        const double v = fabs(x[i]);
        env += (v - env) * (v > env ? a : r);
        if(i > kSr / 10) // skip the follower settling
        {
            *lo = fmin(*lo, env);
            *hi = fmax(*hi, env);
        }
    }
    if(*lo > *hi)
        *lo = *hi = 0;
}

/** Zero crossings per second of (x - mean): the modulation rate of an envelope,
 *  or twice the frequency of a carrier. */
double CrossRateOver(const std::vector<float>& x, double seconds)
{
    const double m = Dc(x);
    int          n = 0;
    for(size_t i = 1; i < x.size(); i++)
        if(((x[i - 1] - m) < 0.0) != ((x[i] - m) < 0.0))
            n++;
    return seconds > 0 ? double(n) / seconds : 0.0;
}

struct Result
{
    std::vector<float> l, r;
    double rms = 0, dc = 0, peak = 0;
    double f0 = 0, harm = 0;   // magnitude at the fundamental, and at 2f..6f
    /** The share of the energy that is NOT at a harmonic of the input: noise,
     *  aliasing, crackle. A waveshaper should leave this near zero however much
     *  distortion it adds; a sample-rate reducer should not. */
    double inharm = 0;
    double env_lo = 0, env_hi = 0;
    double lr = 0;             // RMS of (l - r), relative to the RMS of l
    bool   finite = true;

    /** Harmonic distortion: added harmonics against what is left at the pitch. */
    double thd() const { return f0 > 1e-9 ? harm / f0 : 0.0; }
    /** Amplitude modulation depth, 0 = steady, 1 = fully gated. */
    double am() const { return env_hi > 1e-9 ? (env_hi - env_lo) / env_hi : 0.0; }
};

/** Drives `fn` with a CONSTANT 1.0 and returns what comes out.
 *
 *  For an amplitude modulator that is the modulator itself, plotted -- which is
 *  the only honest way to measure its rate. Measuring the envelope of a tone
 *  instead means measuring the follower, and a follower fast enough for 12 Hz
 *  still ripples at the carrier. */
std::vector<float> Dc1(const std::function<void(float*, float*, size_t)>& fn,
                       float seconds)
{
    const size_t total = static_cast<size_t>(kSr * seconds);
    std::vector<float> out;
    std::vector<float> lb(kBlock), rb(kBlock);
    for(size_t i = 0; i < total; i += kBlock)
    {
        for(size_t j = 0; j < kBlock; j++)
            lb[j] = rb[j] = 1.f;
        fn(lb.data(), rb.data(), kBlock);
        for(size_t j = 0; j < kBlock; j++)
            out.push_back(lb[j]);
    }
    return out;
}

/** Runs `fn` over a 220 Hz sine and measures the last second of two. */
template <typename Fn>
Result Sine(Fn fn, float amp = 0.5f)
{
    const size_t total = static_cast<size_t>(kSr * 2.f);
    const size_t from  = static_cast<size_t>(kSr * 1.f);
    Result       out;
    std::vector<float> lb(kBlock), rb(kBlock);
    for(size_t i = 0; i < total; i += kBlock)
    {
        for(size_t j = 0; j < kBlock; j++)
        {
            const float s = amp * sinf(2.f * float(M_PI) * kF0 * float(i + j) / kSr);
            lb[j] = rb[j] = s;
        }
        fn(lb.data(), rb.data(), kBlock);
        if(i >= from)
            for(size_t j = 0; j < kBlock; j++)
            {
                out.l.push_back(lb[j]);
                out.r.push_back(rb[j]);
            }
    }
    out.finite = Finite(out.l) && Finite(out.r);
    if(!out.finite)
        return out;
    out.rms  = Rms(out.l);
    out.dc   = Dc(out.l);
    for(float v : out.l)
        out.peak = fmax(out.peak, fabs(v));
    out.f0 = Mag(out.l, kF0);
    double h = 0;
    for(int k = 2; k <= 6; k++)
        h = hypot(h, Mag(out.l, kF0 * k));
    out.harm = h;
    // Amplitude of a component from its Goertzel magnitude, then its power.
    // Everything the harmonics do not account for is inharmonic.
    const double n     = double(out.l.size());
    double       tonal = 0;
    for(int k = 1; k <= 12; k++)
    {
        const double a = 2.0 * Mag(out.l, kF0 * k) / n;
        tonal += a * a * 0.5;
    }
    const double power = out.rms * out.rms;
    out.inharm = power > 1e-12 ? fmax(0.0, 1.0 - tonal / power) : 0.0;
    EnvRange(out.l, &out.env_lo, &out.env_hi);
    std::vector<float> d(out.l.size());
    for(size_t i = 0; i < d.size(); i++)
        d[i] = out.l[i] - out.r[i];
    out.lr = out.rms > 1e-9 ? Rms(d) / out.rms : 0.0;
    return out;
}

void Row(const char* name, const Result& x)
{
    printf("  %-14s rms %.4f  thd %6.3f  inharm %.3f  am %.3f  lr %.3f  dc %+.4f  pk %.3f%s\n",
           name, x.rms, x.thd(), x.inharm, x.am(), x.lr, x.dc, x.peak,
           x.finite ? "" : "  NOT FINITE");
}
} // namespace

TEST(FxReview, Character)
{
    printf("\nCHARACTER -- a waveshaper must add harmonics (thd up)\n");
    printf("  dry: rms 0.3536, thd 0, inharm 0\n");
    for(int t = 0; t < Character::kNumTypes; t++)
    {
        Character fx;
        fx.Init(kSr);
        fx.SetType(t);
        fx.SetAmount(1.f);
        fx.SetBias(0.5f);
        Result x = Sine([&](float* l, float* r, size_t n) { fx.ProcessBlock(l, r, n); });
        Row(Character::TypeName(t), x);

        SCOPED_TRACE(Character::TypeName(t));
        EXPECT_TRUE(x.finite);
        EXPECT_GT(x.thd(), 0.05) << "this shaper adds no harmonics: it is not shaping";
        EXPECT_LT(fabs(x.dc), 0.02) << "a DC offset is reaching the output";
        EXPECT_LT(x.peak, 1.5) << "the shaper is running away";
        EXPECT_GT(x.rms, 0.05) << "the shaper swallowed the signal";
    }
}

TEST(FxReview, CharacterBiasAddsEvenHarmonics)
{
    printf("\nCHARACTER bias -- off centre should trade odd harmonics for even\n");
    for(int t = 0; t < Character::kNumTypes; t++)
    {
        double second[2];
        for(int b = 0; b < 2; b++)
        {
            Character fx;
            fx.Init(kSr);
            fx.SetType(t);
            fx.SetAmount(1.f);
            fx.SetBias(b == 0 ? 0.5f : 0.95f);
            Result x = Sine([&](float* l, float* r, size_t n) { fx.ProcessBlock(l, r, n); });
            second[b] = x.f0 > 1e-9 ? Mag(x.l, kF0 * 2.f) / x.f0 : 0.0;
        }
        printf("  %-12s 2nd harmonic  centred %.4f  biased %.4f\n",
               Character::TypeName(t), second[0], second[1]);
        SCOPED_TRACE(Character::TypeName(t));
        if(t == Character::kRectify)
        {
            // The documented exception, and the measurement confirms the
            // comment: centred, Rectify is already FULL-WAVE, which is nothing
            // but even harmonics. Its bias blends toward half-wave, so the
            // second harmonic goes DOWN. Asking for "more even harmonics" here
            // would be asking it to stop being a rectifier.
            EXPECT_GT(second[0], 0.2) << "centred Rectify is not full-wave";
            EXPECT_LT(second[1], second[0]) << "bias did not move toward half-wave";
        }
        else
            EXPECT_GT(second[1], second[0] * 1.5 + 0.005)
                << "bias does not bring in even harmonics";
    }
}

TEST(FxReview, Movement)
{
    printf("\nMOVEMENT -- each type must modulate the thing it is named after\n");
    for(int t = 0; t < Movement::kNumTypes; t++)
    {
        Movement fx;
        fx.Init(kSr);
        fx.SetType(t);
        fx.SetAmount(1.f);
        fx.SetRate(0.5f); // ~0.8 Hz of the 0.05-12 Hz range
        fx.SetOption(Lfo::kSine);
        Result x = Sine([&](float* l, float* r, size_t n) { fx.ProcessBlock(l, r, n); });
        Row(Movement::TypeName(t), x);
        SCOPED_TRACE(Movement::TypeName(t));
        EXPECT_TRUE(x.finite);
        EXPECT_GT(x.rms, 0.05) << "the signal did not survive";
        EXPECT_LT(fabs(x.dc), 0.02);
    }
}

TEST(FxReview, MovementRateIsTheRateYouAskedFor)
{
    printf("\nMOVEMENT rate -- tremolo fed a constant IS its own modulator\n");
    for(float knob : {0.f, 0.25f, 0.5f, 0.75f, 1.f})
    {
        Movement fx;
        fx.Init(kSr);
        fx.SetType(Movement::kTremolo);
        fx.SetAmount(1.f);
        fx.SetRate(knob);
        fx.SetOption(Lfo::kSine);
        // Long enough to see several cycles even at the slowest setting.
        const float seconds = knob < 0.3f ? 60.f : 10.f;
        std::vector<float> m
            = Dc1([&](float* l, float* r, size_t n) { fx.ProcessBlock(l, r, n); }, seconds);
        // Two mean-crossings per cycle, and the modulator is smooth, so this is
        // exact rather than an estimate.
        const double hz = CrossRateOver(m, seconds) / 2.0;
        const double expected = 0.05 * pow(12.0 / 0.05, double(knob));
        printf("  knob %.2f  expected %6.3f Hz  measured %6.3f Hz\n", knob, expected, hz);
        EXPECT_NEAR(hz, expected, expected * 0.1 + 0.02)
            << "the LFO is not running at the rate the knob asks for";
    }
}

TEST(FxReview, Texture)
{
    printf("\nTEXTURE -- degradation, each in its own way\n");
    for(int t = 0; t < Texture::kNumTypes; t++)
        for(int o = 0; o < Texture::OptionCount(t); o++)
        {
            Texture fx;
            fx.Init(kSr);
            fx.SetType(t);
            fx.SetOption(o);
            fx.SetAmount(1.f);
            fx.SetTone(1.f);
            Result x = Sine([&](float* l, float* r, size_t n) { fx.ProcessBlock(l, r, n); });
            char label[32];
            snprintf(label, sizeof label, "%s/%s", Texture::TypeName(t),
                     Texture::OptionName(t, o));
            Row(label, x);
            SCOPED_TRACE(label);
            EXPECT_TRUE(x.finite);
            EXPECT_GT(x.rms, 0.02) << "the signal did not survive";
            EXPECT_LT(x.peak, 2.0);
        }
}

TEST(FxReview, SquashCompresses)
{
    printf("\nTEXTURE Squash -- gain must fall as the input rises\n");
    for(int o = 0; o < Texture::OptionCount(Texture::kSquash); o++)
    {
        double g[2];
        for(int loud = 0; loud < 2; loud++)
        {
            Texture fx;
            fx.Init(kSr);
            fx.SetType(Texture::kSquash);
            fx.SetOption(o);
            fx.SetAmount(1.f);
            fx.SetTone(1.f);
            const float amp = loud ? 0.8f : 0.05f;
            Result      x   = Sine(
                [&](float* l, float* r, size_t n) { fx.ProcessBlock(l, r, n); }, amp);
            g[loud] = x.rms / (amp * 0.70710678);
        }
        printf("  %-8s gain at -26 dB %.3f   at -2 dB %.3f   ratio %.2f\n",
               Texture::OptionName(Texture::kSquash, o), g[0], g[1], g[0] / g[1]);
        SCOPED_TRACE(Texture::OptionName(Texture::kSquash, o));
        EXPECT_GT(g[0], g[1] * 1.3) << "loud and quiet get the same gain: not compressing";
    }
}

// ------------------------------------------------------------------ THE CHAIN
//
// A slot can be well behaved on its own and still put something into the rest
// of the chain that does not belong there. There is no DC blocker after the
// Texture slot -- the output stage is tilt, volume and the limiter -- so
// whatever DC a slot makes goes to the codec, and into Space's feedback if
// Space is running. That is the fault we already fixed once for Space itself
// (DC into a feedback loop is DC amplified, and the line out is AC-coupled, so
// a railed output is heard as silence rather than as distortion).

#include <memory>

#include "texture_chain.h"

namespace
{
struct ChainRig
{
    std::vector<float>                 gl, gr, pl, pr;
    std::unique_ptr<daisysp::ReverbSc> reverb;
    std::unique_ptr<TextureChain>      chain; // ~30 KB: heap, not stack

    ChainRig()
    : gl(Space::kGrainLen), gr(Space::kGrainLen), pl(Space::kPreLen),
      pr(Space::kPreLen), reverb(new daisysp::ReverbSc), chain(new TextureChain)
    {
        chain->Init(kSr, {gl.data(), gr.data(), pl.data(), pr.data(), reverb.get()});
    }

    /** One slot running flat out, everything else bypassed. */
    void Only(int slot, int type, float secondary)
    {
        for(int s = 0; s < TextureChain::kNumSlots; s++)
            chain->SetBypass(s, s != slot);
        chain->SetType(slot, type);
        chain->SetAmount(slot, 1.f);
        chain->SetSecondary(slot, secondary);
        chain->SnapToTargets();
    }
};
} // namespace

TEST(FxReview, NoSlotSendsDcDownTheChain)
{
    printf("\nCHAIN -- DC at the output, per Texture type (nothing blocks it)\n");
    for(int t = 0; t < Texture::kNumTypes; t++)
    {
        ChainRig rig;
        rig.Only(TextureChain::kTexture, t, 1.f);
        Result x = Sine(
            [&](float* l, float* r, size_t n) { rig.chain->ProcessBlock(l, r, n); });
        printf("  %-12s dc %+.4f  rms %.4f  peak %.3f\n",
               Texture::TypeName(t), x.dc, x.rms, x.peak);
        SCOPED_TRACE(Texture::TypeName(t));
        EXPECT_TRUE(x.finite);
        EXPECT_LT(fabs(x.dc), 0.01)
            << "this type sends DC to the output, and into Space's feedback";
    }
}

TEST(FxReview, NoCharacterTypeSendsDcDownTheChain)
{
    printf("\nCHAIN -- DC at the output, per Character type, bias hard over\n");
    for(int t = 0; t < Character::kNumTypes; t++)
    {
        ChainRig rig;
        rig.Only(TextureChain::kCharacter, t, 1.f); // bias fully over
        Result x = Sine(
            [&](float* l, float* r, size_t n) { rig.chain->ProcessBlock(l, r, n); });
        printf("  %-12s dc %+.4f  rms %.4f  peak %.3f\n",
               Character::TypeName(t), x.dc, x.rms, x.peak);
        SCOPED_TRACE(Character::TypeName(t));
        EXPECT_TRUE(x.finite);
        EXPECT_LT(fabs(x.dc), 0.01) << "this type sends DC to the output";
    }
}

// ------------------------------------------------------------------- SPACE
//
// Space is the best-covered slot already (stability at every corner, freeze,
// transparency, bypass, pitch quantise, the DC faults). What none of that asks
// is whether the two knobs you actually reach for do what they say: does Time
// change WHEN the repeat arrives, and does Feedback change HOW LONG the tail
// lasts.

namespace
{
/** Impulse in, then silence. Returns the output, so a repeat shows up as a
 *  second burst and a tail shows up as a decay. */
std::vector<float> Impulse(const std::function<void(float*, float*, size_t)>& fn,
                           float seconds)
{
    const size_t total = static_cast<size_t>(kSr * seconds);
    std::vector<float> out;
    std::vector<float> lb(kBlock), rb(kBlock);
    bool               fired = false;
    for(size_t i = 0; i < total; i += kBlock)
    {
        for(size_t j = 0; j < kBlock; j++)
            lb[j] = rb[j] = 0.f;
        if(!fired)
        {
            // A short burst rather than one sample: a grain engine needs
            // something to grab, and one sample is below every window.
            for(size_t j = 0; j < kBlock; j++)
                lb[j] = rb[j] = 0.8f * sinf(2.f * float(M_PI) * kF0 * float(j) / kSr);
            fired = true;
        }
        fn(lb.data(), rb.data(), kBlock);
        for(size_t j = 0; j < kBlock; j++)
            out.push_back(lb[j]);
    }
    return out;
}

/** Seconds until the signal stays below `frac` of its own peak for good. */
double TailSeconds(const std::vector<float>& x, double frac)
{
    double peak = 0;
    for(float v : x)
        peak = fmax(peak, fabs(v));
    if(peak < 1e-6)
        return 0.0;
    size_t last = 0;
    for(size_t i = 0; i < x.size(); i++)
        if(fabs(x[i]) > peak * frac)
            last = i;
    return double(last) / kSr;
}

/** When the loudest thing AFTER the dry burst arrives -- the first repeat. */
double FirstRepeatSeconds(const std::vector<float>& x)
{
    const size_t skip = kBlock * 4; // past the dry burst itself
    double       best = 0;
    size_t       at   = 0;
    for(size_t i = skip; i < x.size(); i++)
        if(fabs(x[i]) > best)
        {
            best = fabs(x[i]);
            at   = i;
        }
    return best > 1e-4 ? double(at) / kSr : 0.0;
}

void SpaceSetUp(ChainRig& rig, int type, float time, float feedback)
{
    for(int s = 0; s < TextureChain::kNumSlots; s++)
        rig.chain->SetBypass(s, s != TextureChain::kSpace);
    rig.chain->SetType(TextureChain::kSpace, type);
    rig.chain->SetAmount(TextureChain::kSpace, 1.f);
    rig.chain->space().SetTime(time);
    rig.chain->space().SetFeedback(feedback);
    rig.chain->space().SetGrain(0.5f);
    rig.chain->SnapToTargets();
}
} // namespace

TEST(FxReview, SpaceFeedbackLengthensTheTail)
{
    printf("\nSPACE -- feedback must lengthen the tail (seconds to -40 dB)\n");
    for(int t = 0; t < Space::kNumTypes; t++)
    {
        double tail[2];
        for(int hi = 0; hi < 2; hi++)
        {
            ChainRig rig;
            SpaceSetUp(rig, t, 0.5f, hi ? 0.9f : 0.05f);
            std::vector<float> y = Impulse(
                [&](float* l, float* r, size_t n) { rig.chain->ProcessBlock(l, r, n); }, 6.f);
            tail[hi] = TailSeconds(y, 0.01);
        }
        printf("  %-10s fb 0.05 -> %5.2f s    fb 0.90 -> %5.2f s\n",
               Space::TypeName(t), tail[0], tail[1]);
        SCOPED_TRACE(Space::TypeName(t));
        EXPECT_GT(tail[1], tail[0] * 1.2 + 0.05) << "feedback does not lengthen the tail";
    }
}

TEST(FxReview, SpaceTimeMovesTheRepeat)
{
    printf("\nSPACE -- time must move when the first repeat arrives\n");
    for(int t = 0; t < Space::kNumTypes; t++)
    {
        double at[2];
        for(int hi = 0; hi < 2; hi++)
        {
            ChainRig rig;
            SpaceSetUp(rig, t, hi ? 0.9f : 0.1f, 0.5f);
            std::vector<float> y = Impulse(
                [&](float* l, float* r, size_t n) { rig.chain->ProcessBlock(l, r, n); }, 6.f);
            at[hi] = FirstRepeatSeconds(y);
        }
        printf("  %-10s time 0.1 -> %5.3f s    time 0.9 -> %5.3f s\n",
               Space::TypeName(t), at[0], at[1]);
        SCOPED_TRACE(Space::TypeName(t));
        EXPECT_GT(at[1], at[0] + 0.02) << "the time knob does not move the repeat";
    }
}

// Crush's option is about the SWEEP, not about any one setting. Smooth walks
// continuously through the bit depths; Stepped lands on whole bits. At a
// whole-bit setting the two are necessarily identical -- Decimator's
// bit_overflow_ works out to exactly 2 there, and shifting by bits+1 after a 2x
// pre-scale is the same as shifting by bits. Measuring at full crush, as the
// first version of this test did, could only ever show them agreeing.
TEST(FxReview, CrushSmoothSweepsAndSteppedStaircases)
{
    printf("\nTEXTURE Crush -- distinct levels across a sweep of the amount knob\n");
    int distinct[2] = {0, 0};
    for(int o = 0; o < 2; o++)
    {
        std::vector<double> rms;
        for(int k = 0; k <= 28; k++)
        {
            Texture fx;
            fx.Init(kSr);
            fx.SetType(Texture::kCrush);
            fx.SetOption(o);
            fx.SetTone(1.f);
            fx.SetAmount(float(k) / 28.f);
            Result x = Sine([&](float* l, float* r, size_t n) { fx.ProcessBlock(l, r, n); });
            rms.push_back(x.rms);
        }
        for(size_t i = 1; i < rms.size(); i++)
            if(fabs(rms[i] - rms[i - 1]) > 1e-5)
                distinct[o]++;
        printf("  %-8s %2d of 28 steps of the knob changed the output\n",
               Texture::OptionName(Texture::kCrush, o), distinct[o]);
    }
    EXPECT_GT(distinct[0], distinct[1])
        << "Smooth should change on more of the sweep than Stepped: that IS the option";
    EXPECT_GT(distinct[1], 3) << "Stepped never changes at all";
}

// Dust is crackle ON TOP of the signal, not a noise bed replacing it, so it
// barely shows up in an energy measurement -- what it does is put transients
// ABOVE the peak of what you played. That is the thing to test.
TEST(FxReview, DustPutsClicksAboveTheSignal)
{
    printf("\nTEXTURE Dust -- peak against a 0.5 input (clicks ride above it)\n");
    for(int o = 0; o < Texture::OptionCount(Texture::kDust); o++)
    {
        Texture fx;
        fx.Init(kSr);
        fx.SetType(Texture::kDust);
        fx.SetOption(o);
        fx.SetAmount(1.f);
        fx.SetTone(1.f);
        Result x = Sine([&](float* l, float* r, size_t n) { fx.ProcessBlock(l, r, n); });
        printf("  %-8s peak %.3f  (dry peak 0.500)\n",
               Texture::OptionName(Texture::kDust, o), x.peak);
        SCOPED_TRACE(Texture::OptionName(Texture::kDust, o));
        // Radio band-limits as well, so it loses level; it still has to click.
        EXPECT_GT(x.peak, o == 0 ? 0.6 : 0.42) << "no crackle is reaching the output";
    }
}

// Squash's makeup gain does not keep up with the compression: a nominal signal
// comes out well down. Recorded rather than asserted tight, so that if it is
// ever retuned by ear the numbers here say what it used to be.
TEST(FxReview, SquashLosesLevelOnANominalSignal)
{
    printf("\nTEXTURE Squash -- output level for a 0.5 (-6 dB) input\n");
    for(int o = 0; o < Texture::OptionCount(Texture::kSquash); o++)
    {
        Texture fx;
        fx.Init(kSr);
        fx.SetType(Texture::kSquash);
        fx.SetOption(o);
        fx.SetAmount(1.f);
        fx.SetTone(1.f);
        Result x = Sine([&](float* l, float* r, size_t n) { fx.ProcessBlock(l, r, n); });
        const double db = 20.0 * log10(x.rms / 0.35355339);
        printf("  %-8s %+.1f dB against dry\n",
               Texture::OptionName(Texture::kSquash, o), db);
        SCOPED_TRACE(Texture::OptionName(Texture::kSquash, o));
        EXPECT_GT(db, -15.0) << "Squash has become a volume control";
    }
}
