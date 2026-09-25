// The gesture recorder: what it records, what it gives back, and what it must
// leave alone.

#include "gtest/gtest.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "gesture.h"

using namespace rodeo002;

namespace
{
struct Rig
{
    std::vector<uint16_t> buf;
    GestureRecorder       g;
    float                 v[GestureRecorder::kLanes]     = {};
    bool                  moved[GestureRecorder::kLanes] = {};

    Rig() : buf(GestureRecorder::kLanes * GestureRecorder::kMaxFrames)
    {
        g.Init(buf.data(), GestureRecorder::kMaxFrames);
        for(int i = 0; i < GestureRecorder::kLanes; i++)
            v[i] = 0.5f;
    }
    void Tick(int frames = 1)
    {
        for(int i = 0; i < frames; i++)
            g.Tick(v, moved);
        for(int i = 0; i < GestureRecorder::kLanes; i++)
            moved[i] = false;
    }
    // Sweep one lane over `frames`, marking it as moved by the player.
    void Sweep(int lane, float from, float to, int frames)
    {
        for(int i = 0; i < frames; i++)
        {
            v[lane]     = from + (to - from) * (static_cast<float>(i) / (frames - 1));
            moved[lane] = true;
            g.Tick(v, moved);
            moved[lane] = false;
        }
    }
};
} // namespace

TEST(Gesture, ArmingWaitsForTheFirstMovement)
{
    Rig r;
    r.g.Arm();
    EXPECT_EQ(r.g.state(), GestureRecorder::State::kArmed);
    r.Tick(50); // nothing touched: still waiting, no rush
    EXPECT_EQ(r.g.state(), GestureRecorder::State::kArmed);
    EXPECT_EQ(r.g.length_frames(), 0u);

    r.Sweep(3, 0.5f, 0.6f, 10);
    EXPECT_EQ(r.g.state(), GestureRecorder::State::kRecording);
}

TEST(Gesture, APlayedBackLoopReproducesWhatWasRecorded)
{
    Rig r;
    r.g.Arm();
    r.Sweep(3, 0.f, 1.f, 100); // one second of a slow sweep
    r.g.Close();
    ASSERT_EQ(r.g.state(), GestureRecorder::State::kPlaying);
    ASSERT_EQ(r.g.length_frames(), 100u);

    // Play it back with the parameter parked somewhere else entirely.
    r.v[3] = 0.f;
    std::vector<float> seen;
    for(int i = 0; i < 100; i++)
    {
        r.Tick();
        seen.push_back(r.v[3]);
    }
    EXPECT_NEAR(seen.front(), 0.f, 0.01f);
    EXPECT_NEAR(seen.back(), 1.f, 0.01f);
    EXPECT_NEAR(seen[50], 0.5f, 0.02f);
    // ...and it loops rather than stopping at the end.
    r.Tick();
    EXPECT_NEAR(r.v[3], 0.f, 0.01f);
}

// A take that touched one knob owns that one knob. If playback wrote all 32
// lanes, loading a preset under a running loop would be undone by the loop.
TEST(Gesture, OnlyTheLanesThatMovedArePlayedBack)
{
    Rig r;
    r.g.Arm();
    r.Sweep(3, 0.f, 1.f, 60);
    r.g.Close();
    EXPECT_TRUE(r.g.in_loop(3));
    EXPECT_FALSE(r.g.in_loop(4));

    r.v[4] = 0.123f; // something else set it -- a preset load, say
    r.Tick(30);
    EXPECT_FLOAT_EQ(r.v[4], 0.123f) << "the loop wrote a lane it never recorded";
}

