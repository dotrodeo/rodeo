// rodeo-002 — how loud is a preset, and why?
//
// Derek, 2026-10-01: "the maximum volume output has quite a wide range. Some
// patches are very loud, other patches are very quiet."
//
// Before choosing between a per-preset gain and a normaliser, it is worth
// knowing where the spread comes from. If it is mostly the engines, a per-engine
// trim fixes it for nothing -- no stored field, no panel control, no preset
// migration. If it is mostly the patches, nothing but a stored gain will do.

#include "gtest/gtest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "factory_presets.h"
#include "texture_chain.h"
#include "voice.h"

using namespace rodeo002;
using namespace rodeo002::fx;

namespace
{
constexpr float  kSr    = 48000.f;
constexpr size_t kBlock = 48;

struct Rig
{
    VoiceManager                       voices;
    std::vector<float>                 gl, gr, pl, pr;
    std::unique_ptr<daisysp::ReverbSc> reverb;
    std::unique_ptr<TextureChain>      chain;

    Rig()
    : gl(Space::kGrainLen), gr(Space::kGrainLen), pl(Space::kPreLen),
      pr(Space::kPreLen), reverb(new daisysp::ReverbSc), chain(new TextureChain)
    {
        voices.Init(kSr);
        chain->Init(kSr, {gl.data(), gr.data(), pl.data(), pr.data(), reverb.get()});
    }

    /** Everything main.cpp's ApplyPatch does, so the measurement is of the
     *  instrument rather than of a simplification of it. */
    void Apply(const Patch& p)
    {
        voices.SetEngine(p.engine);
        voices.ApplyPatch(p);
        voices.SetMaxVoices(
            CostModel::MaxVoices(p.engine, 0.f, p.sub_level > 0.f));
        for(int s = 0; s < TextureChain::kNumSlots; s++)
        {
            chain->SetAmount(s, p.amount[s]);
            chain->SetSecondary(s, p.secondary[s]);
            chain->SetOption(s, p.option[s]);
            chain->SetType(s, p.type[s]);
            chain->SetBypass(s, p.bypass[s]);
        }
        Space& sp = chain->space();
        sp.SetTime(p.space_time);
        sp.SetFeedback(p.space_feedback);
        sp.SetGrain(p.space_grain);
        sp.SetPitch(p.grain_pitch);
        sp.SetSpread(p.grain_spread);
        sp.SetFreeze(p.freeze);
        chain->SetTilt(p.tilt);
        chain->SetTrim(p.gain); // the preset's own level
        chain->SetVolume(1.f);  // master wide open: this is about the patch
        chain->SnapToTargets();
    }

    /** A four-note chord, held for two seconds and measured FROM NOTE-ON.
     *
     *  Measuring a late window was the first attempt and it was wrong: a struck
     *  patch with no sustain has finished by then, so Reverse Bell read as
     *  silence when what it actually is, is short. Peak answers "how loud does
     *  this get", RMS over the whole note answers "how loud is it to live
     *  with", and a patch needs both to be compared fairly. */
    void Play(double* rms, double* peak, bool with_chain)
    {
        for(int n : {48, 55, 60, 64})
            voices.NoteOn(n, 1.f);
        std::vector<float> l(kBlock), r(kBlock);
        double             e = 0, pk = 0;
        size_t             counted = 0;
        const size_t       total   = static_cast<size_t>(kSr * 2.f);
        for(size_t i = 0; i < total; i += kBlock)
        {
            voices.Render(l.data(), r.data(), kBlock);
            if(with_chain)
                chain->ProcessBlock(l.data(), r.data(), kBlock);
            for(size_t j = 0; j < kBlock; j++)
            {
                e += double(l[j]) * l[j];
                pk = fmax(pk, fabs(l[j]));
                counted++;
            }
        }
        *rms  = counted ? sqrt(e / double(counted)) : 0.0;
        *peak = pk;
    }
};

double Db(double x) { return 20.0 * log10(fmax(x, 1e-9)); }
} // namespace

