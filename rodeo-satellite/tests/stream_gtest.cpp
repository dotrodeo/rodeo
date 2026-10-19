// The byte-at-a-time streamer against sat_satellite_pass(), the reference model.
//
// For every frame shape, a satellite that streams must send exactly what the
// model says, and must send it without a gap: once it starts, one byte for every
// byte that arrives, then everything it still owes when the frame ends.
#include <random>
#include <vector>

#include "gtest/gtest.h"
#include "stream.h"

namespace
{
sat_descriptor Me(uint8_t kind, bool last = false)
{
    sat_descriptor d;
    d.kind     = kind;
    d.slots    = sat_kind_slots(kind);
    d.last     = last;
    d.fw_major = 1;
    d.fw_minor = 0;
    return d;
}

struct Streamed
{
    std::vector<uint8_t> out;
    std::vector<int>     per_byte; // bytes sent after each byte received
    sat_stream           st;
};

Streamed Stream(const std::vector<uint8_t>& in, sat_descriptor me, const sat_slot* inputs)
{
    Streamed r;
    stream_init(&r.st, me);
    std::vector<uint8_t> buf(SAT_MAX_FRAME_LEN + SAT_SLOT_LEN);
    for(uint8_t b : in)
    {
        size_t n = stream_rx(&r.st, b, inputs, buf.data());
        r.out.insert(r.out.end(), buf.begin(), buf.begin() + n);
        r.per_byte.push_back(static_cast<int>(n));
    }
    return r;
}

std::vector<uint8_t> Reference(const std::vector<uint8_t>& in, sat_descriptor me,
                               const sat_slot* inputs, sat_slot* taken, bool* took)
{
    std::vector<uint8_t> out(SAT_MAX_FRAME_LEN + SAT_SLOT_LEN);
    out.resize(sat_satellite_pass(in.data(), in.size(), me, inputs, taken, took, nullptr,
                                  out.data()));
    return out;
}

std::vector<uint8_t> Frame(uint8_t type, uint8_t n, uint8_t hop, std::mt19937& rng)
{
    std::vector<uint8_t> f(SAT_FRAME_LEN(n));
    sat_header_write(f.data(), type, n, hop);
    for(uint8_t i = 0; i < n; i++)
        sat_slot_write(f.data() + SAT_HEADER_LEN + SAT_SLOT_LEN * i,
                       sat_slot_make(static_cast<uint8_t>(rng()), static_cast<uint8_t>(rng()),
                                     static_cast<uint8_t>(rng())));
    return f;
}
} // namespace

TEST(Stream, MatchesTheReferenceForEveryShape)
{
    std::mt19937 rng(1234);
    sat_slot     inputs[4] = {sat_pot(1), sat_pot(2), sat_pot(3), sat_pot(4)};
    for(uint8_t kind : {SAT_KIND_POT, SAT_KIND_POT_BANK_4, SAT_KIND_ENC_BANK_4})
        for(uint8_t type : {SAT_SCAN, SAT_DISCOVER, SAT_COMMAND})
            for(int n : {0, 1, 2, 3, 4, 5, 17, 63, 64})
            {
                if(type == SAT_DISCOVER && n == 64)
                    continue; // a DISCOVER can't grow past 64
                auto in = Frame(type, static_cast<uint8_t>(n), 3, rng);
                sat_descriptor me = Me(kind);
                sat_slot       want_taken[4];
                bool           want_took;
                auto           want = Reference(in, me, inputs, want_taken, &want_took);
                auto           got  = Stream(in, me, inputs);

                SCOPED_TRACE(::testing::Message() << "kind " << int(kind) << " type "
                                                  << int(type) << " n " << n);
                EXPECT_EQ(got.out, want);
                EXPECT_TRUE(stream_done(&got.st));
                EXPECT_EQ(got.st.took, want_took);
                if(want_took)
                    for(int i = 0; i < me.slots; i++)
                        EXPECT_EQ(got.st.taken[i].d0, want_taken[i].d0);
                EXPECT_EQ(got.st.position, 4);
            }
}

