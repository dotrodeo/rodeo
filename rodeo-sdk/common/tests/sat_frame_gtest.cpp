// sat_frame.h: the satellite chain's wire format, from both ends.
//
// The satellite and the Seed both include sat_frame.h, so these tests play both
// parts: the Seed builds a frame, a chain of simulated satellites passes it
// along with sat_satellite_pass(), and the Seed reads what comes back. The
// numbers are the contract's (RODEO_SATELLITE_CHAIN_CONTRACT.md).
#include <cmath>
#include <vector>

#include "gtest/gtest.h"
#include "sat_frame.h"

namespace
{
struct Satellite
{
    sat_descriptor        me;
    std::vector<sat_slot> inputs;  // what it reports
    std::vector<sat_slot> outputs; // what it last took from a SCAN
    uint8_t               position = 0;
};

sat_descriptor Desc(uint8_t kind, bool last, uint8_t major = 1, uint8_t minor = 0)
{
    sat_descriptor d;
    d.kind     = kind;
    d.slots    = sat_kind_slots(kind);
    d.last     = last;
    d.fw_major = major;
    d.fw_minor = minor;
    return d;
}

// Send a frame round the ring: through every satellite in order.
std::vector<uint8_t> Ring(std::vector<uint8_t> frame, std::vector<Satellite>& chain)
{
    for(auto& s : chain)
    {
        std::vector<uint8_t>  out(SAT_MAX_FRAME_LEN + SAT_SLOT_LEN);
        std::vector<sat_slot> taken(s.me.slots);
        bool                  took = false;
        size_t len = sat_satellite_pass(frame.data(), frame.size(), s.me, s.inputs.data(),
                                        taken.data(), &took, &s.position, out.data());
        if(took)
            s.outputs = taken;
        out.resize(len);
        frame = out;
        if(frame.empty())
            break;
    }
    return frame;
}

// A pot, an encoder bank and a button: one-slot and four-slot satellites mixed.
std::vector<Satellite> ThreeSatellites()
{
    std::vector<Satellite> c(3);
    c[0].me     = Desc(SAT_KIND_POT, false);
    c[0].inputs = {sat_pot(0x1234)};
    c[1].me     = Desc(SAT_KIND_ENC_BANK_4, false);
    c[1].inputs = {sat_encoder(1, false, 0), sat_encoder(2, true, 3), sat_encoder(250, false, 0),
                   sat_encoder(4, false, 15)};
    c[2].me     = Desc(SAT_KIND_BUTTON, true);
    c[2].inputs = {sat_button(true, 7)};
    return c;
}
} // namespace

// --- CRC and slots ---------------------------------------------------------------

TEST(SatFrame, Crc8IsSmbus)
{
    // The CRC-8/SMBUS check value: CRC of the ASCII digits "123456789".
    const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(sat_crc8(check, sizeof check), 0xF4);
    EXPECT_EQ(sat_crc8(check, 0), 0x00);
}

TEST(SatFrame, SlotCrcCatchesEverySingleBitError)
{
    sat_slot s = sat_pot(0xBEEF);
    ASSERT_TRUE(sat_slot_ok(s));
    uint8_t b[4];
    sat_slot_write(b, s);
    for(int byte = 0; byte < 4; byte++)
        for(int bit = 0; bit < 8; bit++)
        {
            uint8_t c[4] = {b[0], b[1], b[2], b[3]};
            c[byte] ^= (uint8_t)(1 << bit);
            EXPECT_FALSE(sat_slot_ok(sat_slot_read(c))) << "byte " << byte << " bit " << bit;
        }
}

TEST(SatFrame, InputSlotsEncodeAsTheContractSays)
{
    sat_slot p = sat_pot(0xABCD);
    EXPECT_EQ(p.kind, SAT_IN_POT);
    EXPECT_EQ(p.d0, 0xCD); // little-endian
    EXPECT_EQ(p.d1, 0xAB);
    EXPECT_EQ(sat_pot_position(p), 0xABCD);

    sat_slot b = sat_button(true, 200);
    EXPECT_EQ(b.d0 & 1, 1);
    EXPECT_EQ(b.d1, 200);

    sat_slot e = sat_encoder(77, true, 18); // press counter wraps at 16
    EXPECT_EQ(e.d0, 77);
    EXPECT_EQ(e.d1 & 1, 1);
    EXPECT_EQ(e.d1 >> 4, 2);

    EXPECT_EQ(sat_switch(5).d0, 5);
    EXPECT_EQ(sat_fault(SAT_FAULT_SENSOR, 0).kind, SAT_IN_FAULT);
    EXPECT_EQ(sat_led(128, SAT_LED_FAST_BLINK).kind, SAT_OUT_LED);
}

