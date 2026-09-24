// Pot conditioning: the fixed travel of each wiring, mapped to 0..1.
//
// Correct wiring sweeps 0.00 -> ~3.00 V; a supply-swapped run (a straight cable
// on an analog run, which is how this panel was built) sweeps 0.30 -> 3.30 V.
// Both must give a control value that runs 0 -> 1 with the knob, WITHOUT
// flipping direction: swapping supply and ground changes the range, not which
// way the pot reads. Getting that wrong made every knob on the panel run
// backwards (2026-09-23).
//
// The ranges are FIXED, inset by a dead zone, rather than learned from what the
// channel has seen. Learning was tried and was worse on hardware (2026-09-24):
// a learned endpoint is only as good as the worst sample ever taken, so one
// reading from a settling ADC or a bad joint moved an end for good. Most of
// what follows is there to keep that from coming back.

#include "gtest/gtest.h"

#include "panel_pot.h"

using namespace rodeo002;

namespace
{
// Voltage a satellite produces at knob position 0..1.
float CorrectWiring(float pos, float rpot = 10000.f)
{
    const float top = 3.3f * rpot / (rpot + 1000.f); // ~3.00 V
    return pos * top;
}
float SupplySwapped(float pos, float rpot = 10000.f)
{
    const float bottom = 3.3f * 1000.f / (rpot + 1000.f); // ~0.30 V
    return bottom + pos * (3.3f - bottom);                // same direction
}

// Turn the knob fully up and leave it there, which is what makes a straight
// cable visible: nothing else can read above 3.15 V. The panel samples at
// ~108 Hz, so resting at a position is many readings, not one.
void ShowTheTop(PanelPots& p, int ch, float (*wiring)(float, float), float rpot = 10000.f)
{
    for(int i = 0; i < 10; i++)
        p.Normalize(ch, wiring(1.f, rpot));
}

// Turned fully DOWN: only a correctly wired run can reach 0 V.
void ShowTheBottom(PanelPots& p, int ch, float (*wiring)(float, float), float rpot = 10000.f)
{
    for(int i = 0; i < 10; i++)
        p.Normalize(ch, wiring(0.f, rpot));
}
} // namespace

TEST(PanelPots, CorrectWiringTracksTheKnob)
{
    PanelPots p;
    p.Init();
    ShowTheBottom(p, 0, CorrectWiring); // proves the wiring
    EXPECT_FALSE(p.IsSupplySwapped(0));
    EXPECT_TRUE(p.WiringKnown(0));
    EXPECT_NEAR(p.Normalize(0, CorrectWiring(0.5f)), 0.5f, 0.02f);
    EXPECT_LE(p.Normalize(0, CorrectWiring(0.f)), 0.f);
    EXPECT_GE(p.Normalize(0, CorrectWiring(1.f)), 1.f);
}

// THE 2026-09-24 PICKUP FAULT. Before either end has been reached the wiring is
// unproven -- and it used to be ASSUMED correct, which left a swapped pot
// reading 0.065 at its stop. A parameter sitting at 0 could then never be
// caught: the screen said DN for ever and the knob did nothing. Both wirings
// must reach a true 0 and 1 with nothing proven either way.
TEST(PanelPots, AnUnprovenRunStillReachesBothEnds)
{
    for(float rpot : {8000.f, 10000.f, 12000.f})
        for(int wiring = 0; wiring < 2; wiring++)
        {
            auto      w = wiring == 0 ? CorrectWiring : SupplySwapped;
            PanelPots p;
            p.Init();
            ASSERT_FALSE(p.WiringKnown(0));
            SCOPED_TRACE(testing::Message()
                         << (wiring ? "supply-swapped" : "correct") << " rpot " << rpot);
            EXPECT_FLOAT_EQ(p.Normalize(0, w(0.f, rpot)), 0.f)
                << "a parameter at 0 can never be picked up";
            EXPECT_FLOAT_EQ(p.Normalize(0, w(1.f, rpot)), 1.f)
                << "a parameter at 1 can never be picked up";
        }
}

