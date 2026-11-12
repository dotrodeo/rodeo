// The host protocol (host.h): requests in as USB-MIDI packets, replies out as
// packets, reassembled here and checked byte for byte.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "host.h"

using rodeo::Host;
using rodeo::HostParam;

namespace
{
const char* const kShapes[] = {"saw", "square", "fm"};

const HostParam kParams[] = {
    {0, "cutoff", 0, 1000, 600, "\\u2030", nullptr, 1},
    {1, "shape", 0, 2, 0, "", kShapes, 0},
    {5, "octave", -2, 2, 0, "", nullptr, 0},
    {7, "sound", 0, 9, 0, "", nullptr, 0}, // max depends on the card: Max()
};

struct App : rodeo::HostApp
{
    int      v[8]  = {600, 0, 0, 0, 0, 0, 0, 0};
    int      sounds = 3;
    uint32_t held   = 0;
    int      app_cmds = 0;
    bool     take_cmd = true;

    int  Get(const HostParam& p) override { return v[p.id]; }
    void Set(const HostParam& p, int x) override { v[p.id] = x; }
    int  Max(const HostParam& p) override { return p.id == 7 ? sounds - 1 : p.max; }
    uint32_t Held() override { return held; }
    void Hello(rodeo::HostWriter& w) override { w.Add(",\"voices\":8"); }
    void State(rodeo::HostWriter& w) override { w.Add(",\"preset\":2"); }
    bool Command(uint8_t cmd, const uint8_t* data, size_t len, Host& host) override
    {
        app_cmds++;
        if(!take_cmd)
            return false;
        host.Begin(cmd | Host::kReply).Add("{\"len\":%d,\"first\":%d}", (int)len,
                                           len ? data[0] : -1);
        host.End();
        return true;
    }
};

struct Reply
{
    int         cmd;
    std::string json;
};

struct Bench
{
    App      app;
    Host     host;
    uint8_t  mem[Host::kMemory];
    uint32_t now = 1000;

    explicit Bench(size_t mem_size = Host::kMemory, const rodeo::SatChain* chain = nullptr)
    {
        Host::Config c;
        c.firmware    = "test-fw";
        c.build       = "abc123";
        c.params      = kParams;
        c.count       = sizeof kParams / sizeof kParams[0];
        c.app         = &app;
        c.chain       = chain;
        c.memory      = mem;
        c.memory_size = mem_size;
        host.Init(c);
        host.Connected(true);
    }

    // A request, as the computer's USB-MIDI driver packs it
    void Send(int cmd, std::vector<uint8_t> payload = {}, uint8_t version = 2)
    {
        std::vector<uint8_t> m(std::begin(rodeo::Host::kHeader), std::end(rodeo::Host::kHeader));
        m.push_back(version);
        m.push_back((uint8_t)cmd);
        m.insert(m.end(), payload.begin(), payload.end());
        m.push_back(0xF7);
        for(size_t i = 0; i < m.size(); i += 3)
        {
            size_t  k      = m.size() - i < 3 ? m.size() - i : 3;
            bool    end    = i + k == m.size();
            uint8_t pk[4]  = {(uint8_t)(end ? 0x4 + k : 0x4), m[i], 0, 0};
            for(size_t j = 1; j < k; j++)
                pk[1 + j] = m[i + j];
            EXPECT_TRUE(host.Receive(pk, now));
        }
    }

    // Everything queued, as whole messages; every packet's CIN checked
    std::vector<Reply> Take()
    {
        std::vector<Reply>   out;
        std::vector<uint8_t> m;
        uint8_t              pk[4];
        while(host.Peek(pk))
        {
            host.Pop();
            uint8_t cin = pk[0];
            EXPECT_GE(cin, 0x4);
            EXPECT_LE(cin, 0x7);
            size_t k = cin == 0x4 ? 3 : cin - 0x4;
            for(size_t j = 0; j < k; j++)
            {
                EXPECT_FALSE(cin == 0x4 && pk[1 + j] == 0xF7) << "F7 inside a CIN 4 packet";
                m.push_back(pk[1 + j]);
            }
            if(cin != 0x4)
            {
                EXPECT_EQ(m.back(), 0xF7);
                const size_t h = sizeof rodeo::Host::kHeader;
                std::vector<uint8_t> head(std::begin(rodeo::Host::kHeader),
                                          std::end(rodeo::Host::kHeader));
                head.push_back(2);
                EXPECT_EQ(std::vector<uint8_t>(m.begin(), m.begin() + h + 1), head);
                for(size_t j = 1; j + 1 < m.size(); j++)
                    EXPECT_LT(m[j], 0x80) << "SysEx data is 7-bit";
                out.push_back({m[h + 1], std::string(m.begin() + h + 2, m.end() - 1)});
                m.clear();
            }
        }
        EXPECT_TRUE(m.empty()) << "half a message";
        return out;
    }

