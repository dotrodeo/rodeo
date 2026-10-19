// io_plan.h: an I/O configuration is checked against the connectors before any
// pin is touched.
#include "gtest/gtest.h"
#include "io_plan.h"

using rodeo::IoConfig;
using rodeo::IoMode;
using rodeo::PlanIo;

TEST(IoPlan, AnEmptyConfigurationIsFine)
{
    auto p = PlanIo(IoConfig());
    EXPECT_TRUE(p.ok);
    EXPECT_EQ(p.n_adc, 0);
    EXPECT_FALSE(p.dac1 || p.dac2);
}

TEST(IoPlan, AnalogInputsAreScannedInConnectorOrder)
{
    auto p = PlanIo(IoConfig()
                        .Set(board::kIoCv2, IoMode::kAnalogIn)
                        .Set(board::kIoA1, IoMode::kAnalogIn)
                        .Set(board::kIoA7, IoMode::kAnalogIn));
    ASSERT_TRUE(p.ok);
    ASSERT_EQ(p.n_adc, 3);
    EXPECT_EQ(p.adc_conn[0], board::kIoA1);
    EXPECT_EQ(p.adc_conn[1], board::kIoA7);
    EXPECT_EQ(p.adc_conn[2], board::kIoCv2);
    EXPECT_EQ(p.adc_index[board::kIoA7], 1);
    EXPECT_EQ(p.adc_index[board::kIoA2], -1);
}

TEST(IoPlan, D1CannotBeAnalog)
{
    auto p = PlanIo(IoConfig().Set(board::kIoD1, IoMode::kAnalogIn));
    EXPECT_FALSE(p.ok);
    EXPECT_EQ(p.bad, board::kIoD1);
    EXPECT_NE(p.why, nullptr);
}

TEST(IoPlan, CvOutOnlyOnTheDacPins)
{
    auto bad = PlanIo(IoConfig().Set(board::kIoA3, IoMode::kCvOut));
    EXPECT_FALSE(bad.ok);
    EXPECT_EQ(bad.bad, board::kIoA3);

    auto p = PlanIo(IoConfig().Set(board::kIoCv1, IoMode::kCvOut).Set(board::kIoCv2, IoMode::kAnalogIn));
    ASSERT_TRUE(p.ok);
    EXPECT_TRUE(p.dac1);  // CV1 is DAC channel 1 (PA4)
    EXPECT_FALSE(p.dac2); // CV2 reads instead
    EXPECT_EQ(p.n_adc, 1);
}

TEST(IoPlan, DacCodesClampAndRound)
{
    EXPECT_EQ(rodeo::DacCode(-1.0f), 0);
    EXPECT_EQ(rodeo::DacCode(0.0f), 0);
    EXPECT_EQ(rodeo::DacCode(0.5f), 2048);
    EXPECT_EQ(rodeo::DacCode(1.0f), 4095);
    EXPECT_EQ(rodeo::DacCode(2.0f), 4095);
}