// Reaching either stop proves the wiring, and the mapping then tightens.
TEST(PanelPots, EitherEndProvesTheWiring)
{
    PanelPots p;
    p.Init();
    ShowTheBottom(p, 0, CorrectWiring); // only correct wiring reaches 0 V
    EXPECT_TRUE(p.WiringKnown(0));
    EXPECT_FALSE(p.IsSupplySwapped(0));

    PanelPots q;
    q.Init();
    ShowTheTop(q, 0, SupplySwapped); // only a swapped run reaches 3.3 V
    EXPECT_TRUE(q.WiringKnown(0));
    EXPECT_TRUE(q.IsSupplySwapped(0));

    // Mid-travel proves nothing: both wirings can produce it.
    PanelPots r;
    r.Init();
    for(int i = 0; i < 50; i++)
        r.Normalize(0, 1.5f);
    EXPECT_FALSE(r.WiringKnown(0));
}

TEST(PanelPots, SupplySwappedIsDetectedAndCorrected)
{
    PanelPots p;
    p.Init();
    ShowTheTop(p, 1, SupplySwapped);
    EXPECT_TRUE(p.IsSupplySwapped(1)) << "3.30 V is impossible when wired correctly";
    // The knob, not the voltage, is what must run 0 -> 1.
    EXPECT_LE(p.Normalize(1, SupplySwapped(0.f)), 0.f);
    EXPECT_NEAR(p.Normalize(1, SupplySwapped(0.5f)), 0.5f, 0.02f);
    EXPECT_GE(p.Normalize(1, SupplySwapped(1.f)), 1.f);
}

// The one Derek asked for: every pot in tolerance reaches a true 0 and a true 1,
// on either wiring, with no calibration step of any kind.
TEST(PanelPots, EveryToleranceReachesBothEndsWithNoCalibration)
{
    for(float rpot : {8000.f, 10000.f, 12000.f}) // a 10k pot at +/-20%
    {
        PanelPots p;
        p.Init();
        ShowTheBottom(p, 0, CorrectWiring, rpot);
        ShowTheTop(p, 1, SupplySwapped, rpot);
        SCOPED_TRACE(testing::Message() << "rpot " << rpot);
        EXPECT_FLOAT_EQ(p.Normalize(0, CorrectWiring(0.f, rpot)), 0.f);
        EXPECT_FLOAT_EQ(p.Normalize(0, CorrectWiring(1.f, rpot)), 1.f);
        EXPECT_FLOAT_EQ(p.Normalize(1, SupplySwapped(0.f, rpot)), 0.f);
        EXPECT_FLOAT_EQ(p.Normalize(1, SupplySwapped(1.f, rpot)), 1.f);
    }
}

// What the dead zone costs, stated as a number so a change to it is visible:
// the knob must be off its stop by less than a tenth of its travel before the
// value moves, and the middle of the knob must still be the middle of the range.
TEST(PanelPots, TheDeadZoneIsSmallAndTheMiddleIsUnmoved)
{
    PanelPots p;
    p.Init();
    float leaves_zero = 1.f, reaches_one = 0.f;
    for(int i = 0; i <= 100; i++)
    {
        const float pos = static_cast<float>(i) / 100.f;
        const float n   = p.Normalize(0, CorrectWiring(pos));
        if(n > 0.f && pos < leaves_zero)
            leaves_zero = pos;
        if(n >= 1.f && pos < reaches_one)
            reaches_one = pos;
    }
    EXPECT_LT(leaves_zero, 0.1f) << "too much of the knob does nothing at the bottom";
    EXPECT_NEAR(p.Normalize(0, CorrectWiring(0.5f)), 0.5f, 0.02f);
}

// THE 2026-09-24 REGRESSION. A channel that once read something silly -- a
// settling ADC, a satellite being plugged in, a dry joint -- must map exactly as
// it did before. When the range was learned, that one reading moved the end of
// it for good and the pot could no longer reach zero.
TEST(PanelPots, AStraySampleDoesNotMoveTheRange)
{
    PanelPots clean, poisoned;
    clean.Init();
    poisoned.Init();
    ShowTheTop(clean, 0, SupplySwapped);
    ShowTheTop(poisoned, 0, SupplySwapped);
    poisoned.Normalize(0, SupplySwapped(0.5f)); // mid-travel, proving nothing

    poisoned.Normalize(0, 0.f);    // a dead reading, as a bad joint gives
    poisoned.Normalize(0, 0.007f); // and a nearly-dead one
    for(int i = 0; i <= 20; i++)
    {
        const float v = SupplySwapped(static_cast<float>(i) / 20.f);
        EXPECT_FLOAT_EQ(poisoned.Normalize(0, v), clean.Normalize(0, v))
            << "a stray sample changed the mapping, at " << v << " V";
    }
    EXPECT_FLOAT_EQ(poisoned.Normalize(0, SupplySwapped(0.f)), 0.f)
        << "the pot can no longer reach zero";
}

