// Tests for the pot span calibration in common/adc_convert.h.
//
// Why it exists: a rodeo-potentiometer satellite feeds its pot's top leg from
// +3V3 through R1 = 1 kOhm, so the top of travel is 3.3 * Rpot/(Rpot+1k), not
// 3.3 V. With the nominal 10 kOhm pot that is 3.00 V = 90.9 % of full scale, so
// uncorrected every knob topped out at 91 %. RodeoAdc::GetNorm delegates to
// AdcCodeToNormSpan, so these tests guard the real firmware path.

#include "gtest/gtest.h"
#include "adc_convert.h"

using namespace rodeo;

namespace
{
// Code a pot of `rpot` ohms actually produces at its clockwise stop.
uint16_t TopCodeFor(float rpot)
{
    return static_cast<uint16_t>(static_cast<float>(kAdcCodeMax)
                                 * (rpot / (rpot + kPotSeriesOhms)));
}
constexpr float kNominal = 10000.0f;
constexpr float kMinus20 = 8000.0f;
constexpr float kPlus20  = 12000.0f;
} // namespace

// --- The bug this whole mechanism exists to fix -----------------------------

TEST(PotSpan, UncorrectedNominalPotTopsOutAtNinetyOnePercent)
{
    // Documents the symptom: raw full-scale normalization can never reach 1.0
    // through the satellite's divider.
    EXPECT_NEAR(AdcCodeToNorm(TopCodeFor(kNominal)), 0.909f, 0.002f);
}

TEST(PotSpan, SeededSpanAlreadyReachesFullScaleOnANominalPot)
{
    // No sweep, no learning: straight out of reset a nominal pot must reach 1.0.
    PotSpan span;
    EXPECT_FLOAT_EQ(AdcCodeToNormSpan(TopCodeFor(kNominal), span), 1.0f);
}

TEST(PotSpan, SeededSpanReachesFullScaleAcrossTheToleranceBand)
{
    // The seed is pessimistic on purpose, so even a -20 % pot hits 1.0 unlearned.
    for(float rpot : {kMinus20, 9000.0f, kNominal, 11000.0f, kPlus20})
    {
        PotSpan span;
        EXPECT_FLOAT_EQ(AdcCodeToNormSpan(TopCodeFor(rpot), span), 1.0f)
            << "pot = " << rpot << " ohms";
    }
}

TEST(PotSpan, BottomOfTravelReachesZero)
{
    PotSpan span;
    EXPECT_FLOAT_EQ(AdcCodeToNormSpan(0, span), 0.0f);
    // A few codes of ADC offset at the CCW stop must still read as 0.
    EXPECT_FLOAT_EQ(AdcCodeToNormSpan(40, span), 0.0f);
}

TEST(PotSpan, MidTravelIsNearHalf)
{
    PotSpan span;
    span.Observe(0);
    span.Observe(TopCodeFor(kNominal));
    const uint16_t mid = static_cast<uint16_t>(TopCodeFor(kNominal) / 2);
    EXPECT_NEAR(AdcCodeToNormSpan(mid, span), 0.5f, 0.02f);
}

// --- Learning ---------------------------------------------------------------

TEST(PotSpan, ObserveWidensAndReportsChange)
{
    PotSpan span;
    const uint16_t hi0 = span.hi;
    EXPECT_TRUE(span.Observe(hi0 + 100));
    EXPECT_EQ(span.hi, hi0 + 100);
    EXPECT_FALSE(span.Observe(hi0 + 50)); // inside: no change
    EXPECT_EQ(span.hi, hi0 + 100);

    EXPECT_TRUE(span.Observe(5));
    EXPECT_EQ(span.lo, 5u);
    EXPECT_FALSE(span.Observe(200));
}

TEST(PotSpan, LearningNeverNarrowsTheSpan)
{
    PotSpan span;
    span.Observe(0);
    span.Observe(TopCodeFor(kPlus20));
    const uint16_t lo = span.lo, hi = span.hi;
    for(uint16_t c = 4000; c < 20000; c += 500) // a partial sweep
        span.Observe(c);
    EXPECT_EQ(span.lo, lo);
    EXPECT_EQ(span.hi, hi);
}