    void Run(uint32_t ms)
    {
        for(uint32_t t = 0; t < ms; t += 10)
        {
            now += 10;
            host.Task(now, 12);
        }
    }
};

bool Has(const std::string& s, const std::string& part)
{
    return s.find(part) != std::string::npos;
}
} // namespace

TEST(Host, HelloDescribesTheFirmwareAndEveryParameter)
{
    Bench b;
    b.Send(Host::kHello);
    auto r = b.Take();
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].cmd, Host::kHello | Host::kReply);
    const std::string& j = r[0].json;
    EXPECT_TRUE(Has(j, "\"fw\":\"test-fw\",\"build\":\"abc123\",\"proto\":2,")) << j;
    EXPECT_TRUE(Has(j, "{\"id\":0,\"key\":\"cutoff\",\"min\":0,\"max\":1000,\"def\":600,"
                       "\"unit\":\"\\u2030\",\"pot\":1}"))
        << j;
    EXPECT_TRUE(Has(j, "\"names\":[\"saw\",\"square\",\"fm\"]")) << j;
    EXPECT_TRUE(Has(j, "{\"id\":7,\"key\":\"sound\",\"min\":0,\"max\":2,")) << "Max() wins: " << j;
    EXPECT_TRUE(Has(j, "\"chain\":0")) << j;
    EXPECT_EQ(j.substr(j.size() - 13), "],\"voices\":8}") << "the app's fields last: " << j;
}

TEST(Host, SetTakesFourteenBitsAboveTheMinimum)
{
    Bench b;
    b.Send(Host::kSet, {0, 700 & 0x7F, 700 >> 7, 5, 1, 0}); // cutoff 700, octave -2 + 1
    EXPECT_EQ(b.app.v[0], 700);
    EXPECT_EQ(b.app.v[5], -1);
    b.Send(Host::kSet, {7, 9, 0}); // sound 9, but the card has 3
    EXPECT_EQ(b.app.v[7], 2);
    EXPECT_TRUE(b.Take().empty()) << "a set has no reply";
}

TEST(Host, ProblemsAreSaidNotSwallowed)
{
    Bench b;
    b.Send(Host::kSet, {99, 1, 0, 0, 5, 0});
    auto r = b.Take();
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].cmd, Host::kError);
    EXPECT_EQ(r[0].json, "{\"error\":\"no parameter 99\"}");
    EXPECT_EQ(b.app.v[0], 5) << "the rest of the set still applies";

    b.Send(Host::kHello, {}, 1);
    b.Send(Host::kHello, {}, 1);
    r = b.Take();
    ASSERT_EQ(r.size(), 1u) << "a wrong version is said once";
    EXPECT_TRUE(Has(r[0].json, "protocol 1; this Rodeo speaks 2"));

    b.Send(Host::kChain);
    r = b.Take();
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].json, "{\"error\":\"no satellite chain\"}");
}

TEST(Host, TheFirmwaresOwnCommandsGoToIt)
{
    Bench b;
    b.Send(0x25, {9, 1});
    auto r = b.Take();
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].cmd, 0x65);
    EXPECT_EQ(r[0].json, "{\"len\":2,\"first\":9}");

    b.app.take_cmd = false;
    b.Send(0x26);
    b.Send(0x10); // the SDK's range: never the firmware's
    EXPECT_EQ(b.app.app_cmds, 2);
    r = b.Take();
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0].json, "{\"error\":\"unknown command 0x26\"}");
    EXPECT_EQ(r[1].json, "{\"error\":\"unknown command 0x10\"}");
}

TEST(Host, StateOnceAndStreamedWhileItChanges)
{
    Bench b;
    b.app.held = 5;
    b.Send(Host::kState);
    b.Run(10);
    auto r = b.Take();
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].cmd, Host::kState | Host::kReply);
    EXPECT_EQ(r[0].json,
              "{\"p\":{\"cutoff\":600,\"shape\":0,\"octave\":0,\"sound\":0},\"hold\":5,"
              "\"preset\":2,\"cpu\":12}");

    b.Send(Host::kStream, {1});
    b.Run(10);
    EXPECT_EQ(b.Take().size(), 1u) << "straight away";
    b.Run(2000);
    EXPECT_TRUE(b.Take().empty()) << "nothing changes, nothing is sent";

    b.app.v[0] = 800;
    b.Run(200);
    r = b.Take();
    ASSERT_GE(r.size(), 1u);
    EXPECT_TRUE(Has(r[0].json, "\"cutoff\":800"));
    EXPECT_LE(r.size(), 5u) << "at most every 50 ms";

    b.Run(5000); // no keep-alive
    b.Take();
    b.app.v[0] = 100;
    b.Run(200);
    EXPECT_TRUE(b.Take().empty()) << "the stream lapses";
    EXPECT_FALSE(b.host.Streaming());
}

