// board.h: the Rev B pin map checked against itself and against libDaisy.
//
// The map is a hand-copied table, so these tests are what stand between a
// typo and a driver that drives the wrong pin: every pin is used once, and
// every seed::D / seed::A number agrees with libDaisy's own table, read
// straight out of libDaisy/src/daisy_seed.h.
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "board.h"

using board::SeedPin;

namespace
{
std::vector<std::pair<const char*, SeedPin>> AllPins()
{
    using namespace board::pin;
    std::vector<std::pair<const char*, SeedPin>> v = {
        {"kSatRx", kSatRx},           {"kSatTx", kSatTx},
        {"kCodec2Reset", kCodec2Reset}, {"kCodec2Mclk", kCodec2Mclk},
        {"kCodec2Sdout", kCodec2Sdout}, {"kCodec2Sdin", kCodec2Sdin},
        {"kCodec2Lrck", kCodec2Lrck}, {"kCodec2Sclk", kCodec2Sclk},
        {"kSdD0", kSdD0},             {"kSdCmd", kSdCmd},
        {"kSdClk", kSdClk},           {"kSpiExtCs", kSpiExtCs},
        {"kSpiSck", kSpiSck},         {"kSpiMiso", kSpiMiso},
        {"kSpiMosi", kSpiMosi},       {"kI2cScl", kI2cScl},
        {"kI2cSda", kI2cSda},         {"kMidiTx", kMidiTx},
        {"kMidiRx", kMidiRx},         {"kUsbDm", kUsbDm},
        {"kUsbDp", kUsbDp},
    };
    for(const auto& io : board::kIo)
        v.push_back({io.label, io.pin});
    return v;
}

// libDaisy's seed::D<n> -> (port, pin) and seed::A<n> -> D<n>, parsed from
// daisy_seed.h. Empty if the submodule isn't checked out.
struct DaisyTable
{
    std::map<int, std::pair<char, int>> d;
    std::map<int, int>                  a;
};

DaisyTable ReadDaisySeed()
{
    DaisyTable    t;
    std::ifstream f("../../libDaisy/src/daisy_seed.h");
    std::string   line;
    std::regex    dre(R"(constexpr Pin D(\d+)\s*=\s*Pin\(PORT([A-K]),\s*(\d+)\))");
    std::regex    are(R"(constexpr Pin A(\d+)\s*=\s*D(\d+);)");
    std::smatch   m;
    while(std::getline(f, line))
    {
        if(std::regex_search(line, m, dre))
            t.d[std::stoi(m[1])] = {m[2].str()[0], std::stoi(m[3])};
        else if(std::regex_search(line, m, are))
            t.a[std::stoi(m[1])] = std::stoi(m[2]);
    }
    return t;
}
} // namespace

TEST(Board, EveryPinIsUsedOnce)
{
    auto pins = AllPins();
    for(size_t i = 0; i < pins.size(); i++)
        for(size_t j = i + 1; j < pins.size(); j++)
        {
            EXPECT_FALSE(board::SamePin(pins[i].second, pins[j].second))
                << pins[i].first << " and " << pins[j].first;
            EXPECT_NE(pins[i].second.header, pins[j].second.header)
                << pins[i].first << " and " << pins[j].first;
            EXPECT_NE(pins[i].second.seed_d, pins[j].second.seed_d)
                << pins[i].first << " and " << pins[j].first;
        }
}

TEST(Board, AgreesWithLibDaisy)
{
    DaisyTable t = ReadDaisySeed();
    if(t.d.empty())
        GTEST_SKIP() << "libDaisy/src/daisy_seed.h not found (run from common/tests)";

    for(const auto& p : AllPins())
    {
        ASSERT_TRUE(t.d.count(p.second.seed_d)) << p.first;
        EXPECT_EQ(t.d[p.second.seed_d].first, p.second.port)
            << p.first << " is seed::D" << int(p.second.seed_d);
        EXPECT_EQ(t.d[p.second.seed_d].second, p.second.num)
            << p.first << " is seed::D" << int(p.second.seed_d);
    }
    for(const auto& io : board::kIo)
        if(io.adc >= 0)
        {
            ASSERT_TRUE(t.a.count(io.adc)) << io.label;
            EXPECT_EQ(t.a[io.adc], io.pin.seed_d)
                << io.label << ": seed::A" << int(io.adc) << " is not seed::D"
                << int(io.pin.seed_d);
        }
}

TEST(Board, DacChannelsAreTheDacPins)
{
    // STM32H7: DAC1 channel 1 is PA4, channel 2 is PA5 (libDaisy per/dac.h).
    for(const auto& io : board::kIo)
    {
        if(io.dac == 1)
            EXPECT_TRUE(io.pin.port == 'A' && io.pin.num == 4) << io.label;
        if(io.dac == 2)
            EXPECT_TRUE(io.pin.port == 'A' && io.pin.num == 5) << io.label;
    }
    EXPECT_EQ(board::kIo[board::kIoCv1].dac, 1);
    EXPECT_EQ(board::kIo[board::kIoCv2].dac, 2);
}

TEST(Board, IoEnumMatchesTheTable)
{
    const char* labels[board::kNumIo]
        = {"D1", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "CV1", "CV2"};
    for(int i = 0; i < board::kNumIo; i++)
        EXPECT_STREQ(board::kIo[i].label, labels[i]);
    EXPECT_EQ(board::kIo[board::kIoD1].adc, -1); // pin 8 has no ADC
}

TEST(Board, SharedInterruptLinesAreTheKnownPairs)
{
    // Only one connector of each pair can be an interrupt input. A change that
    // adds a pair should fail here and be written down where the I/O API
    // enforces it.
    std::set<std::pair<std::string, std::string>> shared;
    for(int i = 0; i < board::kNumIo; i++)
        for(int j = i + 1; j < board::kNumIo; j++)
            if(board::SharesExti(board::kIo[i].pin, board::kIo[j].pin))
                shared.insert({board::kIo[i].label, board::kIo[j].label});
    std::set<std::pair<std::string, std::string>> known = {
        {"A3", "A6"},  // PB1 and PC1, line 1
        {"A7", "CV1"}, // PC4 and PA4, line 4
    };
    EXPECT_EQ(shared, known);
}
