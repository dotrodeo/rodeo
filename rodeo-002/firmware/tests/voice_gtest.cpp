// Voice allocation, the voice modes, and the filter-safety rule.
//
// The stability test matters most: DaisySP's Svf diverges to NaN when driven
// hard and never recovers, which killed voices in rodeo-001 and the platform's
// poly examples. VoiceManager keeps the source at +/-1 and SetDrive(0.5); this
// pins that across every engine and the corners of the patch.

#include "gtest/gtest.h"

#include <cmath>
#include <set>
#include <vector>

#include "voice.h"

using namespace rodeo002;

namespace
{
constexpr float  kSr    = 48000.f;
constexpr size_t kBlock = 48;

struct Rendered
{
    bool  finite = true;
    float peak   = 0.f;
    float tail   = 0.f; // peak over the last 50 ms
};

Rendered Render(VoiceManager& vm, float seconds, float tail_seconds = 0.05f)
{
    const size_t total = static_cast<size_t>(kSr * seconds);
    const size_t tail0 = total - static_cast<size_t>(kSr * tail_seconds);
    Rendered     r;
    float        buf[kBlock];
    for(size_t i = 0; i + kBlock <= total; i += kBlock)
    {
        vm.Render(buf, kBlock);
        for(size_t j = 0; j < kBlock; j++)
        {
            if(!std::isfinite(buf[j]))
                r.finite = false;
            const float a = fabsf(buf[j]);
            r.peak        = fmaxf(r.peak, a);
            if(i + j >= tail0)
                r.tail = fmaxf(r.tail, a);
        }
    }
    return r;
}
} // namespace

TEST(VoiceManager, PlaysAndThenGoesQuiet)
{
    VoiceManager vm;
    vm.Init(kSr);
    Patch p;
    p.decay = 0.05f; // ADS: decay is the release time
    vm.ApplyPatch(p);
    vm.NoteOn(60, 0.8f);
    EXPECT_GT(Render(vm, 0.5f).peak, 0.01f);
    vm.NoteOff(60);
    EXPECT_LT(Render(vm, 1.5f).tail, 1e-4f);
    EXPECT_EQ(vm.ActiveVoices(), 0);
}

TEST(VoiceManager, PolyUsesUpToTheCapThenSteals)
{
    VoiceManager vm;
    vm.Init(kSr);
    Patch p;
    vm.ApplyPatch(p);
    vm.SetMaxVoices(4);

    for(int n = 60; n < 64; n++)
        vm.NoteOn(n, 0.8f);
    Render(vm, 0.05f);
    EXPECT_EQ(vm.ActiveVoices(), 4);

    // A fifth note must steal rather than add.
    vm.NoteOn(64, 0.8f);
    Render(vm, 0.05f);
    EXPECT_LE(vm.ActiveVoices(), 4);
}

TEST(VoiceManager, LoweringTheCapSilencesTheVoicesAboveIt)
{
    VoiceManager vm;
    vm.Init(kSr);
    vm.ApplyPatch(Patch{});
    for(int n = 60; n < 68; n++)
        vm.NoteOn(n, 0.8f);
    Render(vm, 0.05f);
    EXPECT_EQ(vm.ActiveVoices(), 8);

    // What CostModel does when an expensive Space type is chosen.
    vm.SetMaxVoices(2);
    Render(vm, 0.05f);
    EXPECT_LE(vm.ActiveVoices(), 2);
}

TEST(VoiceManager, SustainPedalHoldsNotesAfterRelease)
{
    VoiceManager vm;
    vm.Init(kSr);
    Patch p;
    p.decay = 0.02f; // ADS: decay is the release time
    vm.ApplyPatch(p);
    vm.SetSustain(true);
    vm.NoteOn(60, 0.8f);
    vm.NoteOff(60);
    Render(vm, 0.3f);
    EXPECT_EQ(vm.ActiveVoices(), 1) << "pedal down: the note should still sound";
    vm.SetSustain(false);
    EXPECT_LT(Render(vm, 0.5f).tail, 1e-4f);
}