TEST(SatFrame, CountersGiveDirectionAcrossTheWrap)
{
    EXPECT_EQ(sat_delta8(3, 250), 9);   // forward through 255 -> 0
    EXPECT_EQ(sat_delta8(250, 3), -9);  // backward through 0 -> 255
    EXPECT_EQ(sat_delta8(10, 10), 0);
    EXPECT_EQ(sat_delta4(1, 14), 3);    // the encoder's 4-bit press counter
}

// --- Board kinds -----------------------------------------------------------------

TEST(SatFrame, IdResistorGivesTheKindAtBothEndsOfTolerance)
{
    // R_ID per kind (§1.6), against 10 k from the top. 0 = short, -1 = not fitted.
    const double rid[16] = {0,     715,   1540,  2490,  3650,  4990,  6650,  8660,
                            11500, 15000, 20000, 27400, 40200, 64900, 140000, -1};
    const uint16_t fs = 1023; // the CH32V003's 10-bit ADC
    for(int kind = 0; kind < 16; kind++)
        for(double top : {9900.0, 10100.0})       // 10 k, 1 %
            for(double tol : {0.99, 1.01})        // R_ID, 1 %
            {
                double frac = rid[kind] < 0 ? 1.0 : rid[kind] * tol / (top + rid[kind] * tol);
                auto   reading = static_cast<uint16_t>(std::lround(frac * fs));
                EXPECT_EQ(sat_kind_from_adc(reading, fs, nullptr), kind)
                    << "kind " << kind << " reads " << reading;
            }
}

TEST(SatFrame, ReadingBetweenBandsIsAFaultWithItsDetail)
{
    const uint16_t fs = 1023;
    uint8_t        tenths = 0;
    // 7.5 kinds: exactly between 7 and 8.
    EXPECT_EQ(sat_kind_from_adc(static_cast<uint16_t>(7.5 / 15 * fs), fs, &tenths), -1);
    EXPECT_EQ(tenths, 75);
    // Within the tolerance still counts.
    EXPECT_EQ(sat_kind_from_adc(static_cast<uint16_t>(7.2 / 15 * fs), fs, nullptr), 7);
}

TEST(SatFrame, KindsOwnTheirSlotsAndReportTheirInputs)
{
    EXPECT_EQ(sat_kind_slots(SAT_KIND_POT), 1);
    EXPECT_EQ(sat_kind_slots(SAT_KIND_POT_BANK_4), 4);
    EXPECT_EQ(sat_kind_slots(SAT_KIND_ENC_BANK_4), 4);
    EXPECT_EQ(sat_kind_input(SAT_KIND_POT_BANK_4), SAT_IN_POT);
    EXPECT_EQ(sat_kind_input(SAT_KIND_ENC_BANK_4), SAT_IN_ENCODER);
    EXPECT_EQ(sat_kind_input(SAT_KIND_NO_ID), SAT_IN_EMPTY);
    EXPECT_TRUE(sat_kind_supported(SAT_KIND_NO_ID));
    EXPECT_FALSE(sat_kind_supported(9));
}

// --- Headers ---------------------------------------------------------------------

TEST(SatFrame, ReceiverRefusesBadHeaders)
{
    uint8_t h[4];
    sat_header_write(h, SAT_SCAN, 64, 0);
    EXPECT_TRUE(sat_header_ok(h));
    h[SAT_H_N] = 65;
    EXPECT_FALSE(sat_header_ok(h));
    sat_header_write(h, 0x04, 0, 0);
    EXPECT_FALSE(sat_header_ok(h));
    sat_header_write(h, SAT_SCAN, 0, 0);
    h[SAT_H_SOF] = 0xA6;
    EXPECT_FALSE(sat_header_ok(h));
    EXPECT_EQ(SAT_MAX_FRAME_LEN, 260u);
}

// --- Discovery round the ring ----------------------------------------------------

TEST(SatFrame, DiscoverFindsEverySatelliteInOrder)
{
    auto                 chain = ThreeSatellites();
    std::vector<uint8_t> f(SAT_HEADER_LEN);
    f.resize(sat_build_discover(f.data()));
    auto back = Ring(f, chain);

    ASSERT_EQ(back.size(), SAT_FRAME_LEN(3));
    sat_descriptor d[8];
    uint8_t        count = 0;
    ASSERT_EQ(sat_read_discover(back.data(), back.size(), 1, d, 8, &count), SAT_OK);
    ASSERT_EQ(count, 3);
    EXPECT_EQ(d[0].kind, SAT_KIND_POT);
    EXPECT_EQ(d[1].kind, SAT_KIND_ENC_BANK_4);
    EXPECT_EQ(d[1].slots, 4);
    EXPECT_EQ(d[2].kind, SAT_KIND_BUTTON);
    EXPECT_TRUE(d[2].last);
    EXPECT_FALSE(d[0].last || d[1].last);
    EXPECT_EQ(chain[0].position, 1);
    EXPECT_EQ(chain[2].position, 3);
}

