// Presets: the stored format, and the factory bank.
//
// The property that matters most is that storage does not depend on the ORDER
// of EngineId. Derek is weighing up dropping four engines (2026-09-24), and
// removing one renumbers every engine after it -- which, if presets stored the
// enum value, would silently turn every saved patch into a different sound.

#include "gtest/gtest.h"

#include <cmath>
#include <cstring>
#include <set>

#include "factory_presets.h"
#include "preset.h"

using namespace rodeo002;

TEST(Preset, EveryFieldSurvivesARoundTrip)
{
    Patch p;
    // Something different in every field, so a missed one shows up.
    p.harmonics = 0.11f; p.timbre = 0.22f; p.morph = 0.33f; p.spread = 0.44f;
    p.cutoff = 0.55f; p.resonance = 0.66f; p.attack = 0.77f; p.release = 0.88f;
    p.env_to_timbre = 0.12f; p.env_to_morph = 0.23f; p.decay = 0.34f;
    p.sustain = 0.45f; p.filter_env = 0.56f; p.lfo_rate = 0.67f; p.lfo_depth = 0.78f;
    p.keytrack = 0.19f; p.glide = 0.29f; p.vel_to_filter = 0.39f;
    p.bend_range = 7; p.lfo_shape = fx::Lfo::kSquare;
    p.engine = EngineId::kVosim;
    p.voice_mode = VoiceMode::kUnison;
    p.lfo_dest = LfoDest::kSpaceMix;
    p.filter = FilterType::kHighPass;
    for(int i = 0; i < fx::TextureChain::kNumSlots; i++)
    {
        p.amount[i]    = 0.1f * static_cast<float>(i + 1);
        p.secondary[i] = 0.2f * static_cast<float>(i + 1);
        p.type[i]      = i % fx::TextureChain::NumTypes(i);
        p.option[i]    = i % fx::TextureChain::NumOptions(i);
        p.bypass[i]    = (i % 2) == 1;
    }
    p.space_time = 0.41f; p.space_feedback = 0.42f; p.space_grain = 0.43f;
    p.grain_pitch = 0.44f; p.grain_spread = 0.45f; p.freeze = true; p.tilt = 0.46f;

    StoredPreset s;
    PatchToPreset(p, "Round Trip", s);
    Patch q;
    PresetToPatch(s, q);

    EXPECT_STREQ(s.name, "Round Trip");
    EXPECT_EQ(q.engine, p.engine);
    EXPECT_EQ(q.voice_mode, p.voice_mode);
    EXPECT_EQ(q.lfo_dest, p.lfo_dest);
    EXPECT_EQ(q.filter, p.filter);
    EXPECT_EQ(q.lfo_shape, p.lfo_shape);
    EXPECT_EQ(q.bend_range, p.bend_range);
    EXPECT_FLOAT_EQ(q.harmonics, p.harmonics);
    EXPECT_FLOAT_EQ(q.timbre, p.timbre);
    EXPECT_FLOAT_EQ(q.morph, p.morph);
    EXPECT_FLOAT_EQ(q.spread, p.spread);
    EXPECT_FLOAT_EQ(q.cutoff, p.cutoff);
    EXPECT_FLOAT_EQ(q.resonance, p.resonance);
    EXPECT_FLOAT_EQ(q.attack, p.attack);
    EXPECT_FLOAT_EQ(q.release, p.release);
    EXPECT_FLOAT_EQ(q.env_to_timbre, p.env_to_timbre);
    EXPECT_FLOAT_EQ(q.env_to_morph, p.env_to_morph);
    EXPECT_FLOAT_EQ(q.decay, p.decay);
    EXPECT_FLOAT_EQ(q.sustain, p.sustain);
    EXPECT_FLOAT_EQ(q.filter_env, p.filter_env);
    EXPECT_FLOAT_EQ(q.lfo_rate, p.lfo_rate);
    EXPECT_FLOAT_EQ(q.lfo_depth, p.lfo_depth);
    EXPECT_FLOAT_EQ(q.keytrack, p.keytrack);
    EXPECT_FLOAT_EQ(q.glide, p.glide);
    EXPECT_FLOAT_EQ(q.vel_to_filter, p.vel_to_filter);
    for(int i = 0; i < fx::TextureChain::kNumSlots; i++)
    {
        EXPECT_FLOAT_EQ(q.amount[i], p.amount[i]) << "slot " << i;
        EXPECT_FLOAT_EQ(q.secondary[i], p.secondary[i]) << "slot " << i;
        EXPECT_EQ(q.type[i], p.type[i]) << "slot " << i;
        EXPECT_EQ(q.option[i], p.option[i]) << "slot " << i;
        EXPECT_EQ(q.bypass[i], p.bypass[i]) << "slot " << i;
    }
    EXPECT_FLOAT_EQ(q.space_time, p.space_time);
    EXPECT_FLOAT_EQ(q.space_feedback, p.space_feedback);
    EXPECT_FLOAT_EQ(q.space_grain, p.space_grain);
    EXPECT_FLOAT_EQ(q.grain_pitch, p.grain_pitch);
    EXPECT_FLOAT_EQ(q.grain_spread, p.grain_spread);
    EXPECT_EQ(q.freeze, p.freeze);
    EXPECT_FLOAT_EQ(q.tilt, p.tilt);
}

