// The chain engine (sat_chain.h) against a simulated ring.
//
// The ring is built from sat_satellite_pass(), the reference model of a
// satellite, plus what the hardware does around it: the last satellite turns
// the frame back, CLOSE_RING_HERE and RING_AUTO take effect once a frame has
// passed, and a dead satellite or a cut cable swallows the frame, so the Seed
// sees nothing until its timeout. The engine never knows it isn't real.
#include <vector>

#include "gtest/gtest.h"
#include "sat_chain.h"

using rodeo::SatChain;

namespace
{
struct SimSat
{
    sat_descriptor me;
    sat_slot       inputs[4];
    bool           alive  = true;
    bool           forced = false; // CLOSE_RING_HERE in effect
    sat_slot       led    = {0, 0, 0, 0};
    int            frames = 0;
};

struct Ring
{
    std::vector<SimSat> sats;
    int                 cut_after = -1; // a cable cut after this many satellites

    // One frame round the ring: the reply, or nothing if it never comes back.
    bool Pass(const uint8_t* tx, size_t n, std::vector<uint8_t>& reply)
    {
        std::vector<uint8_t> frame(tx, tx + n);
        for(size_t i = 0; i < sats.size(); i++)
        {
            if(cut_after >= 0 && static_cast<int>(i) >= cut_after)
                return false; // the cable before this satellite is cut
            SimSat& s = sats[i];
            if(!s.alive)
                return false;
            // Last if nothing is plugged in downstream (PRESENT high), or forced.
            bool natural = i + 1 == sats.size();
            s.me.last    = natural || s.forced;
            std::vector<uint8_t> out(SAT_MAX_FRAME_LEN + SAT_SLOT_LEN);
            sat_slot             taken[4];
            bool                 took = false;
            uint8_t              pos  = 0;
            size_t len = sat_satellite_pass(frame.data(), frame.size(), s.me, s.inputs, taken,
                                            &took, &pos, out.data());
            if(!len)
                return false;
            out.resize(len);
            frame = out;
            s.frames++;
            if(took)
                s.led = taken[0];
            if(frame[SAT_H_TYPE] == SAT_COMMAND)
                for(int k = 0; k < frame[SAT_H_N]; k++)
                {
                    sat_slot c = sat_slot_read(frame.data() + SAT_HEADER_LEN + SAT_SLOT_LEN * k);
                    if(sat_command_for(c, pos))
                    {
                        if(c.kind == SAT_CMD_CLOSE_RING_HERE)
                            s.forced = true;
                        if(c.kind == SAT_CMD_RING_AUTO)
                            s.forced = false;
                    }
                }
            if(s.me.last)
                break; // turned back along DATA_RET
        }
        reply = frame;
        return true;
    }
};

SimSat Sat(uint8_t kind, uint8_t major = 1)
{
    SimSat s;
    s.me.kind     = kind;
    s.me.slots    = sat_kind_slots(kind);
    s.me.last     = false;
    s.me.fw_major = major;
    s.me.fw_minor = 0;
    for(auto& in : s.inputs)
        in = sat_slot_make(sat_kind_input(kind), 0, 0);
    return s;
}

// Drive the engine against the ring until `until` (us), as the Seed's main loop would.
struct Bench
{
    SatChain chain;
    Ring     ring;
    uint32_t now = 0;
    int      exchanges = 0;

    Bench() { chain.Init(SatChain::Config(), now); }

    void RunUntil(uint32_t until)
    {
        while(now < until)
        {
            SatChain::Exchange ex;
            if(!chain.Poll(now, ex))
            {
                now += 100;
                continue;
            }
            exchanges++;
            std::vector<uint8_t> reply;
            bool back = ring.Pass(ex.tx, ex.tx_len, reply);
            // What a UART expecting `rx_len` bytes (or N from the header) would hold.
            size_t want = ex.rx_len ? ex.rx_len
                                    : (reply.size() >= SAT_HEADER_LEN
                                           ? SAT_FRAME_LEN(reply[SAT_H_N])
                                           : SAT_MAX_FRAME_LEN);
            if(back && reply.size() >= want)
            {
                now += sat_ring_time_us(ex.rx_len ? (ex.rx_len - 4) / 4 : reply[SAT_H_N], 4);
                chain.Reply(reply.data(), want, now);
            }
            else
            {
                now += ex.timeout_us; // a short reply, or none: wait out the timeout
                chain.Reply(reply.data(), back ? reply.size() : 0, now);
            }
        }
    }
    void RunFor(uint32_t us) { RunUntil(now + us); }
};
} // namespace

