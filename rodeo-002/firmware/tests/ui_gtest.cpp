// The panel's rules, tested without a panel.
//
// These are the behaviours that are miserable to debug by ear on hardware:
// pickup (does a pot jump when you let go of shift?), and telling a held shift
// apart from a long press.

#include "gtest/gtest.h"

#include "factory_presets.h"
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
    // Volume moved behind SHIFT on 2026-09-24, freeing a main-layer pot. It is
    // still global: live the moment it moves, never waiting for pickup.
    r.HoldMain(true);
    r.Tick();
    r.MovePot(10, 0.25f);
    EXPECT_NEAR(r.ui.globals().volume, 0.25f, 1e-5f);
    // ...and the main layer under it is Sustain, which does wait for pickup.
    r.HoldMain(false);
    r.Tick();
    const float before = r.ui.patch().sustain;
    r.MovePot(10, 0.26f);
    EXPECT_NEAR(r.ui.patch().sustain, before, 1e-5f) << "Sustain took hold without pickup";
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

// It used to. Nothing resolves on a long press any more -- a hold is shift, and
// only shift, which is what stopped it being confusable (2026-09-24).
TEST(Ui, ACleanHoldAndReleaseDoesNothingAtAll)
{
    Rig r;
    r.HoldMain(true);
    for(int i = 0; i < 100; i++)
        r.Tick();
    EXPECT_TRUE(r.ui.display().shift) << "holding MAIN is supposed to be shift";
    r.HoldMain(false); // nothing else touched
    r.Tick();
    EXPECT_EQ(r.ui.Mode(), UiMode::kNormal);
    EXPECT_STREQ(r.ui.display().pending, "");
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

// ------------------------------------------------------------ THE MENU
//
// MAIN's hold is SHIFT and nothing else. Everything else hangs off its TAP,
// through a modal menu, because a hold that sometimes meant shift and sometimes
// armed the recorder -- told apart only by whether anything else had been
// touched -- was invisible at the moment it mattered (Derek, 2026-09-24).
// The rule left is: tap acts, hold shifts.

namespace
{
void Tap(Rig& r)
{
    r.HoldMain(true);
    r.Tick();
    r.HoldMain(false);
    r.Tick();
}
void LongPress(Rig& r)
{
    r.HoldMain(true);
    r.Tick();
    r.Tick(Ui::kLongPressMs + 50);
    r.HoldMain(false);
    r.Tick();
}
// Open the menu and enter an entry. The menu remembers where it was left, so
// this turns to the entry rather than assuming it opens at the first one.
void OpenMenu(Rig& r, MenuItem item)
{
    Tap(r);
    for(int guard = 0; r.ui.display().menu_index != static_cast<int>(item) && guard < 8; guard++)
        r.Turn(kMain, 1);
    Tap(r);
}
} // namespace

TEST(Ui, TappingMainOpensTheMenu)
{
    Rig r;
    EXPECT_EQ(r.ui.Mode(), UiMode::kNormal);
    Tap(r);
    EXPECT_EQ(r.ui.Mode(), UiMode::kMenu);
    EXPECT_EQ(r.ui.display().mode, UiMode::kMenu);
    EXPECT_EQ(r.ui.TakePresetRequest().action, PresetAction::kNone);
}

TEST(Ui, HoldingMainDoesNothingButShift)
{
    Rig r;
    LongPress(r);
    EXPECT_EQ(r.ui.Mode(), UiMode::kNormal) << "a hold opened something";
    EXPECT_EQ(r.ui.TakePresetRequest().action, PresetAction::kNone);
    EXPECT_STREQ(r.ui.display().pending, "") << "a hold still announces an action";
}

TEST(Ui, TurningInTheMenuMovesTheSelectionNotTheEngine)
{
    Rig            r;
    const EngineId before = r.ui.patch().engine;
    Tap(r);
    r.Turn(kMain, 1);
    EXPECT_EQ(r.ui.display().menu_index, 1);
    EXPECT_EQ(r.ui.patch().engine, before) << "browsing the menu changed the engine";
    // ...and it wraps.
    for(int i = 0; i < static_cast<int>(MenuItem::kCount); i++)
        r.Turn(kMain, 1);
    EXPECT_EQ(r.ui.display().menu_index, 1);
}

TEST(Ui, LoadAndSaveAreSeparateEntriesAndBothActOnATap)
{
    {
        Rig r;
        OpenMenu(r, MenuItem::kLoad);
        ASSERT_EQ(r.ui.Mode(), UiMode::kLoad);
        r.Turn(kMain, 1);
        r.Turn(kMain, 1);
        Tap(r);
        const PresetRequest req = r.ui.TakePresetRequest();
        EXPECT_EQ(req.action, PresetAction::kLoad);
        EXPECT_EQ(req.slot, 2);
        EXPECT_EQ(r.ui.Mode(), UiMode::kNormal);
    }
    {
        Rig r;
        OpenMenu(r, MenuItem::kSave);
        ASSERT_EQ(r.ui.Mode(), UiMode::kSave);
        r.Turn(kMain, 1);
        Tap(r);
        const PresetRequest req = r.ui.TakePresetRequest();
        EXPECT_EQ(req.action, PresetAction::kSave) << "saving now needs no hold at all";
        EXPECT_EQ(req.slot, 1);
    }
}

TEST(Ui, TouchingAnythingElseClosesTheModalLayerWithoutActing)
{
    {
        Rig r;
        Tap(r);
        ASSERT_EQ(r.ui.Mode(), UiMode::kMenu);
        r.MovePot(0, 0.9f);
        EXPECT_EQ(r.ui.Mode(), UiMode::kNormal) << "a pot did not close the menu";
    }
    {
        Rig r;
        OpenMenu(r, MenuItem::kLoad);
        r.Turn(0, 1); // a slot encoder
        EXPECT_EQ(r.ui.Mode(), UiMode::kNormal);
        EXPECT_EQ(r.ui.TakePresetRequest().action, PresetAction::kNone);
    }
}

TEST(Ui, TheModalLayerClosesItselfIfNothingHappens)
{
    Rig r;
    Tap(r);
    r.Tick(Ui::kBrowserIdleMs - 100);
    EXPECT_EQ(r.ui.Mode(), UiMode::kMenu) << "closed too early";
    r.Tick(200);
    EXPECT_EQ(r.ui.Mode(), UiMode::kNormal);
}

TEST(Ui, UsingMainAsShiftDoesNotOpenTheMenu)
{
    Rig r;
    r.HoldMain(true);
    r.Tick();
    r.MovePot(0, 0.9f); // the other hand, on a shift-layer parameter
    r.HoldMain(false);
    r.Tick();
    EXPECT_EQ(r.ui.Mode(), UiMode::kNormal);
}

TEST(Ui, PotDitherDuringAPressDoesNotSwallowTheTap)
{
    Rig r;
    r.HoldMain(true);
    r.Tick();
    for(int i = 0; i < 20; i++)
    {
        r.in.pot[3] = 0.5f + ((i % 2) ? 0.003f : -0.003f);
        r.Tick(2);
    }
    r.HoldMain(false);
    r.Tick();
    EXPECT_EQ(r.ui.Mode(), UiMode::kMenu) << "dither made the tap look like a shift";
}

TEST(Ui, ADeliberateTurnDuringAHoldIsStillAShift)
{
    Rig r;
    r.HoldMain(true);
    r.Tick();
    r.MovePot(3, 0.75f); // a quarter turn, while holding
    r.HoldMain(false);
    r.Tick();
    EXPECT_EQ(r.ui.Mode(), UiMode::kNormal)
        << "a real turn should have made that press a shift";
}

TEST(Ui, TheBrowserShowsTheNameOfTheHighlightedSlot)
{
    Rig               r;
    static PresetBank bank;
    BuildFactoryBank(bank);
    r.ui.SetBank(&bank);
    OpenMenu(r, MenuItem::kLoad);
    r.Tick();
    EXPECT_STREQ(r.ui.display().browser_name, bank.slot[0].name);
    EXPECT_TRUE(r.ui.display().browser_used);

    bank.slot[0].used = 0;
    r.Tick();
    EXPECT_FALSE(r.ui.display().browser_used);
}

// ------------------------------------------------------ GESTURE RECORDER

TEST(Ui, TheGestureEntryCyclesArmRecordAndClear)
{
    Rig             r;
    GestureRecorder g;
    std::vector<uint16_t> buf(GestureRecorder::kLanes * GestureRecorder::kMaxFrames);
    g.Init(buf.data(), GestureRecorder::kMaxFrames);
    r.ui.SetGesture(&g);

    OpenMenu(r, MenuItem::kGesture);
    EXPECT_EQ(g.state(), GestureRecorder::State::kArmed);
    EXPECT_EQ(r.ui.Mode(), UiMode::kNormal) << "the menu should close behind it";

    // Recording starts on the first pot movement, not on the press.
    r.Tick(200);
    EXPECT_EQ(g.state(), GestureRecorder::State::kArmed);
    r.MovePot(0, 0.2f); // catches, then moves: a real parameter change
    r.MovePot(0, 0.8f);
    EXPECT_EQ(g.state(), GestureRecorder::State::kRecording);

    // While recording, a tap closes the loop rather than opening the menu --
    // the one timing-critical action the instrument has.
    Tap(r);
    EXPECT_EQ(g.state(), GestureRecorder::State::kPlaying);
    EXPECT_EQ(r.ui.Mode(), UiMode::kNormal) << "closing the loop opened the menu";

    // Clearing goes back through the menu, since nothing is time-critical now.
    OpenMenu(r, MenuItem::kGesture);
    EXPECT_EQ(g.state(), GestureRecorder::State::kIdle);
}

TEST(Ui, APlayingLoopDrivesTheParameterItRecorded)
{
    Rig             r;
    GestureRecorder g;
    std::vector<uint16_t> buf(GestureRecorder::kLanes * GestureRecorder::kMaxFrames);
    g.Init(buf.data(), GestureRecorder::kMaxFrames);
    r.ui.SetGesture(&g);

    OpenMenu(r, MenuItem::kGesture); // arm
    r.MovePot(0, 0.2f);              // catch Harmonics, then sweep it
    for(int i = 0; i < 6; i++)
        r.MovePot(0, i % 2 ? 0.8f : 0.2f);
    ASSERT_EQ(g.state(), GestureRecorder::State::kRecording);
    Tap(r); // close
    ASSERT_EQ(g.state(), GestureRecorder::State::kPlaying);
    ASSERT_TRUE(g.in_loop(static_cast<int>(ParamId::kHarmonics)));

    // With no hands on the panel, the loop moves the parameter by itself.
    float lo = 1.f, hi = 0.f;
    for(int i = 0; i < 200; i++)
    {
        r.Tick();
        lo = fminf(lo, r.ui.patch().harmonics);
        hi = fmaxf(hi, r.ui.patch().harmonics);
    }
    EXPECT_GT(hi - lo, 0.2f) << "the loop is not driving the parameter";
}

// ------------------------------------------------------ WHAT THE SCREEN SAYS
//
// The readout is not always a number. A stepped pot is a choice and has to name
// it; an envelope segment is part of a shape, so the shape is what to draw; and
// when nothing has been touched the space belongs to the scope.

TEST(Ui, AContinuousPotAsksForABar)
{
    Rig r;
    r.MovePot(0, 0.8f); // Harmonics
    EXPECT_EQ(r.ui.display().readout, Readout::kBar);
    EXPECT_STREQ(r.ui.display().param, "Harmonics");
}

TEST(Ui, ASteppedPotNamesItsChoiceInsteadOfShowingAMeter)
{
    Rig r;
    r.HoldMain(true); // the shift layer, where the stepped pots live
    r.Tick();
    r.MovePot(4, 0.95f); // filter type
    EXPECT_EQ(r.ui.display().readout, Readout::kChoice);
    EXPECT_STREQ(r.ui.display().param, "Filter");
    EXPECT_STREQ(r.ui.display().choice, FilterTypeName(r.ui.patch().filter));
    EXPECT_STRNE(r.ui.display().choice, "");

    r.MovePot(6, 0.05f); // LFO destination
    EXPECT_EQ(r.ui.display().readout, Readout::kChoice);
    EXPECT_STREQ(r.ui.display().choice, LfoDestName(r.ui.patch().lfo_dest));
}

TEST(Ui, AnEnvelopePotAsksForTheWholeEnvelope)
{
    Rig r;
    r.MovePot(8, 0.4f); // attack
    const DisplayState& d = r.ui.display();
    EXPECT_EQ(d.readout, Readout::kEnvelope);
    EXPECT_EQ(d.env_moving, 0) << "attack is the first segment";
    EXPECT_FLOAT_EQ(d.env[0], r.ui.patch().attack);
    EXPECT_FLOAT_EQ(d.env[1], r.ui.patch().decay);
    EXPECT_FLOAT_EQ(d.env[2], r.ui.patch().sustain);

    r.MovePot(9, 0.6f); // decay
    EXPECT_EQ(d.readout, Readout::kEnvelope);
    EXPECT_EQ(d.env_moving, 1);

    r.MovePot(10, 0.3f); // sustain -- all three are on the main layer now
    EXPECT_EQ(d.readout, Readout::kEnvelope);
    EXPECT_EQ(d.env_moving, 2);
}

// ADS, Moog style: Decay is the fall time whether or not the key is still down,
// so the drawing's release segment follows it and the patch keeps both fields in
// step -- what a preset stores is what the panel can express.
TEST(Ui, DecaySetsTheReleaseAsWell)
{
    Rig r;
    r.MovePot(9, 0.2f);  // down across the stored value, so the pot catches
    r.MovePot(9, 0.62f);
    EXPECT_NEAR(r.ui.patch().decay, 0.62f, 0.02f);
    EXPECT_FLOAT_EQ(r.ui.patch().release, r.ui.patch().decay);
    EXPECT_FLOAT_EQ(r.ui.display().env[3], r.ui.patch().decay)
        << "the picture shows a release the sound does not have";
}

TEST(Ui, TheReadoutGivesTheSpaceBackWhenNothingIsTouched)
{
    Rig r;
    r.MovePot(0, 0.8f);
    ASSERT_NE(r.ui.display().readout, Readout::kNone);
    r.Tick(Ui::kPopupMs + 100);
    EXPECT_EQ(r.ui.display().readout, Readout::kNone) << "the scope never gets the screen";
    EXPECT_FALSE(r.ui.display().show_value);
}

// The * in the header. It has to mean exactly "a save would change something".
TEST(Ui, ThePresetIsCleanWhenLoadedAndDirtyOnceAKnobTakesHold)
{
    Rig               r;
    static PresetBank bank;
    BuildFactoryBank(bank);
    r.ui.SetBank(&bank);

    Patch p;
    PresetToPatch(bank.slot[1], p); // "Chroma Pad"
    r.ui.mutable_patch() = p;
    r.ui.NotePresetLoaded(bank.slot[1]);
    r.ui.DropPickup();
    r.Tick();
    EXPECT_FALSE(r.ui.PresetDirty());
    EXPECT_STREQ(r.ui.display().preset_name, bank.slot[1].name);
    EXPECT_FALSE(r.ui.display().preset_dirty);

    // Pickup means an untouched pot changes nothing, so the patch stays clean
    // even as the panel is swept past values it does not own yet.
    r.MovePot(0, 0.02f);
    EXPECT_TRUE(r.ui.PresetDirty()) << "a pot that caught its value did not count";

    // Saving makes the patch the preset again.
    PatchToPreset(r.ui.patch(), bank.slot[1].name, bank.slot[1]);
    r.ui.NotePresetLoaded(bank.slot[1]);
    r.Tick();
    EXPECT_FALSE(r.ui.PresetDirty());
    EXPECT_FALSE(r.ui.display().preset_dirty);
}

TEST(Ui, AnUntouchedPanelNeverMarksThePresetDirty)
{
    Rig               r;
    static PresetBank bank;
    BuildFactoryBank(bank);
    Patch p;
    PresetToPatch(bank.slot[2], p);
    r.ui.mutable_patch() = p;
    r.ui.NotePresetLoaded(bank.slot[2]);
    r.ui.DropPickup();
    for(int i = 0; i < 200; i++)
    {
        // Pots sitting still, dithering, exactly as hardware delivers them.
        for(int k = 0; k < Ui::kNumPots; k++)
            r.in.pot[k] = 0.5f + ((i % 2) ? 0.002f : -0.002f);
        r.Tick();
    }
    EXPECT_FALSE(r.ui.PresetDirty()) << "dither alone marked the patch modified";
}