// The numbers themselves are the contract with every bank already in flash.
// If this fails, presets on hardware will load as the wrong sounds.
TEST(Preset, StableIdsAreTheseExactNumbersForever)
{
    EXPECT_EQ(EngineToStableId(EngineId::kVirtualAnalog), 1);
    EXPECT_EQ(EngineToStableId(EngineId::kVariableSaw), 2);
    EXPECT_EQ(EngineToStableId(EngineId::kFm2), 3);
    EXPECT_EQ(EngineToStableId(EngineId::kFormant), 4);
    EXPECT_EQ(EngineToStableId(EngineId::kVosim), 5);
    EXPECT_EQ(EngineToStableId(EngineId::kHarmonic), 6);
    EXPECT_EQ(EngineToStableId(EngineId::kZOsc), 7);

    // 8..11 were Grainlet, Particle, String and Karplus, dropped 2026-09-29.
    // A preset saved by an older build still names one, and it has to keep
    // meaning "an engine this firmware does not have" rather than being handed
    // to whatever is added next. This is the test that stops that happening.
    for(uint8_t retired : {8, 9, 10, 11})
    {
        bool known = true;
        EXPECT_EQ(EngineFromStableId(retired, &known), EngineId::kVirtualAnalog)
            << "retired id " << static_cast<int>(retired) << " resolved to an engine";
        EXPECT_FALSE(known) << "retired id " << static_cast<int>(retired)
                            << " was reported as a preset that loaded intact";
    }
    for(int e = 0; e < static_cast<int>(EngineId::kCount); e++)
        for(uint8_t retired : {8, 9, 10, 11})
            EXPECT_NE(EngineToStableId(static_cast<EngineId>(e)), retired)
                << "a live engine was handed a retired id";
}

TEST(Preset, EveryEngineHasItsOwnIdAndComesBack)
{
    std::set<uint8_t> seen;
    for(int e = 0; e < static_cast<int>(EngineId::kCount); e++)
    {
        const EngineId id  = static_cast<EngineId>(e);
        const uint8_t  sid = EngineToStableId(id);
        EXPECT_NE(sid, 0) << EngineName(id) << " has no stable id";
        EXPECT_TRUE(seen.insert(sid).second) << EngineName(id) << " shares an id";
        bool known = false;
        EXPECT_EQ(EngineFromStableId(sid, &known), id);
        EXPECT_TRUE(known);
    }
}

// What happens to a preset whose engine this build no longer has: it must load,
// it must not index past the end of anything, and it must SAY so.
TEST(Preset, APresetNamingAMissingEngineFallsBackAndSaysSo)
{
    StoredPreset s;
    PatchToPreset(Patch{}, "Ghost", s);
    s.engine_id = 200; // an engine from a firmware that isn't this one
    Patch p;
    bool  known = true;
    PresetToPatch(s, p, &known);
    EXPECT_FALSE(known) << "a missing engine loaded as though nothing was wrong";
    EXPECT_EQ(p.engine, EngineId::kVirtualAnalog);
    EXPECT_LT(static_cast<int>(p.engine), static_cast<int>(EngineId::kCount));
}

// A store can be older than the firmware, or corrupt. Neither may produce a
// patch that walks off the end of a table or holds a value the panel could not
// have set.
TEST(Preset, ACorruptStoreCannotProduceAnIllegalPatch)
{
    StoredPreset s;
    PatchToPreset(Patch{}, "Corrupt", s);
    s.voice_mode = 99;
    s.lfo_dest   = 99;
    s.filter     = 99;
    s.lfo_shape  = 99;
    s.bend_range = -100;
    s.cutoff     = 4.5f;
    s.resonance  = -2.f;
    s.attack     = NAN;
    for(int i = 0; i < fx::TextureChain::kNumSlots; i++)
    {
        s.type[i]      = 250;
        s.option[i]    = 250;
        s.amount[i]    = 17.f;
        s.secondary[i] = NAN;
    }
    Patch p;
    PresetToPatch(s, p);

    EXPECT_LT(static_cast<int>(p.voice_mode), static_cast<int>(VoiceMode::kCount));
    EXPECT_LT(static_cast<int>(p.lfo_dest), static_cast<int>(LfoDest::kCount));
    EXPECT_LT(static_cast<int>(p.filter), static_cast<int>(FilterType::kCount));
    EXPECT_LT(p.lfo_shape, fx::Lfo::kNumShapes);
    EXPECT_GE(p.bend_range, 0);
    EXPECT_GE(p.cutoff, 0.f);
    EXPECT_LE(p.cutoff, 1.f);
    EXPECT_GE(p.resonance, 0.f);
    EXPECT_TRUE(std::isfinite(p.attack));
    for(int i = 0; i < fx::TextureChain::kNumSlots; i++)
    {
        EXPECT_LT(p.type[i], fx::TextureChain::NumTypes(i)) << "slot " << i;
        EXPECT_LT(p.option[i], fx::TextureChain::NumOptions(i)) << "slot " << i;
        EXPECT_LE(p.amount[i], 1.f) << "slot " << i;
        EXPECT_TRUE(std::isfinite(p.secondary[i])) << "slot " << i;
    }
}

