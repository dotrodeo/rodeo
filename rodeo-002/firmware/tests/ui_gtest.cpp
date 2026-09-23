// The panel's rules, tested without a panel.
//
// These are the behaviours that are miserable to debug by ear on hardware:
// pickup (does a pot jump when you let go of shift?), and telling a held shift
// apart from a long press.

#include "gtest/gtest.h"

#include "ui.h"

using namespace rodeo002;

namespace
{
// Drives the Ui the way the app does: a pot array, encoder deltas, a clock.
struct Rig
{
    Ui      ui;
    UiInput in;
    VoiceManager voices;

    Rig()
    {
        Patch   p;
        Globals g;
        ui.Init(p, g);
        voices.Init(48000.f);
        for(int i = 0; i < Ui::kNumPots; i++)
            in.pot[i] = 0.5f;
        Tick(); // settle: the first pass primes the pot history
    }
    void Tick(uint32_t ms = 10)
    {
        in.now_ms += ms;
        ui.Update(in);
        for(int e = 0; e < 5; e++)
            in.enc_delta[e] = 0;
    }
    void MovePot(int pot, float to, int steps = 8)
    {
        const float from = in.pot[pot];
        for(int s = 1; s <= steps; s++)
        {
            in.pot[pot] = from + (to - from) * (static_cast<float>(s) / steps);
            Tick();
        }
    }
    void Turn(int enc, int d)
    {
        in.enc_delta[enc] = d;
        Tick();
    }
    void HoldMain(bool held) { in.enc_held[Ui::kMainEncoder] = held; }
};

constexpr int kMain = Ui::kMainEncoder;
} // namespace

TEST(Ui, PotTakesControlOnlyAfterItCatchesTheStoredValue)
{
    Rig r;
    // Pot 4 is Cutoff; the patch default is 0.8 and the pot sits at 0.5.
    const float stored = r.ui.patch().cutoff;
    ASSERT_GT(stored, 0.55f);

    r.MovePot(4, 0.6f); // moving, but not yet past 0.8
    EXPECT_FLOAT_EQ(r.ui.patch().cutoff, stored) << "pot must not take over before catching";
    EXPECT_EQ(r.ui.display().pickup_hint, 1) << "screen should say: turn up";

    r.MovePot(4, 0.9f); // crosses 0.8
    EXPECT_NEAR(r.ui.patch().cutoff, 0.9f, 1e-5f);
    EXPECT_EQ(r.ui.display().pickup_hint, 0);

    r.MovePot(4, 0.3f); // now it follows
    EXPECT_NEAR(r.ui.patch().cutoff, 0.3f, 1e-5f);
}

TEST(Ui, ShiftSwapsTheLayerAndPickupProtectsBothSides)
{
    Rig r;
    r.MovePot(0, 0.5f);
    r.MovePot(0, 0.2f); // catch and set Harmonics
    ASSERT_NEAR(r.ui.patch().harmonics, 0.2f, 1e-5f);

    // Hold shift: pot 0 now addresses Env>Timbre (stored 0.5) and must be
    // caught again before it moves anything.
    r.HoldMain(true);
    r.Tick();
    const float env_before = r.ui.patch().env_to_timbre;
    r.MovePot(0, 0.3f);
    EXPECT_FLOAT_EQ(r.ui.patch().env_to_timbre, env_before);
    EXPECT_NEAR(r.ui.patch().harmonics, 0.2f, 1e-5f) << "the main layer must not move";

    r.MovePot(0, 0.7f); // crosses 0.5
    EXPECT_NEAR(r.ui.patch().env_to_timbre, 0.7f, 1e-5f);

    // Release shift: the pot is at 0.7 but Harmonics is 0.2 -- it must not jump.
    r.HoldMain(false);
    r.Tick();
    r.MovePot(0, 0.6f);
    EXPECT_NEAR(r.ui.patch().harmonics, 0.2f, 1e-5f) << "no jump on shift release";
}

TEST(Ui, MasterVolumeIsAlwaysLive)
{
    Rig r;
    // Pot 10 is Volume: global, never picked up.
    r.MovePot(10, 0.25f);
    EXPECT_NEAR(r.ui.globals().volume, 0.25f, 1e-5f);
}