TEST(Host, SaidOnlyWhileStreaming)
{
    Bench b;
    b.host.Say("cutoff: 41 %");
    EXPECT_TRUE(b.Take().empty());
    b.Send(Host::kStream, {1});
    b.Take();
    b.host.Say("shape: \"fm\"");
    auto r = b.Take();
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].cmd, 0x43);
    EXPECT_EQ(r[0].json, "{\"said\":\"shape: fm\"}");
}

TEST(Host, NobodyThereNothingQueued)
{
    Bench b;
    b.host.Connected(false);
    b.Send(Host::kHello);
    EXPECT_TRUE(b.Take().empty());
    EXPECT_EQ(b.host.Dropped(), 1u);

    b.host.Connected(true);
    b.Send(Host::kStream, {1});
    b.host.Connected(false);
    EXPECT_FALSE(b.host.Streaming()) << "a stream ends with the computer";
}

TEST(Host, AFullQueueDropsWholeMessages)
{
    Bench b(600); // 200 for the message, 400 for the queue
    for(int i = 0; i < 10; i++)
        b.Send(Host::kState), b.Run(10);
    auto r = b.Take(); // Take() fails on any half message
    EXPECT_GT(r.size(), 0u);
    EXPECT_LT(r.size(), 10u);
    EXPECT_GT(b.host.Dropped(), 0u);
    for(auto& x : r)
        EXPECT_EQ(x.json.back(), '}');
}

TEST(Host, TooLongForTheBufferIsAnError)
{
    Bench b(240); // 80 for the message
    b.Send(Host::kHello);
    auto r = b.Take();
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].cmd, Host::kError);
    EXPECT_EQ(r[0].json, "{\"error\":\"reply 0x41 too long\"}");
}

TEST(Host, DfuWaitsForItsReplyToGo)
{
    Bench b;
    b.Send(Host::kDfu);
    EXPECT_FALSE(b.host.DfuRequested()) << "the reply is still queued";
    auto r = b.Take();
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].cmd, Host::kDfu | Host::kReply);
    EXPECT_TRUE(b.host.DfuRequested());
    EXPECT_FALSE(b.host.DfuRequested()) << "once";
}

TEST(Host, OnlySysExIsTheHosts)
{
    Bench b;
    const uint8_t note[4] = {0x09, 0x90, 60, 100};
    const uint8_t cc[4]   = {0x0B, 0xB0, 74, 64};
    const uint8_t tune[4] = {0x05, 0xF6, 0, 0}; // CIN 5, but a Tune Request
    EXPECT_FALSE(b.host.Receive(note, b.now));
    EXPECT_FALSE(b.host.Receive(cc, b.now));
    EXPECT_FALSE(b.host.Receive(tune, b.now));
    // Someone else's SysEx is swallowed, and not answered
    const uint8_t other[2][4] = {{0x04, 0xF0, 0x43, 0x10}, {0x06, 0x01, 0xF7, 0}};
    EXPECT_TRUE(b.host.Receive(other[0], b.now));
    EXPECT_TRUE(b.host.Receive(other[1], b.now));
    EXPECT_TRUE(b.Take().empty());
}

TEST(Host, ChainReportBeforeAnySatelliteAnswers)
{
    rodeo::SatChain chain;
    chain.Init(rodeo::SatChain::Config(), 0);
    Bench b(Host::kMemory, &chain);
    b.Send(Host::kChain);
    b.Run(10);
    auto r = b.Take();
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].cmd, Host::kChain | Host::kReply);
    EXPECT_EQ(r[0].json, "{\"state\":\"powerwait\",\"sats\":0,\"broken\":-1,\"s\":[],"
                         "\"good\":0,\"bad\":0}");

    b.Send(Host::kStream, {3});
    b.Run(10);
    r = b.Take();
    ASSERT_EQ(r.size(), 2u) << "state and chain, straight away";
    b.Run(3000);
    r = b.Take();
    int chains = 0;
    for(auto& x : r)
        chains += x.cmd == (Host::kChain | Host::kReply);
    EXPECT_GE(chains, 2) << "the chain at least once a second, for the scan counts";
    EXPECT_LE(chains, 4);
}
