// Tests for common/pot_ui.h — the pure panel-behaviour logic that
// 09_poly_synth_oled uses to drive the OLED (stepped waveform selector +
// "which knob is the user touching" popup).

#include "gtest/gtest.h"
#include "pot_ui.h"

using rodeo::KnobWatch;
using rodeo::StepWithHysteresis;

// --- StepWithHysteresis -----------------------------------------------------

TEST(StepHysteresis, UnseededTakesTheReading)
{
    // current outside 0..steps-1 means "no opinion yet".
    EXPECT_EQ(StepWithHysteresis(0.00f, 4, -1), 0);
    EXPECT_EQ(StepWithHysteresis(0.30f, 4, -1), 1);
    EXPECT_EQ(StepWithHysteresis(0.60f, 4, -1), 2);
    EXPECT_EQ(StepWithHysteresis(0.90f, 4, -1), 3);
}

TEST(StepHysteresis, FullScaleStaysInRange)
{
    // norm == 1.0 divides to exactly `steps` — must clamp, not run off the end.
    EXPECT_EQ(StepWithHysteresis(1.0f, 4, -1), 3);
    EXPECT_EQ(StepWithHysteresis(1.5f, 4, -1), 3); // over-range guard
    EXPECT_EQ(StepWithHysteresis(-0.1f, 4, -1), 0);
}

TEST(StepHysteresis, MustClearTheGuardBandToChange)
{
    // Zone 0 is [0, 0.25). Just past the edge is not enough.
    EXPECT_EQ(StepWithHysteresis(0.26f, 4, 0, 0.03f), 0);
    EXPECT_EQ(StepWithHysteresis(0.28f, 4, 0, 0.03f), 0); // exactly at hi+hyst
    EXPECT_EQ(StepWithHysteresis(0.29f, 4, 0, 0.03f), 1); // clear of it
}

TEST(StepHysteresis, GuardBandWorksDownwardToo)
{
    // Zone 1 is [0.25, 0.50). Dropping just below 0.25 must not flip to zone 0.
    EXPECT_EQ(StepWithHysteresis(0.24f, 4, 1, 0.03f), 1);
    EXPECT_EQ(StepWithHysteresis(0.21f, 4, 1, 0.03f), 0);
}

TEST(StepHysteresis, DitheringOnABoundaryDoesNotFlicker)
{
    // THE POINT: a knob parked on a zone edge with a little ADC noise must hold
    // one waveform, not alternate between two every scan.
    const float kEdge = 0.25f;
    int         zone  = 0;
    for(int i = 0; i < 200; ++i)
    {
        const float noise = ((i % 2) ? 0.004f : -0.004f);
        zone              = StepWithHysteresis(kEdge + noise, 4, zone, 0.03f);
        ASSERT_EQ(zone, 0) << "flickered on scan " << i;
    }
}

TEST(StepHysteresis, FullSweepVisitsEveryZoneInOrder)
{
    int zone = 0;
    int seen[4] = {0, 0, 0, 0};
    for(int i = 0; i <= 1000; ++i)
    {
        zone = StepWithHysteresis(static_cast<float>(i) / 1000.0f, 4, zone);
        seen[zone] = 1;
    }
    EXPECT_EQ(zone, 3);
    for(int z = 0; z < 4; ++z)
        EXPECT_EQ(seen[z], 1) << "never reached zone " << z;
}

TEST(StepHysteresis, DegenerateStepCount)
{
    EXPECT_EQ(StepWithHysteresis(0.7f, 1, -1), 0);
    EXPECT_EQ(StepWithHysteresis(0.7f, 0, -1), 0);
}

// --- KnobWatch --------------------------------------------------------------

namespace
{
constexpr int kN = 10; // the example's ten pots

void Fill(float (&v)[kN], float value)
{
    for(int i = 0; i < kN; ++i)
        v[i] = value;
}
} // namespace

TEST(KnobWatch, IdleNoiseDoesNotTriggerAPopup)
{
    float    pots[kN];
    Fill(pots, 0.5f);
    KnobWatch w;
    w.Reset(pots, kN);

    for(int i = 0; i < 500; ++i)
    {
        pots[3] = 0.5f + ((i % 2) ? 0.003f : -0.003f); // well under the deadband
        ASSERT_FALSE(w.Update(pots, kN, 10u * i));
        ASSERT_EQ(w.active, -1);
    }
}

TEST(KnobWatch, ASingleMoveSelectsThatPot)
{
    float pots[kN];
    Fill(pots, 0.5f);
    KnobWatch w;
    w.Reset(pots, kN);

    pots[6] = 0.6f;
    EXPECT_TRUE(w.Update(pots, kN, 1000));
    EXPECT_EQ(w.active, 6);
}

TEST(KnobWatch, SlowSweepAccumulatesPastTheDeadband)
{
    // THE POINT of anchoring: each step here is 0.004 — a third of the deadband
    // — so a previous-sample comparison would never fire. Against the anchor,
    // three steps trip it.
    float pots[kN];
    Fill(pots, 0.5f);
    KnobWatch w;
    w.Reset(pots, kN);

    bool fired = false;
    for(int i = 1; i <= 5 && !fired; ++i)
    {
        pots[2] = 0.5f + 0.004f * static_cast<float>(i);
        fired   = w.Update(pots, kN, 10u * i);
    }
    EXPECT_TRUE(fired);
    EXPECT_EQ(w.active, 2);
}

TEST(KnobWatch, PopupExpiresAfterHoldMs)
{
    float pots[kN];
    Fill(pots, 0.5f);
    KnobWatch w;
    w.Reset(pots, kN);

    pots[0] = 0.7f;
    ASSERT_TRUE(w.Update(pots, kN, 1000));

    EXPECT_TRUE(w.Update(pots, kN, 1000 + 799));  // still holding
    EXPECT_FALSE(w.Update(pots, kN, 1000 + 800)); // expired
    EXPECT_EQ(w.active, -1);
}

TEST(KnobWatch, HoldIsExtendedByContinuedMovement)
{
    float pots[kN];
    Fill(pots, 0.5f);
    KnobWatch w;
    w.Reset(pots, kN);

    uint32_t t = 0;
    for(int i = 1; i <= 20; ++i) // keep turning for 2 s, well past hold_ms
    {
        t       = 100u * i;
        pots[4] = 0.5f + 0.02f * static_cast<float>(i);
        ASSERT_TRUE(w.Update(pots, kN, t)) << "dropped out at t=" << t;
    }
    EXPECT_FALSE(w.Update(pots, kN, t + 800));
}

TEST(KnobWatch, LatestMoverWins)
{
    float pots[kN];
    Fill(pots, 0.5f);
    KnobWatch w;
    w.Reset(pots, kN);

    pots[1] = 0.7f;
    ASSERT_TRUE(w.Update(pots, kN, 100));
    ASSERT_EQ(w.active, 1);

    pots[8] = 0.2f;
    EXPECT_TRUE(w.Update(pots, kN, 200));
    EXPECT_EQ(w.active, 8);
}

TEST(KnobWatch, ResetSeedsAnchorsWithoutDeclaringMotion)
{
    // Without Reset(), the boot-time zero cache reads as a full-scale move on
    // every pot the moment the first real scan lands.
    float pots[kN];
    Fill(pots, 0.9f);
    KnobWatch w;
    w.Reset(pots, kN);
    EXPECT_FALSE(w.Update(pots, kN, 5000));
    EXPECT_EQ(w.active, -1);
}
