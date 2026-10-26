// Tests for the MAX17048 decoding in common/max17048.h.
//
// The values are the datasheet's units checked at the points that matter on
// the bench: a cell near full and near empty, the byte order on the wire, and
// the sign of CRATE, which is the only way firmware can tell charging from
// discharging (PGOOD and CHG don't reach the Seed).

#include "gtest/gtest.h"

#include "max17048.h"

using namespace rodeo::max17048;

TEST(Max17048, RegistersAreBigEndian)
{
    const uint8_t b[2] = {0xC1, 0x80};
    EXPECT_EQ(Word(b), 0xC180);
}

TEST(Max17048, VoltageScale)
{
    EXPECT_NEAR(Volts(0xD200), 4.200f, 0.001f); // 53760 * 78.125 uV
    EXPECT_NEAR(Volts(0x9600), 3.000f, 0.001f);
    EXPECT_EQ(Volts(0), 0.f);
}

TEST(Max17048, PercentScale)
{
    EXPECT_NEAR(Percent(0x4080), 64.5f, 1e-4f); // high byte %, low byte 1/256
    EXPECT_NEAR(Percent(0x6400), 100.f, 1e-4f);
    EXPECT_NEAR(Percent(0x6500), 101.f, 1e-4f); // can read over 100 when full
}

TEST(Max17048, RateIsSigned)
{
    EXPECT_NEAR(PercentPerHr(100), 20.8f, 1e-3f);
    EXPECT_NEAR(PercentPerHr(0xFF9C), -20.8f, 1e-3f); // -100
    EXPECT_EQ(PercentPerHr(0), 0.f);
}

TEST(Max17048, TrendFromRate)
{
    EXPECT_EQ(Decode(0xC000, 0x3200, 50).trend, Trend::kCharging);
    EXPECT_EQ(Decode(0xC000, 0x3200, static_cast<uint16_t>(-50)).trend, Trend::kDischarging);
    EXPECT_EQ(Decode(0xC000, 0x3200, 2).trend, Trend::kIdle);  // +0.4 %/hr: noise
    EXPECT_EQ(Decode(0xC000, 0x3200, static_cast<uint16_t>(-2)).trend, Trend::kIdle);
    EXPECT_STREQ(TrendName(Trend::kDischarging), "discharging");
}

TEST(Max17048, VersionCheck)
{
    EXPECT_TRUE(VersionOk(0x0011));
    EXPECT_TRUE(VersionOk(0x0012));
    EXPECT_FALSE(VersionOk(0x0000)); // a bus reading zeros
    EXPECT_FALSE(VersionOk(0xFFFF)); // nothing pulling SDA down
}