TEST(SatFrame, EmptyChainDiscoversNothing)
{
    std::vector<Satellite> none;
    std::vector<uint8_t>   f(SAT_HEADER_LEN);
    sat_build_discover(f.data());
    auto           back = Ring(f, none);
    sat_descriptor d[1];
    uint8_t        count = 9;
    EXPECT_EQ(sat_read_discover(back.data(), back.size(), 1, d, 1, &count), SAT_OK);
    EXPECT_EQ(count, 0);
}

TEST(SatFrame, DiscoverRefusesWhatTheContractRefuses)
{
    sat_descriptor d[8];
    uint8_t        count;
    auto discover = [](std::vector<Satellite>& chain) {
        std::vector<uint8_t> f(SAT_HEADER_LEN);
        sat_build_discover(f.data());
        return Ring(f, chain);
    };

    { // a satellite on another major version
        auto chain     = ThreeSatellites();
        chain[1].me    = Desc(SAT_KIND_ENC_BANK_4, false, 2);
        auto back      = discover(chain);
        EXPECT_EQ(sat_read_discover(back.data(), back.size(), 1, d, 8, &count), SAT_ERR_VERSION);
    }
    { // two satellites both think they are last
        auto chain  = ThreeSatellites();
        chain[0].me = Desc(SAT_KIND_POT, true);
        auto back   = discover(chain);
        EXPECT_EQ(sat_read_discover(back.data(), back.size(), 1, d, 8, &count), SAT_ERR_LAST);
    }
    { // a corrupted descriptor
        auto chain = ThreeSatellites();
        auto back  = discover(chain);
        back[SAT_HEADER_LEN + 5] ^= 0x10;
        EXPECT_EQ(sat_read_discover(back.data(), back.size(), 1, d, 8, &count), SAT_ERR_CRC);
    }
    { // HOP disagrees with the descriptors (a satellite that didn't count itself)
        auto chain = ThreeSatellites();
        auto back  = discover(chain);
        back[SAT_H_HOP] = 2;
        EXPECT_EQ(sat_read_discover(back.data(), back.size(), 1, d, 8, &count), SAT_ERR_HOP);
    }
    { // truncated: the ring timed out mid-frame
        auto chain = ThreeSatellites();
        auto back  = discover(chain);
        EXPECT_EQ(sat_read_discover(back.data(), back.size() - 2, 1, d, 8, &count), SAT_ERR_SHORT);
    }
    { // more satellites than the caller has room for
        auto chain = ThreeSatellites();
        auto back  = discover(chain);
        EXPECT_EQ(sat_read_discover(back.data(), back.size(), 1, d, 2, &count), SAT_ERR_TOO_MANY);
    }
}

// --- Scanning round the ring -----------------------------------------------------

TEST(SatFrame, ScanDeliversOutputsAndCollectsInputsInChainOrder)
{
    auto chain = ThreeSatellites();
    // Six slots: 1 for the pot, 4 for the bank, 1 for the button.
    std::vector<sat_slot> outputs = {sat_led(10, SAT_LED_STEADY),  sat_led(20, SAT_LED_SLOW_BLINK),
                                     sat_none(),                  sat_none(),
                                     sat_none(),                  sat_led(60, SAT_LED_FAST_BLINK)};
    std::vector<uint8_t>  f(SAT_FRAME_LEN(6));
    sat_build_scan(f.data(), outputs.data(), 6);
    auto back = Ring(f, chain);

    ASSERT_EQ(back.size(), SAT_FRAME_LEN(6)); // a SCAN comes back the length it left
    ASSERT_EQ(sat_check_scan(back.data(), back.size(), 6, 3), SAT_OK);

    // Each satellite took its own outputs off the front.
    EXPECT_EQ(chain[0].outputs[0].d0, 10);
    EXPECT_EQ(chain[1].outputs[0].d0, 20);
    EXPECT_EQ(chain[1].outputs[0].d1, SAT_LED_SLOW_BLINK);
    EXPECT_EQ(chain[1].outputs[3].kind, SAT_OUT_NONE);
    EXPECT_EQ(chain[2].outputs[0].d0, 60);

    // And the inputs came back in chain order, every CRC intact.
    std::vector<sat_slot> in;
    for(int i = 0; i < 6; i++)
    {
        in.push_back(sat_slot_read(back.data() + SAT_HEADER_LEN + SAT_SLOT_LEN * i));
        EXPECT_TRUE(sat_slot_ok(in.back())) << i;
    }
    EXPECT_EQ(sat_pot_position(in[0]), 0x1234);
    EXPECT_EQ(in[1].kind, SAT_IN_ENCODER);
    EXPECT_EQ(in[2].d0, 2);
    EXPECT_EQ(in[2].d1 & 1, 1);
    EXPECT_EQ(in[3].d0, 250);
    EXPECT_EQ(in[4].d1 >> 4, 15);
    EXPECT_EQ(in[5].kind, SAT_IN_BUTTON);
    EXPECT_EQ(in[5].d1, 7);
}