// Grab a pot the loop is driving and it is yours for the rest of the playback.
TEST(Gesture, GrabbingAPotTakesItBackFromTheLoop)
{
    Rig r;
    r.g.Arm();
    r.Sweep(3, 0.f, 1.f, 60);
    r.g.Close();
    r.Tick(10);
    ASSERT_NE(r.v[3], 0.25f); // the loop is driving it

    r.v[3]     = 0.25f; // the player grabs that pot
    r.moved[3] = true;
    r.Tick();
    for(int i = 0; i < 40; i++) // and keeps it
    {
        r.Tick();
        EXPECT_FLOAT_EQ(r.v[3], 0.25f) << "the loop took the pot back at frame " << i;
    }
    EXPECT_TRUE(r.g.grabbed(3));
}

// The press that closes a loop is timestamped when it goes DOWN but resolves on
// release, so the gap between them has to come off the end.
TEST(Gesture, ClosingTrimsTheEndToThePressItself)
{
    Rig r;
    r.g.Arm();
    r.Sweep(3, 0.f, 1.f, 100);
    r.g.Close(20); // the press went down 20 frames ago
    EXPECT_EQ(r.g.length_frames(), 80u);
}

TEST(Gesture, TheOneControlCyclesArmCloseClear)
{
    Rig r;
    r.g.Advance();
    EXPECT_EQ(r.g.state(), GestureRecorder::State::kArmed);
    r.Sweep(3, 0.f, 0.5f, 20);
    EXPECT_EQ(r.g.state(), GestureRecorder::State::kRecording);
    r.g.Advance();
    EXPECT_EQ(r.g.state(), GestureRecorder::State::kPlaying);
    r.g.Advance();
    EXPECT_EQ(r.g.state(), GestureRecorder::State::kIdle);
    EXPECT_EQ(r.g.length_frames(), 0u);

    // Armed but never played: the control cancels rather than recording nothing.
    r.g.Advance();
    ASSERT_EQ(r.g.state(), GestureRecorder::State::kArmed);
    r.g.Advance();
    EXPECT_EQ(r.g.state(), GestureRecorder::State::kIdle);
}

// Sixteen seconds is the limit. It has to close itself rather than run off the
// end of the buffer or drop the take.
TEST(Gesture, ARecordingThatRunsLongClosesItself)
{
    Rig r;
    r.g.Arm();
    r.Sweep(3, 0.f, 1.f, 10);
    for(size_t i = 0; i < GestureRecorder::kMaxFrames + 50; i++)
        r.Tick();
    EXPECT_EQ(r.g.state(), GestureRecorder::State::kPlaying);
    EXPECT_LE(r.g.length_frames(), GestureRecorder::kMaxFrames + 0);
    EXPECT_GT(r.g.length_frames(), 0u);
}

TEST(Gesture, ClearingStopsItDrivingAnything)
{
    Rig r;
    r.g.Arm();
    r.Sweep(3, 0.f, 1.f, 60);
    r.g.Close();
    r.g.Clear();
    EXPECT_EQ(r.g.state(), GestureRecorder::State::kIdle);
    r.v[3] = 0.42f;
    r.Tick(30);
    EXPECT_FLOAT_EQ(r.v[3], 0.42f);
    EXPECT_FALSE(r.g.in_loop(3));
}

// ------------------------------------------------- STORED WITH A PRESET
//
// A take is ~100 KB, three orders of magnitude bigger than a preset, so it does
// not live in the bank: each slot has its own region and the region's own
// header answers whether that preset has a gesture. That is why gestures cost
// no change to the preset format and no saved preset.

#include "gesture_store.h"

TEST(GestureStore, ATakeFitsItsSlotAndTheSlotsDoNotOverlap)
{
    EXPECT_LE(GestureBytes(GestureRecorder::kMaxFrames), GestureSlotSize());
    for(int i = 1; i < 16; i++)
        EXPECT_EQ(GestureOffsetForSlot(i) - GestureOffsetForSlot(i - 1), GestureSlotSize())
            << "slot " << i << " overlaps its neighbour";
    // Clear of the program below and of the three stores up at the top.
    EXPECT_GE(GestureOffsetForSlot(0), 0x070000u) << "would land on the program image";
    EXPECT_LE(GestureOffsetForSlot(15) + GestureSlotSize(), 0x780000u)
        << "would land on the panel wiring map";
}