TEST(Level, HowLoudIsEachFactoryPreset)
{
    PresetBank b;
    BuildFactoryBank(b);

    printf("\nFACTORY BANK levels, master wide open, one four-note chord\n");
    printf("  %-14s %-15s %8s %8s %8s\n", "preset", "engine", "rms dB", "peak dB", "peak");
    std::vector<double> all;
    for(int i = 0; i < PresetBank::kNumSlots; i++)
    {
        if(!b.slot[i].used)
            continue;
        Patch p;
        PresetToPatch(b.slot[i], p);

        double rms = 0, peak = 0, bare = 0, bare_pk = 0;
        {
            Rig rig;
            rig.Apply(p);
            rig.Play(&rms, &peak, true);
        }
        {
            Rig rig;
            rig.Apply(p);
            rig.Play(&bare, &bare_pk, false); // before the chain
        }
        printf("  %-14s %-15s %+8.1f %+8.1f %8.3f\n",
               b.slot[i].name, EngineName(p.engine), Db(rms), Db(peak), peak);
        (void)bare;
        (void)bare_pk;
        all.push_back(Db(rms));
    }
    std::sort(all.begin(), all.end());
    printf("  --> quietest %+.1f dB, loudest %+.1f dB, SPREAD %.1f dB\n",
           all.front(), all.back(), all.back() - all.front());
}

TEST(Level, HowMuchOfTheSpreadIsTheEngine)
{
    printf("\nENGINES at identical settings -- the part a per-engine trim could fix\n");
    printf("  %-15s %8s %8s\n", "engine", "rms dB", "peak");
    std::vector<double> all;
    for(int e = 0; e < static_cast<int>(EngineId::kCount); e++)
    {
        Patch p; // defaults, nothing in the chain
        p.engine  = static_cast<EngineId>(e);
        p.sustain = 1.f;
        p.attack  = 0.f;
        p.cutoff  = 1.f; // filter wide open: measure the engine, not the filter
        for(int s = 0; s < TextureChain::kNumSlots; s++)
            p.bypass[s] = true;

        Rig rig;
        rig.Apply(p);
        double rms = 0, peak = 0;
        rig.Play(&rms, &peak, false);
        printf("  %-15s %+8.1f %8.3f\n", EngineName(p.engine), Db(rms), peak);
        all.push_back(Db(rms));
    }
    std::sort(all.begin(), all.end());
    printf("  --> quietest %+.1f dB, loudest %+.1f dB, SPREAD %.1f dB\n",
           all.front(), all.back(), all.back() - all.front());
}

// Slow Drift measured as EXACTLY zero after the chain while being loud before
// it, and its peak stayed at 0. That is the signature of NaN, not of silence:
// fmax(x, NaN) returns x, so a NaN never raises a running peak.
TEST(Level, NoFactoryPresetProducesNaN)
{
    PresetBank b;
    BuildFactoryBank(b);
    for(int i = 0; i < PresetBank::kNumSlots; i++)
    {
        if(!b.slot[i].used)
            continue;
        Patch p;
        PresetToPatch(b.slot[i], p);
        SCOPED_TRACE(b.slot[i].name);

        Rig rig;
        rig.Apply(p);
        for(int n : {48, 55, 60, 64})
            rig.voices.NoteOn(n, 1.f);

        std::vector<float> l(kBlock), r(kBlock);
        size_t             first_bad = 0;
        bool               bad       = false;
        const size_t       total     = static_cast<size_t>(kSr * 3.f);
        for(size_t i2 = 0; i2 < total && !bad; i2 += kBlock)
        {
            rig.voices.Render(l.data(), r.data(), kBlock);
            for(size_t j = 0; j < kBlock; j++)
                if(!std::isfinite(l[j]) || !std::isfinite(r[j]))
                {
                    printf("  %-14s VOICE went non-finite at %.3f s\n",
                           b.slot[i].name, double(i2 + j) / kSr);
                    bad       = true;
                    first_bad = i2 + j;
                    break;
                }
            if(bad)
                break;
            rig.chain->ProcessBlock(l.data(), r.data(), kBlock);
            for(size_t j = 0; j < kBlock; j++)
                if(!std::isfinite(l[j]) || !std::isfinite(r[j]))
                {
                    printf("  %-14s CHAIN went non-finite at %.3f s\n",
                           b.slot[i].name, double(i2 + j) / kSr);
                    bad       = true;
                    first_bad = i2 + j;
                    break;
                }
        }
        (void)first_bad;
        EXPECT_FALSE(bad) << "this preset produces a non-finite sample";
    }
}

