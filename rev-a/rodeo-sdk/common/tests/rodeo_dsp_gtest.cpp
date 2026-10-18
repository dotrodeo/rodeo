// Tests for the DSP building blocks in common/rodeo_dsp.h.
//
// Written when the header moved up from rodeo-002 on 2026-10-01, and aimed at
// the three mistakes that header has actually caused on hardware rather than at
// coverage for its own sake:
//
//   * An Lfo initialised at the AUDIO rate and ticked once per block ran 48x
//     slow -- "20 Hz" measured 0.42 Hz. Init() takes the rate it will be
//     ticked at, and the test asserts that for both rates.
//   * DC into a feedback loop is DC amplified, and the line out is AC-coupled,
//     so a railed output is heard as silence rather than as distortion.
//   * A Smoother that never settles leaves a parameter permanently short of
//     where the panel says it is.

#include "gtest/gtest.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include "rodeo_dsp.h"

using namespace rodeo;

namespace
{
/** Mean-crossings per second: twice the frequency of anything smooth. */
double CrossRate(const std::vector<float>& x, double seconds)
{
    double mean = 0;
    for(float v : x)
        mean += v;
    mean /= double(x.size());
    int n = 0;
    for(size_t i = 1; i < x.size(); i++)
        if(((x[i - 1] - mean) < 0.0) != ((x[i] - mean) < 0.0))
            n++;
    return double(n) / seconds;
}
} // namespace

// THE ONE THAT COST A SESSION. Init() takes the rate the LFO will be TICKED at,
// not the audio rate -- a block-rate caller that passes the sample rate gets an
// LFO running at 1/48th of its dial.
TEST(RodeoDsp, LfoRunsAtTheRateItIsTickedAt)
{
    for(float rate : {48000.f, 1000.f})
        for(float hz : {0.5f, 5.f})
        {
            Lfo lfo;
            lfo.Init(rate);
            lfo.SetFreq(hz);
            lfo.SetShape(Lfo::kSine);

            const double       seconds = 8.0;
            const size_t       n       = static_cast<size_t>(rate * seconds);
            std::vector<float> out(n);
            for(size_t i = 0; i < n; i++)
            {
                float a, b;
                lfo.Process(0.f, &a, &b);
                out[i] = a;
            }
            SCOPED_TRACE(testing::Message() << "tick rate " << rate << ", " << hz << " Hz");
            // Counting crossings resolves to +/-1 over the window, which at
            // 0.5 Hz across 8 seconds is +/-0.0625 Hz -- wider than 5%. The
            // tolerance has to carry that or the test fails on its own
            // arithmetic rather than on the LFO.
            EXPECT_NEAR(CrossRate(out, seconds) / 2.0, hz,
                        hz * 0.05 + 1.0 / (2.0 * seconds));
        }
}

// The second output is a fraction of a cycle behind the first, which is what
// makes stereo width and auto-pan work.
TEST(RodeoDsp, LfoSecondPhaseIsOffsetFromTheFirst)
{
    Lfo lfo;
    lfo.Init(1000.f);
    lfo.SetFreq(2.f);
    lfo.SetShape(Lfo::kSine);
    double diff = 0;
    for(int i = 0; i < 4000; i++)
    {
        float a, b;
        lfo.Process(0.5f, &a, &b); // half a cycle apart
        diff += double(a + b) * (a + b);
    }
    EXPECT_LT(diff / 4000.0, 0.02) << "half a cycle apart should very nearly cancel";

    Lfo same;
    same.Init(1000.f);
    same.SetFreq(2.f);
    same.SetShape(Lfo::kSine);
    double together = 0;
    for(int i = 0; i < 4000; i++)
    {
        float a, b;
        same.Process(0.f, &a, &b); // no offset: identical
        together += fabs(double(a) - b);
    }
    EXPECT_LT(together / 4000.0, 1e-6) << "no offset should mean no difference";
}

TEST(RodeoDsp, EveryLfoShapeStaysInRangeAndMoves)
{
    for(int shape = 0; shape < Lfo::kNumShapes; shape++)
    {
        Lfo lfo;
        lfo.Init(1000.f);
        lfo.SetFreq(3.f);
        lfo.SetShape(shape);
        float lo = 2.f, hi = -2.f;
        for(int i = 0; i < 4000; i++)
        {
            float a, b;
            lfo.Process(0.25f, &a, &b);
            ASSERT_TRUE(std::isfinite(a) && std::isfinite(b)) << Lfo::ShapeName(shape);
            lo = fminf(lo, a);
            hi = fmaxf(hi, a);
        }
        SCOPED_TRACE(Lfo::ShapeName(shape));
        EXPECT_GE(lo, -1.001f);
        EXPECT_LE(hi, 1.001f);
        EXPECT_GT(hi - lo, 0.5f) << "this shape barely moves";
    }
}

// DC is what rails a feedback loop, and an AC-coupled output turns a railed
// signal into silence rather than into distortion -- so a blocker that does not
// block is a fault you hear as nothing at all.
TEST(RodeoDsp, DcBlockerRemovesOffsetAndKeepsSignal)
{
    DcBlocker dc;
    double    sum = 0, energy = 0;
    const int n   = 48000;
    for(int i = 0; i < n; i++)
    {
        const float x = 0.5f + 0.3f * sinf(2.f * kPi * 220.f * float(i) / 48000.f);
        const float y = dc.Process(x);
        if(i > 4800) // past the blocker's own settling
        {
            sum += y;
            energy += double(y) * y;
        }
    }
    const double count = n - 4800;
    EXPECT_LT(fabs(sum / count), 1e-3) << "the offset survived";
    // 0.3 amplitude -> 0.045 mean power. The blocker is ~7.6 Hz, so a 220 Hz
    // tone should come through essentially untouched.
    EXPECT_NEAR(energy / count, 0.045, 0.005) << "the signal did not";
}