TEST(SatChain, WaitsForTheSatellitesToBootThenDiscovers)
{
    Bench b;
    b.ring.sats = {Sat(SAT_KIND_POT_BANK_4), Sat(SAT_KIND_ENC_BANK_4)};
    b.RunUntil(49000);
    EXPECT_EQ(b.exchanges, 0); // §6.1: nothing for 50 ms
    EXPECT_EQ(b.chain.GetState(), SatChain::State::kPowerWait);
    b.RunFor(5000);
    EXPECT_EQ(b.chain.GetState(), SatChain::State::kScan);
    ASSERT_EQ(b.chain.Satellites(), 2);
    EXPECT_EQ(b.chain.Satellite(0).kind, SAT_KIND_POT_BANK_4);
    EXPECT_EQ(b.chain.Satellite(1).slots, 4);
    EXPECT_TRUE(b.chain.Changed());
    EXPECT_FALSE(b.chain.Changed()); // once
}

TEST(SatChain, ScansEveryFourMilliseconds)
{
    Bench b;
    b.ring.sats = {Sat(SAT_KIND_POT_BANK_4)};
    b.RunUntil(100000);
    uint32_t before = b.chain.GoodScans();
    b.RunFor(400000);
    EXPECT_NEAR(static_cast<int>(b.chain.GoodScans() - before), 100, 2); // 250 a second
    EXPECT_EQ(b.chain.BadScans(), 0u);
}

TEST(SatChain, ReadsPotsEncodersAndButtons)
{
    Bench b;
    b.ring.sats = {Sat(SAT_KIND_POT_BANK_4), Sat(SAT_KIND_ENC_BANK_4), Sat(SAT_KIND_BUTTON)};
    b.ring.sats[0].inputs[2] = sat_pot(32768);
    b.ring.sats[1].inputs[0] = sat_encoder(250, false, 0);
    b.ring.sats[2].inputs[0] = sat_button(false, 9);
    b.RunUntil(100000);

    EXPECT_NEAR(b.chain.Pot(0, 2), 0.5f, 0.001f);
    EXPECT_EQ(b.chain.EncoderSteps(1, 0), 0); // first sight: nothing has moved yet
    EXPECT_EQ(b.chain.Presses(2, 0), 0);

    b.ring.sats[1].inputs[0] = sat_encoder(3, true, 2); // +9 detents, through the wrap
    b.ring.sats[2].inputs[0] = sat_button(true, 11);    // two presses
    b.RunFor(10000);
    EXPECT_EQ(b.chain.EncoderSteps(1, 0), 9);
    EXPECT_EQ(b.chain.EncoderSteps(1, 0), 0); // consumed
    EXPECT_TRUE(b.chain.Pressed(1, 0));
    EXPECT_EQ(b.chain.Presses(1, 0), 2);
    EXPECT_TRUE(b.chain.Pressed(2, 0));
    EXPECT_EQ(b.chain.Presses(2, 0), 2);
    EXPECT_EQ(b.chain.Pot(5, 0), 0.0f); // no such satellite: harmless
}

TEST(SatChain, SendsEachSatelliteItsLed)
{
    Bench b;
    b.ring.sats = {Sat(SAT_KIND_POT), Sat(SAT_KIND_POT_BANK_4)};
    b.chain.SetLed(1, 200, SAT_LED_SLOW_BLINK);
    b.RunUntil(100000);
    EXPECT_EQ(b.ring.sats[1].led.kind, SAT_OUT_LED);
    EXPECT_EQ(b.ring.sats[1].led.d0, 200);
    EXPECT_EQ(b.ring.sats[1].led.d1, SAT_LED_SLOW_BLINK);
    EXPECT_EQ(b.ring.sats[0].led.d0, 0);
}

TEST(SatChain, EmptyRingIsAZeroSatelliteChain)
{
    // The bench test: the Seed's TX wired straight to its RX.
    Bench b;
    b.RunUntil(100000);
    EXPECT_EQ(b.chain.GetState(), SatChain::State::kScan);
    EXPECT_EQ(b.chain.Satellites(), 0);
    EXPECT_GT(b.chain.GoodScans(), 10u);
}