// Prints the gain each factory preset needs to reach a common peak, which is
// how the numbers in factory_presets.h were arrived at. Left in the suite so
// the next person to re-voice the bank does not have to rebuild this.
TEST(Level, SuggestedFactoryGains)
{
    const double target_db = -6.0; // peak, leaving headroom for velocity
    PresetBank   b;
    BuildFactoryBank(b);
    printf("\nSUGGESTED patch gains for a %.0f dB peak\n", target_db);
    for(int i = 0; i < PresetBank::kNumSlots; i++)
    {
        if(!b.slot[i].used)
            continue;
        Patch p;
        PresetToPatch(b.slot[i], p);
        p.gain = 1.f; // measure the sound, not whatever gain it already carries

        Rig rig;
        rig.Apply(p);
        double rms = 0, peak = 0;
        rig.Play(&rms, &peak, true);
        double want = target_db - Db(peak);
        const bool clipped = want > 12.0 || want < -18.0;
        want = want > 12.0 ? 12.0 : (want < -18.0 ? -18.0 : want);
        printf("  %-14s peak %+6.1f dB -> gain %+5.1f dB  (DbToGain %.4ff)%s\n",
               b.slot[i].name, Db(peak), want, pow(10.0, want / 20.0),
               clipped ? "   AT THE LIMIT" : "");
    }
}

// The bank has to STAY level. Peak is what is matched, not RMS: a pluck
// genuinely carries less energy than a pad, and forcing their RMS together
// would mean boosting the pluck until it clipped. Matching peaks is what stops
// one preset arriving 19 dB louder than the last.
TEST(Level, TheFactoryBankIsLevel)
{
    PresetBank b;
    BuildFactoryBank(b);
    double lo = 1e9, hi = -1e9;
    for(int i = 0; i < PresetBank::kNumSlots; i++)
    {
        if(!b.slot[i].used)
            continue;
        Patch p;
        PresetToPatch(b.slot[i], p);
        SCOPED_TRACE(b.slot[i].name);

        Rig rig;
        rig.Apply(p);
        double rms = 0, peak = 0;
        rig.Play(&rms, &peak, true);
        const double db = Db(peak);
        lo = fmin(lo, db);
        hi = fmax(hi, db);
        // Nothing may clip, and nothing may vanish.
        EXPECT_LT(peak, 0.95) << "this preset is against the limiter";
        EXPECT_GT(db, -14.0) << "this preset is far quieter than the rest";
    }
    printf("\n  factory peak spread: %.1f dB (was 19.3 before patch gains)\n", hi - lo);
    EXPECT_LT(hi - lo, 5.0) << "the bank has drifted out of level";
}

// A v2 bank must survive the move to v3 with every preset intact. The only
// field that changes meaning is the one that held a dead copy of `decay`.
TEST(Level, AV2BankMigratesWithoutLosingAnything)
{
    PresetBank fresh;
    BuildFactoryBank(fresh);

    // Forge a v2 bank: the v3 layout with the version put back and the gain
    // field holding what v2 put there, which was a copy of decay.
    PresetBank old = fresh;
    old.version    = 2;
    for(int i = 0; i < PresetBank::kNumSlots; i++)
        old.slot[i].gain = old.slot[i].decay;

    ASSERT_FALSE(old.Valid()) << "a v2 bank should not pass as v3";
    ASSERT_TRUE(old.MigrateFromV2());
    EXPECT_TRUE(old.Valid()) << "the migration did not produce a valid bank";

    for(int i = 0; i < PresetBank::kNumSlots; i++)
    {
        SCOPED_TRACE(testing::Message() << "slot " << i + 1);
        EXPECT_FLOAT_EQ(old.slot[i].gain, 1.f) << "a migrated slot kept decay as its gain";
        // Everything else has to be byte-for-byte what it was.
        EXPECT_STREQ(old.slot[i].name, fresh.slot[i].name);
        EXPECT_FLOAT_EQ(old.slot[i].cutoff, fresh.slot[i].cutoff);
        EXPECT_FLOAT_EQ(old.slot[i].decay, fresh.slot[i].decay);
        EXPECT_EQ(old.slot[i].engine_id, fresh.slot[i].engine_id);
        EXPECT_EQ(old.slot[i].used, fresh.slot[i].used);
    }

    // And it must refuse anything that is not a v2 bank.
    PresetBank junk = fresh;
    junk.version    = 99;
    EXPECT_FALSE(junk.MigrateFromV2());
    PresetBank current = fresh;
    EXPECT_FALSE(current.MigrateFromV2()) << "it migrated a bank that was already v3";
}