TEST(PanelPots, DirectionIsNeverFlipped)
{
    PanelPots p;
    p.Init();
    ShowTheBottom(p, 0, CorrectWiring);
    ShowTheTop(p, 1, SupplySwapped);
    for(int ch = 0; ch < 2; ch++)
    {
        auto  wiring = ch == 0 ? CorrectWiring : SupplySwapped;
        float last   = -1.f;
        for(int i = 0; i <= 10; i++)
        {
            const float n = p.Normalize(ch, wiring(static_cast<float>(i) / 10.f, 10000.f));
            EXPECT_GE(n, last) << "channel " << ch << " step " << i;
            last = n;
        }
    }
}

// The wiring verdict is permanent and stored, so a lone spike must not set it.
TEST(PanelPots, ALoneSpikeIsNotAWiringVerdict)
{
    PanelPots p;
    p.Init();
    for(int i = 0; i < 20; i++)
    {
        p.Normalize(0, CorrectWiring(0.9f));
        p.Normalize(0, 3.29f); // one bad sample among good ones
    }
    ShowTheBottom(p, 0, CorrectWiring);
    EXPECT_FALSE(p.IsSupplySwapped(0)) << "a spike between good readings set the wiring";
    EXPECT_NEAR(p.Normalize(0, CorrectWiring(1.f)), 1.f, 0.001f);
}

TEST(PanelPots, PotToleranceDoesNotFlipTheVerdict)
{
    PanelPots p;
    p.Init();
    // +20% pot, the highest a correct satellite can read: 3.3 * 12/13 = 3.046 V.
    ShowTheTop(p, 2, CorrectWiring, 12000.f);
    EXPECT_FALSE(p.IsSupplySwapped(2)) << "a +20% pot must not look like bad wiring";
    // -20% pot on a straight cable still reads well above the threshold.
    PanelPots q;
    q.Init();
    ShowTheTop(q, 3, SupplySwapped, 8000.f);
    EXPECT_TRUE(q.IsSupplySwapped(3));
}

TEST(PanelPots, WiringSurvivesAnExportAndImport)
{
    PanelPots p;
    p.Init();
    ShowTheTop(p, 1, SupplySwapped);
    ShowTheTop(p, 5, SupplySwapped);
    PanelPots::Calibration c;
    p.Export(c);

    PanelPots q; // a fresh boot
    q.Init();
    ASSERT_TRUE(q.Import(c));
    EXPECT_TRUE(q.IsSupplySwapped(1));
    EXPECT_TRUE(q.IsSupplySwapped(5));
    EXPECT_FALSE(q.IsSupplySwapped(0));
    // And the swapped channel is right from the first reading, with no turn.
    EXPECT_FLOAT_EQ(q.Normalize(1, SupplySwapped(0.f)), 0.f);
    EXPECT_FLOAT_EQ(q.Normalize(1, SupplySwapped(1.f)), 1.f);
}

TEST(PanelPots, ImportRejectsWhatIsNotOurs)
{
    PanelPots p;
    p.Init();
    PanelPots::Calibration c;
    p.Export(c);

    PanelPots::Calibration bad = c;
    bad.magic                  = 0xDEADBEEFu; // erased, or never written
    EXPECT_FALSE(p.Import(bad));
    bad         = c;
    bad.version = 2; // the version that treated unproven wiring as correct
    EXPECT_FALSE(p.Import(bad));
}

TEST(PanelPots, ForgetStartsAChannelOver)
{
    PanelPots p;
    p.Init();
    ShowTheTop(p, 0, SupplySwapped);
    ASSERT_TRUE(p.IsSupplySwapped(0));
    p.Forget(0); // a satellite or cable has been changed
    EXPECT_FALSE(p.IsSupplySwapped(0)) << "the wiring belongs to the run that left";
    EXPECT_FALSE(p.WiringKnown(0));
}

TEST(PanelPots, UnsweptChannelsStillProduceSomethingSensible)
{
    PanelPots p;
    p.Init();
    for(float v = 0.f; v <= 3.3f; v += 0.1f)
    {
        const float n = p.Normalize(4, v);
        EXPECT_GE(n, 0.f);
        EXPECT_LE(n, 1.f);
    }
}