TEST(Stream, OutputNeverGapsOnceStarted)
{
    std::mt19937 rng(99);
    sat_slot     inputs[4] = {sat_pot(1), sat_pot(2), sat_pot(3), sat_pot(4)};
    for(uint8_t kind : {SAT_KIND_POT, SAT_KIND_POT_BANK_4})
        for(int n : {4, 8, 30})
        {
            auto           in  = Frame(SAT_SCAN, static_cast<uint8_t>(n), 0, rng);
            sat_descriptor me  = Me(kind);
            auto           got = Stream(in, me, inputs);
            uint8_t        lag = sat_lag_bytes(SAT_SCAN, static_cast<uint8_t>(n), me.slots);
            // Nothing until the lag is reached, then exactly one per byte in, and
            // the rest when the last byte arrives.
            for(size_t i = 0; i + 1 < got.per_byte.size(); i++)
                EXPECT_EQ(got.per_byte[i], i + 1 >= lag ? 1 : 0) << "after byte " << i;
            EXPECT_EQ(got.per_byte.back(), lag);
        }
}

TEST(Stream, DropsABadHeaderAndSendsNothing)
{
    std::mt19937 rng(7);
    auto         in = Frame(SAT_SCAN, 4, 0, rng);
    in[SAT_H_TYPE]  = 0x09;
    sat_slot inputs[1] = {sat_pot(5)};
    auto     got       = Stream(in, Me(SAT_KIND_POT), inputs);
    EXPECT_TRUE(got.out.empty());
    EXPECT_FALSE(stream_done(&got.st));
}

TEST(Stream, WaitsForAFrameToStartAndForTheIdleGapBetweenFrames)
{
    std::mt19937 rng(8);
    sat_slot     inputs[1] = {sat_pot(5)};
    auto         in        = Frame(SAT_COMMAND, 1, 0, rng);

    std::vector<uint8_t> noise = {0x00, 0x13, 0xFF};
    noise.insert(noise.end(), in.begin(), in.end());
    auto got = Stream(noise, Me(SAT_KIND_POT), inputs);
    EXPECT_EQ(got.out, Reference(in, Me(SAT_KIND_POT), inputs, nullptr, nullptr));

    // A second frame needs the idle gap's reset first (contract §2.2).
    std::vector<uint8_t> buf(SAT_MAX_FRAME_LEN);
    EXPECT_EQ(stream_rx(&got.st, SAT_SOF, inputs, buf.data()), 0u);
    stream_reset(&got.st);
    size_t sent = 0;
    for(uint8_t b : in)
        sent += stream_rx(&got.st, b, inputs, buf.data());
    EXPECT_EQ(sent, in.size());
}

TEST(Stream, AChainOfStreamersEqualsAChainOfReferences)
{
    std::mt19937                rng(5);
    std::vector<sat_descriptor> chain = {Me(SAT_KIND_POT), Me(SAT_KIND_ENC_BANK_4),
                                         Me(SAT_KIND_POT_BANK_4), Me(SAT_KIND_BUTTON, true)};
    sat_slot inputs[4] = {sat_encoder(1, false, 0), sat_encoder(2, true, 1), sat_pot(3), sat_pot(4)};
    for(uint8_t type : {SAT_DISCOVER, SAT_SCAN})
    {
        uint8_t n  = type == SAT_SCAN ? 10 : 0; // 1 + 4 + 4 + 1 slots
        auto    a  = Frame(type, n, 0, rng);
        auto    b  = a;
        for(auto& me : chain)
        {
            a = Stream(a, me, inputs).out;
            b = Reference(b, me, inputs, nullptr, nullptr);
        }
        EXPECT_EQ(a, b) << "type " << int(type);
        EXPECT_EQ(a[SAT_H_HOP], 4);
    }
}

TEST(Stream, KeepsTheCommandsAddressedToIt)
{
    // Three commands: one for position 2, one for everyone, one for position 5.
    sat_slot cmds[3] = {sat_command(SAT_CMD_IDENTIFY, 2, 4), sat_command(SAT_CMD_RING_AUTO, 0, 0),
                        sat_command(SAT_CMD_CLOSE_RING_HERE, 5, 0)};
    std::vector<uint8_t> f(SAT_FRAME_LEN(3));
    sat_build_command(f.data(), cmds, 3);
    f[SAT_H_HOP] = 1; // so this satellite is at position 2
    sat_slot inputs[1] = {sat_pot(0)};
    auto     got       = Stream(f, Me(SAT_KIND_POT), inputs);
    ASSERT_EQ(got.st.n_cmds, 2);
    EXPECT_EQ(got.st.cmds[0].kind, SAT_CMD_IDENTIFY);
    EXPECT_EQ(got.st.cmds[0].d1, 4);
    EXPECT_EQ(got.st.cmds[1].kind, SAT_CMD_RING_AUTO);
    EXPECT_EQ(got.out, Reference(f, Me(SAT_KIND_POT), inputs, nullptr, nullptr));
}