TEST(Preset, TheFactoryBankIsValidAndLoadable)
{
    PresetBank b;
    BuildFactoryBank(b);
    ASSERT_TRUE(b.Valid());

    int used = 0;
    for(int i = 0; i < PresetBank::kNumSlots; i++)
    {
        const StoredPreset& s = b.slot[i];
        if(!s.used)
            continue;
        used++;
        SCOPED_TRACE(testing::Message() << "slot " << i << " '" << s.name << "'");
        EXPECT_GT(strlen(s.name), 0u);
        EXPECT_LT(strlen(s.name), static_cast<size_t>(StoredPreset::kNameLen));
        bool  known = false;
        Patch p;
        PresetToPatch(s, p, &known);
        EXPECT_TRUE(known) << "factory preset names an engine this build lacks";
        for(int k = 0; k < fx::TextureChain::kNumSlots; k++)
        {
            EXPECT_LT(p.type[k], fx::TextureChain::NumTypes(k));
            EXPECT_LT(p.option[k], fx::TextureChain::NumOptions(k));
        }
    }
    // Sixteen factory sounds; the rest are empty on purpose, for you to fill.
    EXPECT_EQ(used, 16) << "the factory bank should ship sixteen sounds";
    EXPECT_GT(PresetBank::kNumSlots + 0, used) << "there should be room to save your own";
    for(int i = used; i < PresetBank::kNumSlots; i++)
        EXPECT_FALSE(b.slot[i].used) << "slot " << i << " should be empty, not a copy";
}

// Derek is weighing up dropping these four (2026-09-24). No factory preset may
// depend on one, so that dropping them orphans nothing.
TEST(Preset, NoFactoryPresetUsesAnEngineOnTheChoppingBlock)
{
    PresetBank b;
    BuildFactoryBank(b);
    for(int i = 0; i < PresetBank::kNumSlots; i++)
    {
        if(!b.slot[i].used)
            continue;
        Patch p;
        PresetToPatch(b.slot[i], p);
        SCOPED_TRACE(testing::Message() << b.slot[i].name);
        EXPECT_LT(static_cast<int>(p.engine), static_cast<int>(EngineId::kCount));
    }
}

// PersistentStorage only writes when the bank compares unequal, so a save that
// changed one slot has to be visible through operator!=.
TEST(Preset, ABankComparesUnequalWhenASlotChanges)
{
    PresetBank a, b;
    BuildFactoryBank(a);
    BuildFactoryBank(b);
    EXPECT_TRUE(a == b);
    Patch p;
    p.cutoff = 0.123f;
    PatchToPreset(p, "Changed", b.slot[9]);
    EXPECT_TRUE(a != b) << "a saved preset would never reach flash";
}

TEST(Preset, AnOlderOrEmptyBankIsNotMistakenForOurs)
{
    PresetBank b;
    BuildFactoryBank(b);
    b.version = PresetBank::kVersion + 1;
    EXPECT_FALSE(b.Valid());
    BuildFactoryBank(b);
    b.magic = 0xFFFFFFFFu; // erased flash
    EXPECT_FALSE(b.Valid());
}