TEST(RodeoDsp, SmootherReachesItsTargetAndStartsWhereItIsTold)
{
    Smoother s;
    s.Init(1000.f, 0.01f, 0.25f); // 10 ms at a 1 kHz tick
    EXPECT_FLOAT_EQ(s.Next(), 0.25f) << "it did not start at the value given";

    s.Set(0.75f);
    for(int i = 0; i < 1000; i++) // a full second: a hundred time constants
        s.Next();
    EXPECT_NEAR(s.Next(), 0.75f, 1e-4) << "it never arrives";
    EXPECT_TRUE(s.Settled());

    s.Snap(0.1f);
    EXPECT_FLOAT_EQ(s.Next(), 0.1f) << "Snap should not need time";
}

TEST(RodeoDsp, OnePoleSplitsLowFromHigh)
{
    // TWO FILTERS, each fed its own continuous tone. The first version of this
    // copied one filter per sample and pushed a single sample through the copy,
    // which measures nothing: a one-pole IS its history, so a copy with no
    // history just scales its input.
    auto through = [](float hz, bool high) {
        OnePole f;
        f.SetFreq(1000.f, 48000.f);
        double e = 0;
        for(int i = 0; i < 48000; i++)
        {
            const float x = sinf(2.f * kPi * hz * float(i) / 48000.f);
            const float y = high ? (x - f.Lp(x)) : f.Lp(x);
            if(i > 4800) // past the filter settling
                e += double(y) * y;
        }
        return e;
    };

    // At a 1 kHz corner: 100 Hz passes the low-pass and is rejected by the
    // high-pass, and 10 kHz does the opposite.
    EXPECT_GT(through(100.f, false), through(10000.f, false) * 10.0)
        << "the low-pass passed 10 kHz as readily as 100 Hz";
    EXPECT_GT(through(10000.f, true), through(100.f, true) * 10.0)
        << "the high-pass passed 100 Hz as readily as 10 kHz";
}

// ExpMap is how every time and frequency control on a Rodeo panel is shaped, so
// its ends have to be exact: a knob that does not reach its stated minimum is a
// knob that cannot turn an effect off.
TEST(RodeoDsp, ExpMapHitsItsEndsAndIsMonotonic)
{
    EXPECT_FLOAT_EQ(ExpMap(0.f, 0.005f, 8.f), 0.005f);
    EXPECT_FLOAT_EQ(ExpMap(1.f, 0.005f, 8.f), 8.f);
    float prev = -1.f;
    for(int i = 0; i <= 100; i++)
    {
        const float v = ExpMap(float(i) / 100.f, 0.005f, 8.f);
        EXPECT_GT(v, prev);
        prev = v;
    }
    // Exponential, not linear: the midpoint is the geometric mean.
    EXPECT_NEAR(ExpMap(0.5f, 1.f, 100.f), 10.f, 0.01f);
}

TEST(RodeoDsp, TimeCoeffGivesTheTimeConstantItIsAsked)
{
    const float sr = 48000.f;
    for(float seconds : {0.001f, 0.02f, 0.5f})
    {
        // After one time constant a one-pole should have covered ~63% of the way.
        float       y = 0.f;
        const float k = TimeCoeff(seconds, sr);
        const int   n = static_cast<int>(seconds * sr);
        for(int i = 0; i < n; i++)
            y += (1.f - y) * k;
        SCOPED_TRACE(testing::Message() << seconds << " s");
        EXPECT_NEAR(y, 0.632f, 0.02f);
    }
}

// FastTanh is an APPROXIMATION and this records how close it is, rather than
// pretending it is exact. Worst case measured 2.3% near |x| = 1.75, which for a
// waveshaper is part of its character and not an error -- but if the
// approximation is ever swapped, this says by how much the sound moved.
TEST(RodeoDsp, FastTanhTracksTheRealOneAndSaturates)
{
    float worst = 0.f, worst_at = 0.f;
    for(float x = -6.f; x <= 6.f; x += 0.01f)
    {
        const float e = fabsf(FastTanh(x) - tanhf(x));
        if(e > worst)
        {
            worst    = e;
            worst_at = x;
        }
    }
    printf("      [ note ] FastTanh worst error %.4f at x = %.2f\n", worst, worst_at);
    EXPECT_LT(worst, 0.03f) << "the approximation has drifted from tanh";

    EXPECT_LT(fabsf(FastTanh(100.f)), 1.001f) << "it has to stay bounded";
    EXPECT_GT(FastTanh(100.f), 0.9f);
    EXPECT_LT(FastTanh(-100.f), -0.9f);
    // Odd symmetry: a waveshaper that is not symmetric adds even harmonics
    // nobody asked for.
    for(float x = 0.f; x <= 6.f; x += 0.1f)
        EXPECT_NEAR(FastTanh(x), -FastTanh(-x), 1e-6f) << "asymmetric at x = " << x;
}

TEST(RodeoDsp, RngIsBoundedAndNotStuck)
{
    Rng r;
    r.Seed(0xC0FFEEu);
    float lo = 2.f, hi = -2.f;
    double sum = 0;
    for(int i = 0; i < 20000; i++)
    {
        const float v = r.Bipolar();
        ASSERT_GE(v, -1.f);
        ASSERT_LE(v, 1.f);
        lo = fminf(lo, v);
        hi = fmaxf(hi, v);
        sum += v;
    }
    EXPECT_LT(lo, -0.9f);
    EXPECT_GT(hi, 0.9f);
    EXPECT_LT(fabs(sum / 20000.0), 0.05) << "a bipolar source should be centred";
}