TEST(PotSpan, AFullSweepMakesTheEndpointsExact)
{
    // A +20 % pot: unlearned it saturates early, learned it uses the full travel.
    const uint16_t top = TopCodeFor(kPlus20);
    PotSpan        span;
    // Just below the stop, before learning, already reads 1.0 (early saturation).
    EXPECT_FLOAT_EQ(AdcCodeToNormSpan(static_cast<uint16_t>(top * 0.97f), span),
                    1.0f);
    // Sweep it end to end.
    span.Observe(0);
    span.Observe(top);
    // Now 97 % of travel reads as clearly less than full.
    EXPECT_LT(AdcCodeToNormSpan(static_cast<uint16_t>(top * 0.97f), span), 0.99f);
    // ...and the stop itself still reads exactly 1.0.
    EXPECT_FLOAT_EQ(AdcCodeToNormSpan(top, span), 1.0f);
}

TEST(PotSpan, GlitchAboveThePhysicalCeilingIsRejected)
{
    // THE GUARD: one spurious full-scale sample must not permanently widen the
    // span and undo the calibration for every knob after it.
    PotSpan span;
    const uint16_t hi0 = span.hi;
    EXPECT_FALSE(span.Observe(kAdcCodeMax));
    EXPECT_EQ(span.hi, hi0);
    EXPECT_FALSE(span.Observe(kPotMaxPlausibleHiCode + 1));
    EXPECT_EQ(span.hi, hi0);
    // A real +20 % pot is below the ceiling and IS accepted.
    EXPECT_TRUE(span.Observe(TopCodeFor(kPlus20)));
}

TEST(PotSpan, StillMonotonicAfterAGlitchAttempt)
{
    PotSpan span;
    span.Observe(kAdcCodeMax); // rejected
    span.Observe(0);
    span.Observe(TopCodeFor(kNominal));
    float prev = -1.0f;
    for(uint16_t c = 0; c < TopCodeFor(kNominal); c += 256)
    {
        const float n = AdcCodeToNormSpan(c, span);
        EXPECT_GE(n, prev);
        EXPECT_GE(n, 0.0f);
        EXPECT_LE(n, 1.0f);
        prev = n;
    }
}

// --- Degenerate channels ----------------------------------------------------

TEST(PotSpan, CollapsedSpanFallsBackToRawFullScale)
{
    // An absent or shorted channel must not divide by ~zero or produce garbage.
    PotSpan span;
    span.lo = 1000;
    span.hi = 1000;
    EXPECT_FLOAT_EQ(AdcCodeToNormSpan(16384, span), AdcCodeToNorm(16384));

    span.hi = 900; // inverted
    EXPECT_FLOAT_EQ(AdcCodeToNormSpan(16384, span), AdcCodeToNorm(16384));

    span.lo = 0; // too narrow to be a real knob
    span.hi = static_cast<uint16_t>(kPotMinSpanCodes - 1);
    EXPECT_FLOAT_EQ(AdcCodeToNormSpan(1000, span), AdcCodeToNorm(1000));
}

TEST(PotSpan, OutputAlwaysStaysInRange)
{
    PotSpan span;
    span.Observe(0);
    span.Observe(TopCodeFor(kNominal));
    for(uint32_t c = 0; c <= static_cast<uint32_t>(kAdcCodeMax); c += 97)
    {
        const float n = AdcCodeToNormSpan(static_cast<uint16_t>(c), span);
        ASSERT_GE(n, 0.0f);
        ASSERT_LE(n, 1.0f);
    }
}

// --- The hardware constants -------------------------------------------------

TEST(PotSpan, SeedMatchesTheSatelliteDivider)
{
    // If R1 or the pot contract ever changes, this is the test that notices.
    EXPECT_NEAR(PotTopFraction(kNominal), 10.0f / 11.0f, 1e-6f);
    EXPECT_EQ(kPotSeedHiCode, TopCodeFor(kMinus20));
    EXPECT_LT(kPotSeedHiCode, TopCodeFor(kNominal)); // seed is pessimistic
    EXPECT_GT(kPotMaxPlausibleHiCode, TopCodeFor(kPlus20)); // headroom for +20 %
}