TEST(VoiceManager, MonoIsOneVoiceAndFallsBackToTheHeldNote)
{
    VoiceManager vm;
    vm.Init(kSr);
    Patch p;
    p.voice_mode = VoiceMode::kMono;
    vm.ApplyPatch(p);

    vm.NoteOn(60, 0.8f);
    vm.NoteOn(67, 0.8f); // legato on top
    Render(vm, 0.1f);
    EXPECT_EQ(vm.ActiveVoices(), 1);

    vm.NoteOff(67); // releasing the top note returns to the one still held
    Render(vm, 0.1f);
    EXPECT_EQ(vm.ActiveVoices(), 1);

    vm.NoteOff(60);
    EXPECT_LT(Render(vm, 1.0f).tail, 1e-3f);
}

TEST(VoiceManager, UnisonStacksSeveralVoicesOnOneNote)
{
    VoiceManager vm;
    vm.Init(kSr);
    Patch p;
    p.voice_mode = VoiceMode::kUnison;
    p.spread     = 1.f;
    vm.ApplyPatch(p);
    vm.NoteOn(60, 0.8f);
    Render(vm, 0.05f);
    EXPECT_GT(vm.ActiveVoices(), 1);
    EXPECT_LE(vm.ActiveVoices(), 4);
}

// The one that guards the platform's known Svf blow-up.
TEST(VoiceManager, EveryEngineStaysFiniteAtThePatchCorners)
{
    for(int e = 0; e < static_cast<int>(EngineId::kCount); e++)
    {
        const EngineId id = static_cast<EngineId>(e);
        for(float cutoff : {0.f, 0.6f, 1.f})
            for(float res : {0.f, 1.f})
                for(float env_amt : {0.f, 1.f})
                {
                    VoiceManager vm;
                    vm.Init(kSr);
                    Patch p;
                    p.engine     = id;
                    p.cutoff     = cutoff;
                    p.resonance  = res;
                    p.filter_env = env_amt;
                    p.harmonics = p.timbre = p.morph = 1.f;
                    p.attack                         = 0.f;
                    p.decay                          = 0.2f;
                    p.sustain                        = 1.f;
                    vm.SetEngine(id);
                    vm.ApplyPatch(p);
                    // Low notes work the physical models hardest.
                    vm.NoteOn(31, 1.f);
                    vm.NoteOn(43, 1.f);
                    const Rendered r = Render(vm, 1.0f);
                    SCOPED_TRACE(testing::Message()
                                 << EngineName(id) << " cutoff " << cutoff << " res " << res
                                 << " envamt " << env_amt);
                    EXPECT_TRUE(r.finite) << "filter or engine diverged";
                    EXPECT_LT(r.peak, 4.f);
                }
    }
}

TEST(VoiceManager, BendAndModWheelAreAudibleButBounded)
{
    VoiceManager vm;
    vm.Init(kSr);
    Patch p;
    p.lfo_dest  = LfoDest::kPitch;
    p.lfo_depth = 1.f;
    vm.ApplyPatch(p);
    vm.NoteOn(60, 0.8f);
    vm.SetBend(2.f);
    vm.SetModWheel(1.f);
    vm.SetAftertouch(1.f);
    const Rendered r = Render(vm, 0.5f);
    EXPECT_TRUE(r.finite);
    EXPECT_GT(r.peak, 0.01f);
    EXPECT_LT(r.peak, 4.f);
}

// The panel reads pots ~108 times a second. Applied raw, each reading lands as
// a jump and the ear hears a staircase -- reported from hardware on 2026-09-23,
// most audibly on Harmonics, which sets the virtual-analog sync frequency. The
// control smoothers must turn that into a ramp.
TEST(VoiceManager, ControlChangesRampRatherThanStep)
{
    VoiceManager vm;
    vm.Init(kSr, kBlock);
    Patch p;
    p.harmonics = 0.f;
    p.cutoff    = 0.2f;
    vm.ApplyPatch(p);
    vm.NoteOn(60, 0.8f);
    Render(vm, 0.2f); // settle at the starting value
    ASSERT_NEAR(vm.SmoothedHarmonics(), 0.f, 0.02f);

    // A big jump, as a fast turn of the knob delivers.
    p.harmonics = 1.f;
    p.cutoff    = 0.9f;
    vm.ApplyPatch(p);

    // One block must move only a fraction of the way -- that is the point.
    float  buf[kBlock];
    vm.Render(buf, kBlock);
    EXPECT_LT(vm.SmoothedHarmonics(), 0.2f) << "a single block jumped most of the way";

    // ...and it must still arrive promptly: ~20 ms, not a slow drift.
    for(int i = 0; i < 60; i++) // 60 blocks = 60 ms
        vm.Render(buf, kBlock);
    EXPECT_GT(vm.SmoothedHarmonics(), 0.9f);
    EXPECT_GT(vm.SmoothedCutoff(), 0.85f);
}

