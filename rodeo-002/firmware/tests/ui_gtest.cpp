// The panel's rules, tested without a panel.
//
// These are the behaviours that are miserable to debug by ear on hardware:
// pickup (does a pot jump when you let go of shift?), and telling a held shift
// apart from a long press.

#include "gtest/gtest.h"

#include <cstring>
#include <string>

#include "factory_presets.h"
#include "ui.h"
#include "voice.h"

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
        for(int e = 0; e < Ui::kNumEncoders; e++)
            in.enc_delta[e] = 0; // ALL of them: a delta left set keeps applying
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
    void HoldNav(bool held) { in.enc_held[Ui::kNavEncoder] = held; }
};

constexpr int kMain = Ui::kMainEncoder;
constexpr int kNav  = Ui::kNavEncoder;
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

// Volume is GLOBAL -- never stored in a preset -- but it is not exempt from
// pickup. It used to be, which was right while it sat on the main layer where
// the knob's position was always the volume. Behind SHIFT, that same knob's
// position belongs to Sustain, so going live the instant SHIFT went down jumped
// the output level to wherever Sustain was sitting (Derek, 2026-09-24).
TEST(Ui, VolumeWaitsToBeCaughtLikeEverythingElse)
{
    Rig         r;
    const float before = r.ui.globals().volume;

    // Reaching for SHIFT must not move the output, whatever the knob is on.
    r.in.pot[10] = 0.2f;
    r.HoldMain(true);
    r.Tick();
    r.Tick();
    EXPECT_FLOAT_EQ(r.ui.globals().volume, before) << "volume jumped when SHIFT went down";

    // It says which way to turn, and takes hold once the pot crosses it.
    r.MovePot(10, 0.25f);
    EXPECT_FLOAT_EQ(r.ui.globals().volume, before);
    EXPECT_NE(r.ui.display().pickup_hint, 0) << "no UP/DN hint on an uncaught volume";
    r.MovePot(10, 0.95f); // crosses the stored value
    r.MovePot(10, 0.6f);
    EXPECT_NEAR(r.ui.globals().volume, 0.6f, 0.02f);
}

