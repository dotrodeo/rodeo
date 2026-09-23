// Pot conditioning, including the analog runs built with straight cables --
// the state the panel arrived in on 2026-09-23.
//
// Correct wiring sweeps 0.00 -> ~3.00 V; a supply-swapped run sweeps
// 0.30 -> 3.30 V. Both must give a control value that runs 0 -> 1 with the
// knob, WITHOUT flipping direction: swapping supply and ground changes the
// range, not which way the pot reads. Getting that wrong made every knob on the
// panel run backwards.

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

// Sweep the knob end to end so the channel learns its travel.
void Sweep(PanelPots& p, int ch, float (*wiring)(float, float), float rpot = 10000.f)
{
    for(int i = 0; i <= 20; i++)
        p.Normalize(ch, wiring(static_cast<float>(i) / 20.f, rpot));
    for(int i = 20; i >= 0; i--)
        p.Normalize(ch, wiring(static_cast<float>(i) / 20.f, rpot));
}
} // namespace

TEST(PanelPots, CorrectWiringTracksTheKnob)
{
    PanelPots p;
    p.Init();
    Sweep(p, 0, CorrectWiring);
    EXPECT_FALSE(p.IsSupplySwapped(0));
    EXPECT_NEAR(p.Normalize(0, CorrectWiring(0.f)), 0.f, 0.02f);
    EXPECT_NEAR(p.Normalize(0, CorrectWiring(0.5f)), 0.5f, 0.05f);
    EXPECT_NEAR(p.Normalize(0, CorrectWiring(1.f)), 1.f, 0.02f);
}

TEST(PanelPots, SupplySwappedIsDetectedAndCorrected)
{
    PanelPots p;
    p.Init();
    Sweep(p, 1, SupplySwapped);
    EXPECT_TRUE(p.IsSupplySwapped(1)) << "3.30 V is impossible when wired correctly";
    // The knob, not the voltage, is what must run 0 -> 1.
    EXPECT_NEAR(p.Normalize(1, SupplySwapped(0.f)), 0.f, 0.02f);
    EXPECT_NEAR(p.Normalize(1, SupplySwapped(0.5f)), 0.5f, 0.05f);
    EXPECT_NEAR(p.Normalize(1, SupplySwapped(1.f)), 1.f, 0.02f);
}

TEST(PanelPots, BothEndsAreReachableWithEitherWiring)
{
    PanelPots p;
    p.Init();
    Sweep(p, 0, CorrectWiring);
    Sweep(p, 1, SupplySwapped);
    // The complaint that started this: pots that could not reach 0.
    EXPECT_LE(p.Normalize(0, CorrectWiring(0.f)), 0.001f);
    EXPECT_LE(p.Normalize(1, SupplySwapped(0.f)), 0.001f);
    EXPECT_GE(p.Normalize(0, CorrectWiring(1.f)), 0.999f);
    EXPECT_GE(p.Normalize(1, SupplySwapped(1.f)), 0.999f);
}

TEST(PanelPots, DirectionIsNeverFlipped)
{
    PanelPots p;
    p.Init();
    // Both wirings must RISE with the knob. The bug this pins made every pot on
    // the panel run backwards.
    for(int ch = 0; ch < 2; ch++)
    {
        auto wiring = ch == 0 ? CorrectWiring : SupplySwapped;
        Sweep(p, ch, wiring);
        float last = -1.f;
        for(int i = 0; i <= 10; i++)
        {
            const float n = p.Normalize(ch, wiring(static_cast<float>(i) / 10.f, 10000.f));
            EXPECT_GE(n, last) << "channel " << ch << " step " << i;
            last = n;
        }
    }
}

TEST(PanelPots, PotToleranceDoesNotFlipTheVerdict)
{
    PanelPots p;
    p.Init();
    // +20% pot, the highest a correct satellite can read: 3.3 * 12/13 = 3.046 V.
    Sweep(p, 2, CorrectWiring, 12000.f);
    EXPECT_FALSE(p.IsSupplySwapped(2)) << "a +20% pot must not look like bad wiring";
    EXPECT_NEAR(p.Normalize(2, CorrectWiring(1.f, 12000.f)), 1.f, 0.02f);
    // -20% pot on a straight cable still reads well above the threshold.
    PanelPots q;
    q.Init();
    Sweep(q, 3, SupplySwapped, 8000.f);
    EXPECT_TRUE(q.IsSupplySwapped(3));
}

TEST(PanelPots, UnsweptChannelsStillProduceSomethingSensible)
{
    PanelPots p;
    p.Init();
    // Before any sweep, the nominal travel is assumed -- no wild values.
    for(float v = 0.f; v <= 3.3f; v += 0.1f)
    {
        const float n = p.Normalize(4, v);
        EXPECT_GE(n, 0.f);
        EXPECT_LE(n, 1.f);
    }
}
