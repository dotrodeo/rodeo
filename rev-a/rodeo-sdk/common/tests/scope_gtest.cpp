// The scope: triggering, normalising, and staying inside its box.
//
// It lives on a strip twenty pixels tall while a parameter is on screen, so
// every one of those matters -- and the clamping is not cosmetic. The display
// takes unsigned coordinates, so an overshoot wraps and paints a band across
// the whole screen.
//
// Moved here with rodeo_scope.h on 2026-10-01: the module is the platform's, so
// its tests belong beside the platform's other host tests.

#include "gtest/gtest.h"

#include <cmath>

#include "rodeo_scope.h"

using namespace rodeo;

namespace
{
constexpr int kY0 = 20, kY1 = 37; // the strip, as the firmware lays it out

// Feed `seconds` of a sine at `amp`, at the audio rate.
void Feed(Scope& s, float hz, float amp, float seconds, float sr = 48000.f)
{
    const int n = static_cast<int>(sr * seconds);
    for(int i = 0; i < n; i++)
        s.Push(amp * sinf(2.f * static_cast<float>(M_PI) * hz * static_cast<float>(i) / sr));
}

// Let the gain settle: it closes on its target over a few Traces.
void Settle(Scope& s, int y0 = kY0, int y1 = kY1, int frames = 20)
{
    uint8_t y[Scope::kWidth];
    for(int i = 0; i < frames; i++)
        s.Trace(y0, y1, y);
}

struct Span
{
    int lo = 255, hi = 0;
};
Span Measure(Scope& s, int y0 = kY0, int y1 = kY1)
{
    uint8_t y[Scope::kWidth];
    s.Trace(y0, y1, y);
    Span sp;
    for(int i = 0; i < Scope::kWidth; i++)
    {
        if(y[i] < sp.lo)
            sp.lo = y[i];
        if(y[i] > sp.hi)
            sp.hi = y[i];
    }
    return sp;
}
} // namespace

TEST(Scope, ALoudSignalFillsTheStrip)
{
    Scope s;
    s.Init();
    Feed(s, 220.f, 0.9f, 0.5f);
    Settle(s);
    const Span sp = Measure(s);
    EXPECT_LE(sp.lo, kY0 + 2) << "does not reach the top of its box";
    EXPECT_GE(sp.hi, kY1 - 2) << "does not reach the bottom of its box";
}

// The point of normalising: a quiet patch has to be as readable as a loud one.
TEST(Scope, AQuietSignalFillsTheStripJustAsWell)
{
    Scope s;
    s.Init();
    Feed(s, 220.f, 0.05f, 0.5f); // 25 dB down
    Settle(s);
    const Span sp = Measure(s);
    EXPECT_LE(sp.lo, kY0 + 2);
    EXPECT_GE(sp.hi, kY1 - 2);
}

TEST(Scope, ItFillsWhateverRegionItIsGiven)
{
    Scope s;
    s.Init();
    Feed(s, 220.f, 0.5f, 0.5f);
    Settle(s, 20, 52); // the full space, as when nothing else is on screen
    const Span sp = Measure(s, 20, 52);
    EXPECT_LE(sp.lo, 22);
    EXPECT_GE(sp.hi, 50);
}

// Silence must not be amplified into a picture of its own noise.
TEST(Scope, SilenceStaysFlatInTheMiddle)
{
    Scope s;
    s.Init();
    Feed(s, 220.f, 0.0005f, 0.5f); // essentially nothing
    Settle(s);
    const Span sp = Measure(s);
    const int  cy = (kY0 + kY1) / 2;
    EXPECT_LE(sp.hi - sp.lo, 4) << "noise was blown up to fill the strip";
    EXPECT_NEAR((sp.lo + sp.hi) / 2, cy, 2);
}

// THE ONE THAT MATTERS. The gain trails the signal by a few frames, so a note
// attacking after a quiet passage is drawn with the gain that belonged to the
// quiet. Unclamped, those rows wrap and paint across the whole screen.
TEST(Scope, ATransientCannotEscapeItsBox)
{
    Scope s;
    s.Init();
    // Quiet, but above the floor where the gain stops chasing -- otherwise the
    // gain never winds up and the test proves nothing.
    Feed(s, 220.f, 0.06f, 0.5f);
    Settle(s);
    ASSERT_GT(s.Gain(), 1.f / 127.f * 4.f) << "the gain never wound up, so this proves nothing";

    Feed(s, 220.f, 1.0f, 0.05f); // full scale, suddenly
    uint8_t y[Scope::kWidth];
    for(int frame = 0; frame < 6; frame++) // through the whole convergence
    {
        s.Trace(kY0, kY1, y);
        for(int i = 0; i < Scope::kWidth; i++)
        {
            ASSERT_GE(y[i], kY0) << "drew above its box at frame " << frame;
            ASSERT_LE(y[i], kY1) << "drew below its box at frame " << frame;
        }
    }
}

// A held note has to stand still, or the strip is unreadable.
TEST(Scope, ASteadyNoteDoesNotCrawlSideways)
{
    Scope s;
    s.Init();
    Feed(s, 200.f, 0.8f, 0.5f);
    Settle(s);
    uint8_t a[Scope::kWidth], b[Scope::kWidth];
    s.Trace(kY0, kY1, a);
    Feed(s, 200.f, 0.8f, 0.05f); // more of the same signal
    s.Trace(kY0, kY1, b);
    int worst = 0;
    for(int i = 0; i < Scope::kWidth; i++)
        worst = std::max(worst, std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
    EXPECT_LE(worst, 3) << "the trace shifted between frames: triggering is not holding";
}

// The screen only redraws when this changes, so it has to notice a new signal
// and hold still for an unchanging one.
TEST(Scope, TheHashFollowsThePicture)
{
    Scope s;
    s.Init();
    Feed(s, 220.f, 0.8f, 0.5f);
    Settle(s);
    const uint32_t before = s.Hash();
    EXPECT_EQ(s.Hash(), before) << "a still picture would redraw for ever";
    Feed(s, 330.f, 0.4f, 0.1f);
    EXPECT_NE(s.Hash(), before) << "a changed signal would never be redrawn";
}