TEST(GestureStore, OnlyTheFramesRecordedAreWorthWriting)
{
    // A four-second take is a quarter of the space, and the erase is sized to
    // match -- erasing 128 KB stalls the panel for the best part of a second.
    EXPECT_LT(GestureBytes(400), GestureBytes(GestureRecorder::kMaxFrames) / 3);
    EXPECT_EQ(GestureBytes(0), sizeof(GestureHeader));
}

TEST(GestureStore, AnUnwrittenOrForeignRegionIsNotAGesture)
{
    GestureHeader h{};
    h.magic = 0xFFFFFFFFu; // erased flash
    EXPECT_FALSE(h.HasGesture());

    h.magic     = GestureHeader::kMagic;
    h.version   = GestureHeader::kVersion;
    h.lanes     = GestureRecorder::kLanes;
    h.frames    = 0; // saved with no gesture: clears whatever was there
    h.lane_mask = 0;
    EXPECT_TRUE(h.Valid());
    EXPECT_FALSE(h.HasGesture());

    h.frames = GestureRecorder::kMaxFrames * 4; // nonsense length
    EXPECT_FALSE(h.Valid());
}

// The round trip a save-then-load makes: the app writes the buffer out and
// reads it back, and the recorder has to pick it up playing.
TEST(GestureStore, ATakeSurvivesTheRoundTripAndStartsLooping)
{
    Rig r;
    r.g.Arm();
    r.Sweep(3, 0.f, 1.f, 120);
    r.g.Close();
    const size_t   frames = r.g.length_frames();
    const uint32_t mask   = r.g.LaneMask();
    ASSERT_GT(frames, 0u);
    EXPECT_EQ(mask, 1u << 3);

    // "Flash": header plus exactly the bytes worth writing.
    std::vector<uint8_t> flash(GestureBytes(frames));
    GestureHeader        h{GestureHeader::kMagic, GestureHeader::kVersion,
                    static_cast<uint16_t>(GestureRecorder::kLanes),
                    static_cast<uint32_t>(frames), mask};
    std::memcpy(flash.data(), &h, sizeof(h));
    std::memcpy(flash.data() + sizeof(h), r.g.Data(),
                frames * GestureRecorder::kLanes * sizeof(uint16_t));

    // A fresh instrument, reading it back.
    Rig q;
    const GestureHeader* rh = reinterpret_cast<const GestureHeader*>(flash.data());
    ASSERT_TRUE(rh->HasGesture());
    std::memcpy(q.g.MutableData(), flash.data() + sizeof(GestureHeader),
                rh->frames * GestureRecorder::kLanes * sizeof(uint16_t));
    q.g.Adopt(rh->frames, rh->lane_mask);

    EXPECT_EQ(q.g.state(), GestureRecorder::State::kPlaying) << "a stored gesture should start";
    EXPECT_EQ(q.g.length_frames(), frames);
    EXPECT_TRUE(q.g.in_loop(3));
    EXPECT_FALSE(q.g.in_loop(4));

    // ...and it drives the same parameter through the same shape.
    float lo = 1.f, hi = 0.f;
    for(size_t i = 0; i < frames; i++)
    {
        q.Tick();
        lo = fminf(lo, q.v[3]);
        hi = fmaxf(hi, q.v[3]);
    }
    EXPECT_NEAR(lo, 0.f, 0.02f);
    EXPECT_NEAR(hi, 1.f, 0.02f);
}

TEST(GestureStore, AdoptingNonsenseLeavesNothingRunning)
{
    Rig r;
    r.g.Adopt(0, 0xFFFFFFFFu);
    EXPECT_EQ(r.g.state(), GestureRecorder::State::kIdle);
    r.g.Adopt(GestureRecorder::kMaxFrames * 2, 0xFFFFFFFFu);
    EXPECT_EQ(r.g.state(), GestureRecorder::State::kIdle);
}