// Smoothing must not make a knob feel laggy: 20 ms is the target.
TEST(VoiceManager, ControlSmoothingReachesHalfwayWithinAboutTwentyMilliseconds)
{
    VoiceManager vm;
    vm.Init(kSr, kBlock);
    Patch p;
    p.harmonics = 0.f;
    vm.ApplyPatch(p);
    float buf[kBlock];
    for(int i = 0; i < 50; i++)
        vm.Render(buf, kBlock);

    p.harmonics = 1.f;
    vm.ApplyPatch(p);
    int blocks = 0;
    while(vm.SmoothedHarmonics() < 0.5f && blocks < 200)
    {
        vm.Render(buf, kBlock);
        blocks++;
    }
    EXPECT_GT(blocks, 3) << "too abrupt to be smoothing";
    EXPECT_LT(blocks, 40) << "a knob that takes this long feels laggy";
}

// The LFO is ticked once per block, so the block rate is its sample rate.
// Initialised at the AUDIO rate it advanced 1/48th as far per tick as it
// thought it was: "20 Hz" measured 0.45 Hz on the host, which is why the LFO
// felt stuck in the mud at every setting (2026-09-24).
namespace
{
double LfoHz(float rate, float seconds = 4.f)
{
    VoiceManager vm;
    vm.Init(kSr, kBlock);
    Patch p;
    p.lfo_rate  = rate;
    p.lfo_depth = 1.f;
    p.lfo_dest  = LfoDest::kCutoff;
    vm.ApplyPatch(p);
    vm.NoteOn(60, 0.8f);
    float  buf[kBlock];
    int    crossings = 0;
    float  prev      = 0.f;
    size_t blocks    = static_cast<size_t>(kSr * seconds) / kBlock;
    for(size_t i = 0; i < blocks; i++)
    {
        vm.Render(buf, kBlock);
        const float v = vm.LfoValue();
        if(i > 0 && (v > 0.f) != (prev > 0.f))
            crossings++;
        prev = v;
    }
    return crossings / 2.0 / static_cast<double>(seconds);
}
} // namespace

TEST(VoiceManager, TheLfoRunsAtTheRateItClaims)
{
    // Top of the range: 40 Hz, which the 1 kHz block rate samples 25 times a
    // cycle. Counting whole cycles over four seconds is coarse, hence the 10%.
    EXPECT_NEAR(LfoHz(1.f), 40.0, 4.0);
    // ...and it is still a slow LFO at the bottom.
    EXPECT_LT(LfoHz(0.f, 20.f), 0.2);
}

TEST(VoiceManager, TheLfoRateKnobIsMonotonic)
{
    const double a = LfoHz(0.25f);
    const double b = LfoHz(0.6f);
    const double c = LfoHz(1.f);
    EXPECT_LT(a, b);
    EXPECT_LT(b, c);
}

// ADS, Moog style (2026-09-24): there is no separate release time. The Decay
// knob sets the fall whether the key is still held or not, so a short decay has
// to give a short tail after note-off -- p.release is not consulted at all.
TEST(VoiceManager, DecayIsTheReleaseTime)
{
    for(float decay : {0.05f, 0.9f})
    {
        VoiceManager vm;
        vm.Init(kSr);
        Patch p;
        p.decay   = decay;
        p.sustain = 1.f;             // hold the level so only the tail differs
        p.release = 1.f;             // must be ignored
        vm.ApplyPatch(p);
        vm.NoteOn(60, 0.8f);
        Render(vm, 0.3f);
        vm.NoteOff(60);
        const float tail = Render(vm, 0.5f).tail;
        SCOPED_TRACE(testing::Message() << "decay " << decay);
        if(decay < 0.5f)
            EXPECT_LT(tail, 1e-3f) << "a short decay left a long tail";
        else
            EXPECT_GT(tail, 1e-3f) << "a long decay did not";
    }
}

