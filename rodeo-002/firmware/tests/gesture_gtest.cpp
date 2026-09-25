// The gesture recorder: what it records, what it gives back, and what it must
// leave alone.

#include "gtest/gtest.h"

#include <cmath>
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