TEST(SatFrame, StaleMapScanPassesThroughUntouched)
{
    // §2.3: a satellite that gets fewer slots than it owns forwards the frame
    // with HOP + 1 and reports nothing.
    std::vector<Satellite> chain(1);
    chain[0].me     = Desc(SAT_KIND_POT_BANK_4, true);
    chain[0].inputs = {sat_pot(1), sat_pot(2), sat_pot(3), sat_pot(4)};
    sat_slot              one[1] = {sat_led(5, 0)};
    std::vector<uint8_t>  f(SAT_FRAME_LEN(1));
    sat_build_scan(f.data(), one, 1);
    auto back = Ring(f, chain);
    ASSERT_EQ(back.size(), SAT_FRAME_LEN(1));
    EXPECT_EQ(back[SAT_H_HOP], 1);
    EXPECT_EQ(sat_slot_read(back.data() + SAT_HEADER_LEN).kind, SAT_OUT_LED);
    EXPECT_TRUE(chain[0].outputs.empty());
}

TEST(SatFrame, ScanCheckCatchesAChainThatChanged)
{
    auto                  chain = ThreeSatellites();
    std::vector<sat_slot> outputs(6, sat_none());
    std::vector<uint8_t>  f(SAT_FRAME_LEN(6));
    sat_build_scan(f.data(), outputs.data(), 6);
    auto back = Ring(f, chain);
    EXPECT_EQ(sat_check_scan(back.data(), back.size(), 6, 4), SAT_ERR_HOP); // one went missing
    EXPECT_EQ(sat_check_scan(back.data(), back.size() - 1, 6, 3), SAT_ERR_SHORT);
    EXPECT_EQ(sat_check_scan(back.data(), back.size(), 5, 3), SAT_ERR_HEADER);
}

// --- Commands --------------------------------------------------------------------

TEST(SatFrame, CommandsReachTheSatelliteTheyAddress)
{
    EXPECT_TRUE(sat_command_for(sat_command(SAT_CMD_CLOSE_RING_HERE, 2, 0), 2));
    EXPECT_FALSE(sat_command_for(sat_command(SAT_CMD_CLOSE_RING_HERE, 2, 0), 3));
    // 0 means everyone, for the commands that allow it.
    EXPECT_TRUE(sat_command_for(sat_command(SAT_CMD_RING_AUTO, 0, 0), 7));
    EXPECT_FALSE(sat_command_for(sat_command(SAT_CMD_CLOSE_RING_HERE, 0, 0), 7));
    // RESET needs its guard byte.
    EXPECT_TRUE(sat_command_for(sat_command(SAT_CMD_RESET, 0, SAT_RESET_GUARD), 4));
    EXPECT_FALSE(sat_command_for(sat_command(SAT_CMD_RESET, 4, 0x00), 4));
    // A corrupted command does nothing.
    sat_slot c = sat_command(SAT_CMD_IDENTIFY, 1, 3);
    c.d1 ^= 1;
    EXPECT_FALSE(sat_command_for(c, 1));
}

TEST(SatFrame, CommandFrameReachesEverySatelliteUnchanged)
{
    auto     chain  = ThreeSatellites();
    sat_slot cmd[1] = {sat_command(SAT_CMD_IDENTIFY, 2, 5)};
    std::vector<uint8_t> f(SAT_FRAME_LEN(1));
    sat_build_command(f.data(), cmd, 1);
    auto back = Ring(f, chain);
    ASSERT_EQ(back.size(), SAT_FRAME_LEN(1));
    EXPECT_EQ(back[SAT_H_HOP], 3);
    sat_slot r = sat_slot_read(back.data() + SAT_HEADER_LEN);
    EXPECT_TRUE(sat_command_for(r, 2));
}

// --- Timing ----------------------------------------------------------------------

TEST(SatFrame, RingTimeMatchesTheContractsExample)
{
    // §6.2: 30 one-slot satellites, about 2.5 ms; the timeout twice that.
    EXPECT_EQ(sat_ring_time_us(30, 30), 2440u);
    EXPECT_EQ(sat_timeout_us(30, 30), 4880u);
    EXPECT_EQ(sat_timeout_us(0, 0), 1000u); // never under 1 ms
}