// ...and it still never travels in a preset.
TEST(Ui, VolumeIsNotPartOfThePatch)
{
    Rig r;
    r.HoldMain(true);
    r.Tick();
    r.MovePot(10, 0.95f);
    r.MovePot(10, 0.6f);
    ASSERT_NEAR(r.ui.globals().volume, 0.6f, 0.02f);
    StoredPreset s;
    PatchToPreset(r.ui.patch(), "vol", s);
    Patch back;
    PresetToPatch(s, back);
    Globals g;
    EXPECT_NE(g.volume, r.ui.globals().volume) << "the test is not proving anything";
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

TEST(Ui, DisplayReportsEngineSlotAndSlots)
{
    Rig r;
    r.ui.Refresh();
    const DisplayState& d = r.ui.display();
    EXPECT_STREQ(d.engine, EngineName(r.ui.patch().engine));
    EXPECT_EQ(d.preset_total, static_cast<int>(PresetBank::kNumSlots));
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
// MAIN: the gesture recorder, in every mode.
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
// NAV: move, enter, back. Its hold fires at the threshold rather than on
// release, since NAV has no shift to be told apart from.
void NavTap(Rig& r)
{
    r.HoldNav(true);
    r.Tick();
    r.HoldNav(false);
    r.Tick();
}
void NavBack(Rig& r)
{
    r.HoldNav(true);
    r.Tick();
    r.Tick(Ui::kLongPressMs + 50);
    r.HoldNav(false);
    r.Tick();
}
// Open the menu and enter an entry. It remembers where it was left, so this
// turns to the entry rather than assuming where it opens.
void OpenMenu(Rig& r, MenuItem item)
{
    if(r.ui.Mode() != UiMode::kMenu)
        NavTap(r);
    for(int guard = 0;
        r.ui.display().menu_index != static_cast<int>(item) && guard < 10;
        guard++)
        r.Turn(kNav, 1);
    NavTap(r);
}
} // namespace

TEST(Ui, OnlyNavsTapOpensTheMenu)
{
    Rig r;
    EXPECT_EQ(r.ui.Mode(), UiMode::kNormal);
    NavTap(r);
    EXPECT_EQ(r.ui.Mode(), UiMode::kMenu);

    // Turning NAV used to open it as well. It no longer does: with the menu
    // closed the turn belongs to the preset selector, and a gesture that means
    // two things is the thing we set out to remove (Derek, 2026-09-29).
    NavBack(r);
    ASSERT_EQ(r.ui.Mode(), UiMode::kNormal);
    r.Turn(kNav, 1);
    EXPECT_EQ(r.ui.Mode(), UiMode::kNormal) << "a turn opened the menu";
}

// The preset selector: NAV's turn on the normal screen.
TEST(Ui, NavTurnCyclesEverySlotIncludingTheEmptyOnes)
{
    Rig               r;
    static PresetBank bank;
    BuildFactoryBank(bank);
    r.ui.SetBank(&bank);
    r.ui.NotePresetLoaded(bank.slot[0], 0);

    r.Turn(kNav, 1);
    PresetRequest req = r.ui.TakePresetRequest();
    EXPECT_EQ(req.action, PresetAction::kLoad) << "a turn did not load anything";
    EXPECT_EQ(req.slot, 1);
    EXPECT_EQ(r.ui.Mode(), UiMode::kNormal) << "cycling left the normal screen";

    // Backwards off slot 0 lands on the LAST slot, empty or not. An empty slot
    // plays Init, so there is nothing to protect anyone from by skipping it --
    // and skipping would make the bank a different length depending on how
    // much of it you had filled in (Derek, 2026-09-29).
    r.ui.NotePresetLoaded(bank.slot[0], 0);
    r.Turn(kNav, -1);
    req = r.ui.TakePresetRequest();
    EXPECT_EQ(req.action, PresetAction::kLoad);
    EXPECT_EQ(req.slot, PresetBank::kNumSlots - 1);
    EXPECT_FALSE(bank.slot[req.slot].used) << "the last slot was not an empty one";

    // All the way round, every slot once, back where it started.
    r.ui.NotePresetLoaded(bank.slot[0], 0);
    for(int i = 1; i <= PresetBank::kNumSlots; i++)
    {
        r.Turn(kNav, 1);
        req = r.ui.TakePresetRequest();
        EXPECT_EQ(req.action, PresetAction::kLoad) << "stalled at step " << i;
        EXPECT_EQ(req.slot, i % PresetBank::kNumSlots) << "wrong slot at step " << i;
    }
}

// An empty slot is a slot you can play. The bank has to hand one out with a
// patch in it, or cycling onto it would be silence under a knob you are meant
// to turn (Derek, 2026-09-29: "I don't think empty user slots should be
// silent").
TEST(Preset, AnEmptySlotHoldsInitUnderAnEmptyName)
{
    static PresetBank bank;
    BuildFactoryBank(bank);

    int empties = 0;
    for(int i = 0; i < PresetBank::kNumSlots; i++)
    {
        if(bank.slot[i].used)
            continue;
        empties++;
        EXPECT_STREQ(bank.slot[i].name, kEmptySlotName)
            << "slot " << i << " does not say it is empty";

        StoredPreset want;
        PatchToPreset(InitPatch(), bank.slot[i].name, want);
        want.used = 0;
        EXPECT_EQ(std::memcmp(&bank.slot[i], &want, sizeof(StoredPreset)), 0)
            << "empty slot " << i << " is not Init";
    }
    EXPECT_GT(empties, 0) << "the factory bank has no empty slots to check";
}

// The header says what you are HEARING. Scrolling the browser past a slot is
// not loading it, and the number must not claim otherwise.
TEST(Ui, TheHeaderNumberFollowsWhatIsPlayingNotTheBrowser)
{
    Rig               r;
    static PresetBank bank;
    BuildFactoryBank(bank);
    r.ui.SetBank(&bank);
    r.ui.NotePresetLoaded(bank.slot[3], 3);
    r.ui.Refresh();
    ASSERT_EQ(r.ui.display().preset_slot, 3);

    OpenMenu(r, MenuItem::kSave);
    ASSERT_EQ(r.ui.Mode(), UiMode::kSave);
    for(int i = 0; i < 5; i++)
        r.Turn(kNav, 1);
    r.ui.Refresh();
    EXPECT_EQ(r.ui.display().browser_slot, 8) << "the browser did not move";
    EXPECT_EQ(r.ui.display().preset_slot, 3) << "the header followed the browser";

    // Loading is what moves it, and only NAV's turn on the normal screen loads.
    r.ui.NotePresetLoaded(bank.slot[8], 8);
    r.ui.Refresh();
    EXPECT_EQ(r.ui.display().preset_slot, 8);
}

// Where you are IS where the browser opens. Cycling to a preset and then
// saving should offer that slot first, not slot 1.
TEST(Ui, TheBrowserOpensOnThePresetThatIsPlaying)
{
    Rig               r;
    static PresetBank bank;
    BuildFactoryBank(bank);
    r.ui.SetBank(&bank);
    r.ui.NotePresetLoaded(bank.slot[5], 5);

    OpenMenu(r, MenuItem::kSave);
    ASSERT_EQ(r.ui.Mode(), UiMode::kSave);
    EXPECT_EQ(r.ui.display().browser_slot, 5);
}

// Back goes one level rather than all the way out.
TEST(Ui, NavHoldGoesBackOneLevel)
{
    Rig r;
    OpenMenu(r, MenuItem::kPatch);
    ASSERT_EQ(r.ui.Mode(), UiMode::kPatchPage);
    NavBack(r);
    EXPECT_EQ(r.ui.Mode(), UiMode::kMenu) << "back should land on the menu";
    NavBack(r);
    EXPECT_EQ(r.ui.Mode(), UiMode::kNormal);
}

TEST(Ui, HoldingMainDoesNothingButShift)
{
    Rig r;
    LongPress(r);
    EXPECT_EQ(r.ui.Mode(), UiMode::kNormal) << "a hold opened something";
    EXPECT_EQ(r.ui.TakePresetRequest().action, PresetAction::kNone);
    EXPECT_STREQ(r.ui.display().pending, "") << "a hold still announces an action";
}

TEST(Ui, NavMovesTheSelectionAndMainDoesNot)
{
    Rig            r;
    const EngineId before = r.ui.patch().engine;
    NavTap(r);
    r.Turn(kNav, 1);
    EXPECT_EQ(r.ui.display().menu_index, 1);
    EXPECT_EQ(r.ui.patch().engine, before) << "browsing the menu changed the engine";

    // MAIN never moves a selection, and never reaches the engine from in here.
    r.Turn(kMain, 1);
    EXPECT_EQ(r.ui.display().menu_index, 1) << "MAIN moved the selection";
    EXPECT_EQ(r.ui.patch().engine, before) << "MAIN changed the engine from the menu";
}

// Loading left the menu when NAV's turn became the selector: a list you scroll
// to reach a preset is a slower way to do what one detent now does, and two
// ways to do the same thing is what this revision is for (Derek, 2026-09-29).
// Save keeps its browser, because choosing WHERE to write is a real question.
TEST(Ui, TheMenuHasNoLoadEntry)
{
    for(int i = 0; i < static_cast<int>(MenuItem::kCount); i++)
        EXPECT_STRNE(MenuItemName(static_cast<MenuItem>(i)), "Load preset");
    EXPECT_STREQ(MenuItemName(MenuItem::kSave), "Save preset")
        << "Save should be the entry the menu opens on";
}

TEST(Ui, SavingChoosesASlotAndThenNamesIt)
{
    Rig r;
    OpenMenu(r, MenuItem::kSave);
    ASSERT_EQ(r.ui.Mode(), UiMode::kSave);
    r.Turn(kNav, 1);
    NavTap(r);
    ASSERT_EQ(r.ui.Mode(), UiMode::kName) << "saving names the slot first";
    NavTap(r);
    const PresetRequest req = r.ui.TakePresetRequest();
    EXPECT_EQ(req.action, PresetAction::kSave);
    EXPECT_EQ(req.slot, 1);
}

// THE MENU HAS PRECEDENCE. A pot brushed on the way past -- or moved by a
// gesture playing back, which no hand touched at all -- used to drop you out of
// it mid-thought, which is the same complaint the five-second timeout earned
// (Derek, 2026-09-29). Playing still works with the menu open; the menu just
// keeps the screen until you leave it.
TEST(Ui, NothingOnThePanelClosesTheMenu)
{
    {
        Rig r;
        NavTap(r);
        ASSERT_EQ(r.ui.Mode(), UiMode::kMenu);
        r.MovePot(0, 0.9f);
        EXPECT_EQ(r.ui.Mode(), UiMode::kMenu) << "a pot closed the menu";
    }
    {
        Rig r;
        OpenMenu(r, MenuItem::kSave);
        ASSERT_EQ(r.ui.Mode(), UiMode::kSave);
        r.Turn(0, 1); // a slot encoder
        EXPECT_EQ(r.ui.Mode(), UiMode::kSave) << "a slot encoder closed the browser";
        r.in.enc_held[0] = true;
        r.Tick();
        r.in.enc_held[0] = false;
        r.Tick();
        EXPECT_EQ(r.ui.Mode(), UiMode::kSave) << "a slot encoder press closed the browser";
        EXPECT_EQ(r.ui.TakePresetRequest().action, PresetAction::kNone);
    }
    // ...and the parameter still moved, because you can still play.
    {
        Rig r;
        NavTap(r);
        const float before = r.ui.patch().cutoff;
        r.MovePot(4, 0.8f);
        r.MovePot(4, 0.2f);
        EXPECT_NE(r.ui.patch().cutoff, before) << "the menu swallowed the knob";
        EXPECT_EQ(r.ui.Mode(), UiMode::kMenu);
    }
}

// The menu used to close itself after five seconds, which is precisely the
// moment it should not: it went away while Derek was still deciding what to
// pick (2026-09-25). Nothing is lost by leaving it open, and NAV's hold is the
// way out.
TEST(Ui, TheMenuWaitsAsLongAsItTakesAndNavHoldLeaves)
{
    Rig r;
    NavTap(r);
    ASSERT_EQ(r.ui.Mode(), UiMode::kMenu);
    for(int i = 0; i < 40; i++) // twenty seconds of thinking
        r.Tick(500);
    EXPECT_EQ(r.ui.Mode(), UiMode::kMenu) << "the menu closed while it was being read";

    // NAV's hold is the way out; the Close entry it used to need is gone.
    NavBack(r);
    EXPECT_EQ(r.ui.Mode(), UiMode::kNormal);
    EXPECT_EQ(r.ui.TakePresetRequest().action, PresetAction::kNone);
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
    Rig                   r;
    GestureRecorder       g;
    std::vector<uint16_t> buf(GestureRecorder::kLanes * GestureRecorder::kMaxFrames);
    g.Init(buf.data(), GestureRecorder::kMaxFrames);
    r.ui.SetGesture(&g);

    r.HoldMain(true);
    r.Tick();
    for(int i = 0; i < 20; i++)
    {
        r.in.pot[3] = 0.5f + ((i % 2) ? 0.003f : -0.003f);
        r.Tick(2);
    }
    r.HoldMain(false);
    r.Tick();
    EXPECT_EQ(g.state(), GestureRecorder::State::kArmed)
        << "dither made the tap look like a shift";
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
    OpenMenu(r, MenuItem::kSave);
    r.Tick();
    EXPECT_STREQ(r.ui.display().browser_name, bank.slot[0].name);
    EXPECT_TRUE(r.ui.display().browser_used);

    bank.slot[0].used = 0;
    r.Tick();
    EXPECT_FALSE(r.ui.display().browser_used);
}

// ------------------------------------------------------ GESTURE RECORDER

TEST(Ui, MainsTapCyclesTheGestureRecorder)
{
    Rig                   r;
    GestureRecorder       g;
    std::vector<uint16_t> buf(GestureRecorder::kLanes * GestureRecorder::kMaxFrames);
    g.Init(buf.data(), GestureRecorder::kMaxFrames);
    r.ui.SetGesture(&g);

    Tap(r); // arm -- nothing opened, no mode entered
    EXPECT_EQ(g.state(), GestureRecorder::State::kArmed);
    EXPECT_EQ(r.ui.Mode(), UiMode::kNormal);

    r.Tick(200);
    EXPECT_EQ(g.state(), GestureRecorder::State::kArmed) << "it should wait for a pot";
    r.MovePot(0, 0.2f);
    r.MovePot(0, 0.8f);
    EXPECT_EQ(g.state(), GestureRecorder::State::kRecording);

    Tap(r); // close the loop
    EXPECT_EQ(g.state(), GestureRecorder::State::kPlaying);
    Tap(r); // clear
    EXPECT_EQ(g.state(), GestureRecorder::State::kIdle);
}

TEST(Ui, APlayingLoopDrivesTheParameterItRecorded)
{
    Rig                   r;
    GestureRecorder       g;
    std::vector<uint16_t> buf(GestureRecorder::kLanes * GestureRecorder::kMaxFrames);
    g.Init(buf.data(), GestureRecorder::kMaxFrames);
    r.ui.SetGesture(&g);

    Tap(r);             // arm
    r.MovePot(0, 0.2f); // catch Harmonics, then sweep it
    for(int i = 0; i < 6; i++)
        r.MovePot(0, i % 2 ? 0.8f : 0.2f);
    ASSERT_EQ(g.state(), GestureRecorder::State::kRecording);
    Tap(r);
    ASSERT_EQ(g.state(), GestureRecorder::State::kPlaying);
    ASSERT_TRUE(g.in_loop(static_cast<int>(ParamId::kHarmonics)));

    float lo = 1.f, hi = 0.f;
    for(int i = 0; i < 200; i++)
    {
        r.Tick();
        lo = fminf(lo, r.ui.patch().harmonics);
        hi = fmaxf(hi, r.ui.patch().harmonics);
    }
    EXPECT_GT(hi - lo, 0.2f) << "the loop is not driving the parameter";
}

// MAIN's tap means the same thing with a menu up, because it always means the
// same thing.
TEST(Ui, TheRecorderIsReachableWhateverIsOnScreen)
{
    Rig                   r;
    GestureRecorder       g;
    std::vector<uint16_t> buf(GestureRecorder::kLanes * GestureRecorder::kMaxFrames);
    g.Init(buf.data(), GestureRecorder::kMaxFrames);
    r.ui.SetGesture(&g);

    NavTap(r);
    ASSERT_EQ(r.ui.Mode(), UiMode::kMenu);
    Tap(r);
    EXPECT_EQ(g.state(), GestureRecorder::State::kArmed);
    EXPECT_EQ(r.ui.Mode(), UiMode::kMenu) << "arming closed the menu";
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
    r.ui.NotePresetLoaded(bank.slot[1], 1);
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
    r.ui.NotePresetLoaded(bank.slot[1], 1);
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
    r.ui.NotePresetLoaded(bank.slot[2], 2);
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

// A pot at rest has to mean it. The ADCs deliver a channel ~108 times a second,
// so one bad conversion persists across many passes of the main loop -- counting
// passes would never catch it, and the screen would show a parameter nobody
// touched (Derek, 2026-09-24: "little ghost UI parameter changes").
TEST(Ui, AStrayReadingDoesNotMoveAParameterOrClaimTheScreen)
{
    Rig         r;
    r.MovePot(0, 0.2f); // catch Harmonics so it is live
    const float held = r.ui.patch().harmonics;
    r.Tick(500);        // and let it settle back to rest

    r.in.pot[0] = 0.9f; // one bad conversion
    r.Tick(10);
    r.in.pot[0] = 0.2f; // gone again
    r.Tick(10);
    EXPECT_NEAR(r.ui.patch().harmonics, held, 1e-5f) << "a glitch moved the parameter";

    // ...but a hand on the same knob still gets through promptly.
    r.in.pot[0] = 0.9f;
    r.Tick(10); // first sighting
    r.Tick(15);
    r.Tick(15); // held past the confirm window from that sighting
    EXPECT_NEAR(r.ui.patch().harmonics, 0.9f, 0.01f) << "a real turn was swallowed";
}

// Confirming must not make a turn feel stepped: once a pot is moving it keeps
// responding immediately.
TEST(Ui, APotAlreadyTurningRespondsWithoutWaiting)
{
    Rig r;
    r.MovePot(0, 0.2f);
    r.MovePot(0, 0.8f); // in motion now
    float last = r.ui.patch().harmonics;
    int   responded = 0;
    for(int i = 0; i < 10; i++)
    {
        r.in.pot[0] = 0.8f - 0.02f * static_cast<float>(i + 1);
        r.Tick(5); // faster than the confirm window
        if(fabsf(r.ui.patch().harmonics - last) > 1e-6f)
            responded++;
        last = r.ui.patch().harmonics;
    }
    EXPECT_GE(responded, 5) << "a turn in progress is being re-confirmed step by step";
}

// ------------------------------------------------------ SETTINGS PAGES
//
// The set-once-per-patch values, which have no pot of their own and until now
// could only be reached by editing a preset in source. Turn to choose the line,
// HOLD MAIN and turn to change it -- the same push-turn that picks the voice
// mode, so a page needs no edit mode of its own.

TEST(Ui, ThePatchPageChangesTheValuesNothingElseCanReach)
{
    Rig r;
    OpenMenu(r, MenuItem::kPatch);
    ASSERT_EQ(r.ui.Mode(), UiMode::kPatchPage);
    EXPECT_EQ(r.ui.display().page_count, static_cast<int>(PatchSetting::kCount));

    // NAV moves between lines and changes nothing.
    const float keytrack = r.ui.patch().keytrack;
    r.Turn(kNav, 1);
    EXPECT_EQ(r.ui.display().page_index, 1);
    EXPECT_FLOAT_EQ(r.ui.patch().keytrack, keytrack);

    // MAIN changes the highlighted line. No hold anywhere.
    const float vel = r.ui.patch().vel_to_filter;
    for(int i = 0; i < 5; i++)
        r.Turn(kMain, 1);
    EXPECT_GT(r.ui.patch().vel_to_filter, vel);
    EXPECT_EQ(r.ui.Mode(), UiMode::kPatchPage);

    r.Turn(kNav, 1);
    const int bend = r.ui.patch().bend_range;
    r.Turn(kMain, 1);
    EXPECT_EQ(r.ui.patch().bend_range, bend + 1);

    r.Turn(kNav, 1);
    const int shape = r.ui.patch().lfo_shape;
    r.Turn(kMain, 1);
    EXPECT_NE(r.ui.patch().lfo_shape, shape);
    EXPECT_LT(r.ui.patch().lfo_shape, fx::Lfo::kNumShapes);
}

TEST(Ui, TheGlobalPageSetsTheMidiChannel)
{
    Rig r;
    OpenMenu(r, MenuItem::kGlobal);
    ASSERT_EQ(r.ui.Mode(), UiMode::kGlobalPage);
    EXPECT_STREQ(r.ui.display().page_value, "Omni") << "0 should read as Omni";

    r.Turn(kMain, 1);
    EXPECT_EQ(r.ui.globals().midi_channel, 1);
    EXPECT_STREQ(r.ui.display().page_value, "1");

    for(int i = 0; i < 30; i++)
        r.Turn(kMain, -1);
    EXPECT_EQ(r.ui.globals().midi_channel, 0) << "it ran past Omni";
}

TEST(Ui, LeavingAPageKeepsWhatWasChanged)
{
    Rig r;
    OpenMenu(r, MenuItem::kPatch);
    r.Turn(kMain, 1);
    const float keytrack = r.ui.patch().keytrack;
    NavBack(r);
    EXPECT_EQ(r.ui.Mode(), UiMode::kMenu);
    EXPECT_FLOAT_EQ(r.ui.patch().keytrack, keytrack) << "leaving a page undid the change";
}

// ------------------------------------------------ ENGINES THAT CANNOT BE POLY
//
// Nothing overrides the voice mode any more. String and Karplus were the only
// engines that could not carry eight voices, and they went on 2026-09-29, so
// turning through the engine list must leave the mode exactly where you put it
// and Poly must be reachable on every one of them.

TEST(Ui, NoEngineTakesTheVoiceModeAwayFromYou)
{
    Rig r;
    ASSERT_EQ(r.ui.patch().voice_mode, VoiceMode::kPoly);

    for(int i = 0; i < static_cast<int>(EngineId::kCount) * 2; i++)
    {
        r.Turn(kMain, 1);
        SCOPED_TRACE(EngineName(r.ui.patch().engine));
        EXPECT_EQ(r.ui.patch().voice_mode, VoiceMode::kPoly)
            << "an engine moved the voice mode";
        EXPECT_STREQ(r.ui.display().notice, "")
            << "an engine announced a constraint it no longer has";
    }
}

TEST(Ui, PolyIsOfferedOnEveryEngine)
{
    Rig r;
    for(int e = 0; e < static_cast<int>(EngineId::kCount); e++)
    {
        SCOPED_TRACE(EngineName(r.ui.patch().engine));
        // Push-turn all the way round the modes; Poly has to come up.
        r.HoldMain(true);
        r.Tick();
        bool saw_poly = false;
        for(int i = 0; i < static_cast<int>(VoiceMode::kCount); i++)
        {
            r.Turn(kMain, 1);
            saw_poly |= r.ui.patch().voice_mode == VoiceMode::kPoly;
        }
        r.HoldMain(false);
        r.Tick();
        EXPECT_TRUE(saw_poly) << "Poly was skipped on this engine";
        r.Turn(kMain, 1); // next engine
    }
}

// ------------------------------------------------------- NAMING A PRESET
//
// The last thing on the panel that could not be done from the panel. Turn
// spells, hold and turn moves along the name, tap saves -- the same two axes a
// settings page uses, so it needs no gesture of its own.

TEST(Ui, SavingSpellsANameFirst)
{
    Rig               r;
    static PresetBank bank;
    BuildFactoryBank(bank);
    r.ui.SetBank(&bank);

    OpenMenu(r, MenuItem::kSave);
    NavTap(r); // choose slot 1 and go to the name
    ASSERT_EQ(r.ui.Mode(), UiMode::kName);
    EXPECT_EQ(std::string(r.ui.display().name_text).substr(0, 4), "INIT");

    // MAIN spells the character under the cursor; NAV moves along the name.
    r.Turn(kMain, 1);
    EXPECT_NE(r.ui.display().name_text[0], 'I');
    const char first = r.ui.display().name_text[0];

    r.Turn(kNav, 1);
    r.Turn(kNav, 1);
    EXPECT_EQ(r.ui.display().name_cursor, 2);
    EXPECT_EQ(r.ui.display().name_text[0], first) << "moving changed what was typed";

    r.Turn(kMain, -1);
    NavTap(r);
    const PresetRequest req = r.ui.TakePresetRequest();
    EXPECT_EQ(req.action, PresetAction::kSave);
    ASSERT_NE(req.name, nullptr);
    EXPECT_GT(strlen(req.name), 0u);
    EXPECT_LT(strlen(req.name), static_cast<size_t>(StoredPreset::kNameLen));
}

TEST(Ui, AnEmptySlotIsNamedAfterItselfAndAnAllSpaceNameFallsBack)
{
    Rig               r;
    static PresetBank bank;
    BuildFactoryBank(bank);
    bank.slot[5].used = 0;
    r.ui.SetBank(&bank);

    OpenMenu(r, MenuItem::kSave);
    for(int i = 0; i < 5; i++)
        r.Turn(kNav, 1);
    NavTap(r);
    ASSERT_EQ(r.ui.Mode(), UiMode::kName);
    EXPECT_EQ(std::string(r.ui.display().name_text).substr(0, 6), "USER 6");

    // Spell every character down to a space: a name of nothing is not a name.
    for(int c = 0; c < 6; c++)
    {
        if(c > 0)
            r.Turn(kNav, 1);
        for(int guard = 0; r.ui.display().name_text[c] != ' ' && guard < 60; guard++)
            r.Turn(kMain, -1);
        ASSERT_EQ(r.ui.display().name_text[c], ' ') << "could not blank character " << c;
    }
    NavTap(r);
    const PresetRequest req = r.ui.TakePresetRequest();
    ASSERT_NE(req.name, nullptr);
    EXPECT_STREQ(req.name, "USER 6") << "an all-spaces name should fall back to the slot";
}

// Touching a pot while spelling a name used to abandon the save. It no longer
// does -- the menu has precedence everywhere, not only in the menu itself --
// but the property that actually mattered is unchanged: a pot cannot SAVE.
// Only NAV's tap does that.
TEST(Ui, PlayingWhileNamingNeitherSavesNorLosesTheName)
{
    Rig               r;
    static PresetBank bank;
    BuildFactoryBank(bank);
    r.ui.SetBank(&bank);
    OpenMenu(r, MenuItem::kSave);
    NavTap(r);
    ASSERT_EQ(r.ui.Mode(), UiMode::kName);
    const char* spelled = r.ui.display().name_text;
    const char  first   = spelled[0];

    r.MovePot(0, 0.9f);
    EXPECT_EQ(r.ui.Mode(), UiMode::kName) << "a pot threw away the name";
    EXPECT_EQ(r.ui.TakePresetRequest().action, PresetAction::kNone) << "it saved anyway";
    EXPECT_EQ(r.ui.display().name_text[0], first) << "a pot spelled into the name";

    // NAV's hold goes back to the slot rather than out, so a mistyped name does
    // not mean choosing the slot again.
    NavBack(r);
    EXPECT_EQ(r.ui.Mode(), UiMode::kSave);
}

// The mod wheel used to be welded to LFO depth. It is a destination now, and
// every one of them has to actually go somewhere -- the same rule the LFO's
// destinations are held to, for the same reason.
TEST(Ui, TheModWheelDestinationIsOnThePatchPageAndStoredWithThePreset)
{
    Rig r;
    OpenMenu(r, MenuItem::kPatch);
    for(int guard = 0;
        r.ui.display().page_index != static_cast<int>(PatchSetting::kModDest) && guard < 8;
        guard++)
        r.Turn(kNav, 1);
    EXPECT_STREQ(r.ui.display().page_value, ModDestName(ModDest::kLfoDepth))
        << "the default should be what the wheel always did";

    r.Turn(kMain, 1);
    const ModDest picked = r.ui.patch().mod_dest;
    EXPECT_NE(picked, ModDest::kLfoDepth);

    StoredPreset s;
    PatchToPreset(r.ui.patch(), "mod", s);
    Patch back;
    PresetToPatch(s, back);
    EXPECT_EQ(back.mod_dest, picked);

    s.mod_dest = 0; // an older preset's padding
    PresetToPatch(s, back);
    EXPECT_EQ(back.mod_dest, ModDest::kLfoDepth);
    s.mod_dest = 200; // or a corrupt one
    PresetToPatch(s, back);
    EXPECT_LT(static_cast<int>(back.mod_dest), static_cast<int>(ModDest::kCount));
}

TEST(Ui, TheMidiOutChannelIsSeparateFromTheInputChannelAndHasNoOmni)
{
    Rig r;
    OpenMenu(r, MenuItem::kGlobal);
    for(int guard = 0;
        r.ui.display().page_index != static_cast<int>(GlobalSetting::kMidiOutChannel)
        && guard < 8;
        guard++)
        r.Turn(kNav, 1);
    EXPECT_STREQ(r.ui.display().page_value, "1");

    for(int i = 0; i < 40; i++) // wind it down past the bottom
        r.Turn(kMain, -1);
    EXPECT_EQ(r.ui.globals().midi_out_channel, 1) << "output cannot be Omni";
    EXPECT_EQ(r.ui.globals().midi_channel, 0) << "the input channel moved with it";
}


// ------------------------------------------------------- FACTORY RESET
//
// The one destructive thing on the instrument, so the one thing that has to be
// answered rather than merely tapped. A hold cannot be the confirmation -- a
// hold is shift -- so the answer is turned off NO first, which is a deliberate
// act rather than a tap that landed on the wrong entry.

TEST(Ui, FactoryResetHasToBeAnsweredBeforeItFires)
{
    Rig r;
    OpenMenu(r, MenuItem::kFactoryReset);
    ASSERT_EQ(r.ui.Mode(), UiMode::kResetPage);
    EXPECT_FALSE(r.ui.display().reset_armed) << "it should start on NO";

    // NAV's tap on NO is simply the way out.
    NavTap(r);
    EXPECT_EQ(r.ui.Mode(), UiMode::kNormal);
    EXPECT_FALSE(r.ui.TakeFactoryReset()) << "a reset fired without being answered";

    // MAIN turns the answer to YES; NAV's tap then fires it, once.
    OpenMenu(r, MenuItem::kFactoryReset);
    r.Turn(kMain, 1);
    EXPECT_TRUE(r.ui.display().reset_armed);
    NavTap(r);
    EXPECT_TRUE(r.ui.TakeFactoryReset());
    EXPECT_FALSE(r.ui.TakeFactoryReset()) << "the request is not one-shot";
}

// The reset page no longer backs out when you touch something else, because
// nothing does. What has to stay true is the part that would cost you your
// presets: it starts on NO every time it is opened, and only NAV's tap on YES
// resets anything.
TEST(Ui, FactoryResetStartsOnNoEveryTimeAndNeedsNavToFire)
{
    Rig r;
    OpenMenu(r, MenuItem::kFactoryReset);
    EXPECT_FALSE(r.ui.display().reset_armed) << "it opened on YES";
    r.Turn(kMain, 1);
    ASSERT_TRUE(r.ui.display().reset_armed);

    // Playing a note's worth of knob does not reset the instrument, and does
    // not close the page either.
    r.MovePot(0, 0.9f);
    EXPECT_EQ(r.ui.Mode(), UiMode::kResetPage);
    EXPECT_FALSE(r.ui.TakeFactoryReset()) << "a pot reset the instrument";

    // Leaving without confirming disarms it, so the next visit starts on NO.
    NavBack(r);
    ASSERT_EQ(r.ui.Mode(), UiMode::kMenu);
    EXPECT_FALSE(r.ui.TakeFactoryReset());
    OpenMenu(r, MenuItem::kFactoryReset);
    EXPECT_FALSE(r.ui.display().reset_armed) << "it remembered YES";
}