TEST(Ui, MainEncoderSelectsEngineAndPushTurnSelectsVoiceMode)
{
    Rig r;
    const EngineId first = r.ui.patch().engine;
    r.Turn(kMain, 1);
    EXPECT_NE(r.ui.patch().engine, first);

    r.HoldMain(true);
    r.Tick();
    const VoiceMode vm = r.ui.patch().voice_mode;
    r.Turn(kMain, 1); // push-turn
    EXPECT_NE(r.ui.patch().voice_mode, vm);
    r.HoldMain(false);
    r.Tick();
}

TEST(Ui, HoldingShiftDoesNotFireMainsLongPress)
{
    Rig r;
    r.HoldMain(true);
    for(int i = 0; i < 100; i++) // a full second of holding
        r.Tick();
    r.MovePot(2, 0.9f);          // reach for a pot while holding
    r.HoldMain(false);
    r.Tick();
    EXPECT_STREQ(r.ui.display().message, "") << "a shift must not resolve as a press";
}

TEST(Ui, ACleanHoldAndReleaseResolvesAsALongPress)
{
    Rig r;
    r.HoldMain(true);
    for(int i = 0; i < 100; i++)
        r.Tick();
    r.HoldMain(false); // nothing else touched
    r.Tick();
    EXPECT_STRNE(r.ui.display().message, "");
}

TEST(Ui, SlotEncoderTurnsPickTypesAndTapsBypass)
{
    Rig r;
    const int slot = fx::TextureChain::kMovement;
    const int t0   = r.ui.patch().type[slot];
    r.Turn(slot, 1);
    EXPECT_NE(r.ui.patch().type[slot], t0);

    EXPECT_FALSE(r.ui.patch().bypass[slot]);
    r.in.enc_held[slot] = true;
    r.Tick(50); // a tap, well under the long-press threshold
    r.in.enc_held[slot] = false;
    r.Tick();
    EXPECT_TRUE(r.ui.patch().bypass[slot]);
}

TEST(Ui, HoldingTheSpaceEncoderFreezes)
{
    Rig r;
    const int slot = fx::TextureChain::kSpace;
    EXPECT_FALSE(r.ui.patch().freeze);
    r.in.enc_held[slot] = true;
    for(int i = 0; i < 80; i++) // past 600 ms
        r.Tick();
    EXPECT_TRUE(r.ui.patch().freeze) << "long-press on SPACE is Freeze";
    r.in.enc_held[slot] = false;
    r.Tick();
    EXPECT_FALSE(r.ui.patch().bypass[slot]) << "a long press must not also bypass";
}

TEST(Ui, ShiftTurnPicksTheSlotOption)
{
    Rig r;
    const int slot = fx::TextureChain::kSpace;
    const int o0   = r.ui.patch().option[slot];
    r.HoldMain(true);
    r.Tick();
    r.Turn(slot, 1);
    EXPECT_NE(r.ui.patch().option[slot], o0);
    EXPECT_EQ(r.ui.patch().type[slot], r.ui.patch().type[slot]); // type unchanged
}

TEST(Ui, SteppedPotsLandOnWholeChoices)
{
    Rig r;
    r.HoldMain(true);
    r.Tick();
    // Pot 4's shift position is Filter type (3 choices).
    r.MovePot(4, 0.f);
    r.MovePot(4, 1.f);
    EXPECT_EQ(static_cast<int>(r.ui.patch().filter), static_cast<int>(FilterType::kCount) - 1);
    r.MovePot(4, 0.f);
    EXPECT_EQ(static_cast<int>(r.ui.patch().filter), 0);
}

TEST(Ui, DisplayReportsEngineVoicesAndSlots)
{
    Rig r;
    r.voices.SetMaxVoices(6);
    r.ui.Refresh(r.voices);
    const DisplayState& d = r.ui.display();
    EXPECT_STREQ(d.engine, EngineName(r.ui.patch().engine));
    EXPECT_EQ(d.voices_max, 6);
    EXPECT_STREQ(d.slot_type[fx::TextureChain::kSpace],
                 fx::TextureChain::TypeName(fx::TextureChain::kSpace,
                                            r.ui.patch().type[fx::TextureChain::kSpace]));
}
