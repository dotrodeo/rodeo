// Host tests for the ADC value pipeline (common/adc_convert.h).
//
// These functions ARE the production path — RodeoAdc::GetCode/GetVolts/GetNorm
// delegate straight to them — so a green run here guards the real firmware.

#include "adc_convert.h"
#include <gtest/gtest.h>

using namespace rodeo;

// ---------------------------------------------------------------------------
// The glitch guard. A unipolar pot at fully-off can read a few codes NEGATIVE.
// Without the clamp, casting that to uint16_t wraps it to ~full-scale, so a
// knob turned all the way DOWN would read all the way UP. This is the single
// highest-value test in the firmware; keep it forever.
// ---------------------------------------------------------------------------
TEST(AdcClamp, NegativeTailClampsToZero)
{
    EXPECT_EQ(ClampAdcCode(-1), 0u);
    EXPECT_EQ(ClampAdcCode(-4), 0u);       // typical fully-off pot dip
    EXPECT_EQ(ClampAdcCode(-32768), 0u);   // most-negative int16
}

TEST(AdcClamp, NonNegativePassesThrough)
{
    EXPECT_EQ(ClampAdcCode(0), 0u);
    EXPECT_EQ(ClampAdcCode(1), 1u);
    EXPECT_EQ(ClampAdcCode(16384), 16384u);
    EXPECT_EQ(ClampAdcCode(32767), 32767u); // full scale
}

// Regression pin for the exact bug: (uint16_t)(-4) == 65532 without the clamp.
TEST(AdcClamp, NegativeNeverWrapsToFullScale)
{
    EXPECT_NE(ClampAdcCode(-4), 65532u);
    EXPECT_LT(ClampAdcCode(-4), 32768u);
}

// ---------------------------------------------------------------------------
// code -> volts, ratiometric 3.3 V reference.
// ---------------------------------------------------------------------------
TEST(AdcVolts, Endpoints)
{
    EXPECT_FLOAT_EQ(AdcCodeToVolts(0, 3.3f), 0.0f);
    EXPECT_FLOAT_EQ(AdcCodeToVolts(32767, 3.3f), 3.3f);
}

TEST(AdcVolts, Midscale)
{
    // Half of full-scale code -> ~half of Vref.
    EXPECT_NEAR(AdcCodeToVolts(16384, 3.3f), 1.65f, 1e-3f);
}

TEST(AdcVolts, ScalesWithVref)
{
    // Same code, different reference -> proportional volts.
    EXPECT_FLOAT_EQ(AdcCodeToVolts(32767, 5.0f), 5.0f);
}

// ---------------------------------------------------------------------------
// code -> normalized 0..1.
// ---------------------------------------------------------------------------
TEST(AdcNorm, Endpoints)
{
    EXPECT_FLOAT_EQ(AdcCodeToNorm(0), 0.0f);
    EXPECT_FLOAT_EQ(AdcCodeToNorm(32767), 1.0f);
}

TEST(AdcNorm, Midscale)
{
    EXPECT_NEAR(AdcCodeToNorm(16384), 0.5f, 1e-4f);
}