// New factory sounds have to reach an instrument that has already been saved to.
// The store's defaults are only written while it has never been used, so after
// the first save a later firmware's bank would otherwise never be seen.
TEST(Preset, EmptySlotsAreFilledFromTheFactoryWithoutTouchingAnythingElse)
{
    PresetBank factory;
    BuildFactoryBank(factory);

    // A bank as it might be found in flash: an older firmware's first eight,
    // one slot saved by hand, the rest never filled.
    PresetBank stored;
    BuildFactoryBank(stored);
    for(int i = 8; i < PresetBank::kNumSlots; i++)
        stored.slot[i].used = 0;
    Patch mine;
    mine.cutoff = 0.321f;
    PatchToPreset(mine, "Mine", stored.slot[9]);

    // Only slots the FACTORY has a sound for: 16 of them, less the nine that
    // are already occupied (0-7 and the saved one).
    const int filled = FillEmptySlots(stored, factory);
    EXPECT_EQ(filled, 16 - 9) << "every empty slot the factory can fill should be";
    // ...and the user slots past the factory sixteen stay empty, rather than
    // being refilled at every boot.
    for(int i = 16; i < PresetBank::kNumSlots; i++)
        EXPECT_FALSE(stored.slot[i].used) << "user slot " << i << " was filled";

    // What was there is untouched -- the saved one above all.
    EXPECT_STREQ(stored.slot[9].name, "Mine");
    Patch back;
    PresetToPatch(stored.slot[9], back);
    EXPECT_FLOAT_EQ(back.cutoff, 0.321f);
    for(int i = 0; i < 8; i++)
        EXPECT_STREQ(stored.slot[i].name, factory.slot[i].name) << "slot " << i;
    // ...and the new sounds arrived.
    EXPECT_STREQ(stored.slot[10].name, factory.slot[10].name);
    EXPECT_TRUE(stored.slot[15].used);

    // Running it again changes nothing: it is not a restore.
    EXPECT_EQ(FillEmptySlots(stored, factory), 0);
}

TEST(Preset, FillingRefusesABankItDoesNotUnderstand)
{
    PresetBank factory, stored;
    BuildFactoryBank(factory);
    BuildFactoryBank(stored);
    stored.slot[3].used = 0;
    stored.magic        = 0xFFFFFFFFu; // erased
    EXPECT_EQ(FillEmptySlots(stored, factory), 0);
}

// ------------------------------------------------------------ SUB-OSCILLATOR
//
// The sub rides in the two bytes that were `reserved`. Those have always been
// written as zero, which is what let it arrive without bumping
// PresetBank::kVersion -- and a version bump would have rejected the stored
// bank and lost every preset already saved. These tests are what that promise
// rests on.

TEST(Preset, APresetFromBeforeTheSubLoadsWithItOff)
{
    // Exactly what an older build wrote: a valid preset with both spare bytes
    // zero, which is all `reserved` ever contained.
    Patch        original;
    original.cutoff = 0.42f;
    StoredPreset s;
    PatchToPreset(original, "Old", s);
    s.sub_level = 0;
    s.sub_shape = 0;

    Patch p;
    PresetToPatch(s, p);
    EXPECT_FLOAT_EQ(p.sub_level, 0.f) << "an old preset came back with the sub audible";
    EXPECT_EQ(p.sub_interval, SubInterval::kMinus12)
        << "a zeroed byte has to decode to the interval you would have picked";
    EXPECT_EQ(p.sub_wave, SubWave::kSine);
    EXPECT_FLOAT_EQ(p.cutoff, 0.42f) << "the rest of the preset did not survive";

    // And it still round-trips to the same bytes, so loading an old preset and
    // saving it back does not mark the bank dirty or rewrite flash.
    StoredPreset again;
    PatchToPreset(p, "Old", again);
    EXPECT_EQ(std::memcmp(&s, &again, sizeof(StoredPreset)), 0);
}

TEST(Preset, TheSubSurvivesARoundTrip)
{
    for(int w = 0; w < static_cast<int>(SubWave::kCount); w++)
        for(int i = 0; i < static_cast<int>(SubInterval::kCount); i++)
        {
            Patch p;
            p.sub_level    = 0.6f;
            p.sub_wave     = static_cast<SubWave>(w);
            p.sub_interval = static_cast<SubInterval>(i);
            SCOPED_TRACE(testing::Message()
                         << SubWaveName(p.sub_wave) << " " << SubIntervalName(p.sub_interval));

            StoredPreset s;
            PatchToPreset(p, "Sub", s);
            Patch q;
            PresetToPatch(s, q);

            EXPECT_EQ(q.sub_wave, p.sub_wave);
            EXPECT_EQ(q.sub_interval, p.sub_interval);
            // 8 bits over 0..1, so half a step is the most it can be out.
            EXPECT_NEAR(q.sub_level, p.sub_level, 1.f / 255.f);
        }
}

// A corrupt byte must not index past either table.
TEST(Preset, AnImpossibleSubByteFallsBackRatherThanIndexingOut)
{
    Patch        p;
    StoredPreset s;
    PatchToPreset(p, "Sub", s);
    s.sub_shape = 0xFF; // wave 15, interval 15: neither exists

    Patch q;
    PresetToPatch(s, q);
    EXPECT_LT(static_cast<int>(q.sub_wave), static_cast<int>(SubWave::kCount));
    EXPECT_LT(static_cast<int>(q.sub_interval), static_cast<int>(SubInterval::kCount));
}