// Reported from hardware 2026-09-24: with Decay at zero the synth "somewhat
// randomly" stops responding to keypresses, and changing presets does not fix
// it. The cause is not the decay -- it is that a voice was only ever started by
// a RISING GATE EDGE. DaisySP's Adsr restarts on `gate && !gate_`, and Start()
// set x.gate = true without retriggering, so re-using or stealing a voice whose
// gate was already high left the envelope exactly where it was. With a long
// decay that is inaudible; with a short one the envelope has already collapsed
// to zero, so the new note plays at no amplitude at all. It persists across a
// preset change because it is voice state, not patch state.
TEST(VoiceManager, RetriggeringAHeldNoteSoundsAgain)
{
    VoiceManager vm;
    vm.Init(kSr, kBlock);
    Patch p;
    p.attack  = 0.f;
    p.decay   = 0.f; // the fall time, and now the release too
    p.sustain = 0.f; // collapses to silence while the key is still down
    vm.ApplyPatch(p);

    vm.NoteOn(60, 0.9f);
    const float first = Render(vm, 0.2f).peak;
    ASSERT_GT(first, 0.01f) << "the first note never sounded";

    // The same key again, without releasing: the allocator re-uses that voice.
    vm.NoteOn(60, 0.9f);
    const float again = Render(vm, 0.2f).peak;
    EXPECT_GT(again, first * 0.5f) << "the retriggered note was silent";
}

// The same fault by the other route: every voice busy, so a new note steals one.
TEST(VoiceManager, AStolenVoiceSoundsAgain)
{
    VoiceManager vm;
    vm.Init(kSr, kBlock);
    Patch p;
    p.attack  = 0.f;
    p.decay   = 0.f;
    p.sustain = 0.f;
    vm.ApplyPatch(p);
    vm.SetMaxVoices(4);

    for(int n = 60; n < 64; n++) // fill the cap, all still held
        vm.NoteOn(n, 0.9f);
    Render(vm, 0.2f); // let them all collapse to silence

    vm.NoteOn(72, 0.9f); // must steal one, and must be heard
    EXPECT_GT(Render(vm, 0.2f).peak, 0.01f) << "the stolen voice never sounded";
}

// The retrigger above must not reach Mono, which is documented as legato: a new
// note while one is held changes the pitch and does NOT restart the envelope.
// MonoOn writes the voice directly rather than going through Start(), and this
// is what says so.
TEST(VoiceManager, MonoLegatoDoesNotRestartTheEnvelope)
{
    VoiceManager vm;
    vm.Init(kSr, kBlock);
    Patch p;
    p.voice_mode = VoiceMode::kMono;
    p.attack     = 0.f;
    p.decay      = 0.f;
    p.sustain    = 0.f; // the first note blips, then nothing
    vm.ApplyPatch(p);

    vm.NoteOn(60, 0.9f);
    ASSERT_GT(Render(vm, 0.2f).peak, 0.01f) << "the first note never sounded";
    vm.NoteOn(67, 0.9f); // legato, over the held note
    EXPECT_LT(Render(vm, 0.2f).peak, 0.01f) << "legato retriggered the envelope";

    // ...but a phrase that has ended and starts again does trigger.
    vm.NoteOff(67);
    vm.NoteOff(60);
    Render(vm, 0.2f);
    vm.NoteOn(62, 0.9f);
    EXPECT_GT(Render(vm, 0.2f).peak, 0.01f) << "a new phrase did not sound";
}

// The deeper half of the same fault, and the one that made it look random. With
// sustain at zero the envelope reaches IDLE while the key is STILL HELD, so the
// note-off used to skip the voice before DaisySP's Adsr ever saw the falling
// edge -- leaving its internal gate_ true for ever. Every later note-on then had
// no rising edge and was silent, in every preset, until the firmware restarted.
// Playing the same key over and over is what catches it.
TEST(VoiceManager, RepeatedNotesKeepSoundingWithNoSustain)
{
    for(int mode = 0; mode < 2; mode++)
    {
        VoiceManager vm;
        vm.Init(kSr, kBlock);
        Patch p;
        p.voice_mode = mode == 0 ? VoiceMode::kPoly : VoiceMode::kMono;
        p.attack     = 0.f;
        p.decay      = 0.f; // the whole envelope collapses while the key is down
        p.sustain    = 0.f;
        vm.ApplyPatch(p);
        SCOPED_TRACE(mode == 0 ? "poly" : "mono");

        for(int i = 0; i < 10; i++)
        {
            vm.NoteOn(60, 0.9f);
            const float peak = Render(vm, 0.1f).peak;
            vm.NoteOff(60);
            Render(vm, 0.1f);
            EXPECT_GT(peak, 0.01f) << "note " << i + 1 << " was silent";
        }
    }
}