TEST(SatChain, ASatelliteAddedLiveIsFound)
{
    Bench b;
    b.ring.sats = {Sat(SAT_KIND_POT)};
    b.RunUntil(100000);
    b.chain.Changed();
    b.ring.sats.push_back(Sat(SAT_KIND_ENC_BANK_4)); // plugged in while running
    b.RunFor(200000);
    EXPECT_EQ(b.chain.GetState(), SatChain::State::kScan);
    EXPECT_EQ(b.chain.Satellites(), 2);
    EXPECT_TRUE(b.chain.Changed());
}

TEST(SatChain, FindsABreakAndKeepsScanningBeforeIt)
{
    Bench b;
    for(int i = 0; i < 5; i++)
        b.ring.sats.push_back(Sat(SAT_KIND_POT));
    b.ring.sats[0].inputs[0] = sat_pot(65535);
    b.RunUntil(100000);
    ASSERT_EQ(b.chain.Satellites(), 5);

    b.ring.sats[3].alive = false; // satellite 4 dies
    b.RunFor(500000);
    EXPECT_EQ(b.chain.GetState(), SatChain::State::kPartial);
    EXPECT_EQ(b.chain.BrokenAfter(), 3);
    EXPECT_TRUE(b.ring.sats[2].forced); // the ring is closed at satellite 3
    EXPECT_FALSE(b.ring.sats[0].forced || b.ring.sats[1].forced);
    EXPECT_NEAR(b.chain.Pot(0, 0), 1.0f, 0.001f); // still playable

    uint32_t scans = b.chain.GoodScans();
    b.RunFor(100000);
    EXPECT_GT(b.chain.GoodScans(), scans + 15); // and still scanning

    b.ring.sats[3].alive = true; // fixed
    b.RunFor(3000000);           // the next retry finds the whole ring
    EXPECT_EQ(b.chain.GetState(), SatChain::State::kScan);
    EXPECT_EQ(b.chain.Satellites(), 5);
    EXPECT_FALSE(b.ring.sats[2].forced);
}

TEST(SatChain, ABreakBeforeTheFirstSatelliteLeavesNothing)
{
    Bench b;
    b.ring.sats = {Sat(SAT_KIND_POT), Sat(SAT_KIND_POT)};
    b.RunUntil(100000);
    b.ring.cut_after = 0; // the motherboard's cable is out
    b.RunFor(500000);
    EXPECT_EQ(b.chain.GetState(), SatChain::State::kPartial);
    EXPECT_EQ(b.chain.BrokenAfter(), 0);
    EXPECT_EQ(b.chain.Satellites(), 0);
    b.ring.cut_after = -1;
    b.RunFor(3000000);
    EXPECT_EQ(b.chain.Satellites(), 2);
}

TEST(SatChain, RefusesASatelliteOnAnotherMajorVersion)
{
    Bench b;
    b.ring.sats = {Sat(SAT_KIND_POT), Sat(SAT_KIND_POT, 2)};
    b.RunUntil(100000);
    EXPECT_EQ(b.chain.GetState(), SatChain::State::kVersion);
    EXPECT_EQ(b.chain.GoodScans(), 0u); // never scans a chain it might misread
}

TEST(SatChain, AnOddBadScanChangesNothing)
{
    Bench b;
    b.ring.sats = {Sat(SAT_KIND_POT)};
    b.RunUntil(100000);
    b.ring.sats[0].alive = false;
    b.RunFor(9000); // two scans lost: under the three that mean a lost ring
    b.ring.sats[0].alive = true;
    b.RunFor(100000);
    EXPECT_EQ(b.chain.GetState(), SatChain::State::kScan);
    EXPECT_GE(b.chain.BadScans(), 1u);
}

TEST(SatChain, ClockWrapIsHarmless)
{
    Bench b;
    b.now = 0xFFFFFFFFu - 60000; // 60 ms before the microsecond counter wraps
    b.chain.Init(SatChain::Config(), b.now);
    b.ring.sats = {Sat(SAT_KIND_POT)};
    uint32_t start = b.now;
    while(b.now - start < 200000)
    {
        SatChain::Exchange ex;
        if(b.chain.Poll(b.now, ex))
        {
            std::vector<uint8_t> r;
            b.ring.Pass(ex.tx, ex.tx_len, r);
            b.now += 300;
            b.chain.Reply(r.data(), r.size(), b.now);
        }
        else
            b.now += 100;
    }
    EXPECT_EQ(b.chain.GetState(), SatChain::State::kScan);
    EXPECT_GT(b.chain.GoodScans(), 30u);
}
