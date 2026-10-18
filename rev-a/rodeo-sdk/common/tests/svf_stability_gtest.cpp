// Regression guard for the DaisySP Svf blow-up that crashed 06_poly_synth,
// 09_poly_synth_oled and rodeo-001.
//
// The bug: Svf::Process applies a cubic drive term (band_ -= drive_*band_^3)
// intended to tame resonance. Driven hard at a high cutoff it DIVERGES — the
// state goes to NaN, and because nothing ever clears low_/band_/high_ the
// voice's filter is dead until the next power cycle. With DaisySP's stock
// pre_drive_ (0.5) and two oscillators summed at unity (+/-2.0), sweeping the
// cutoff past ~2-3 kHz with resonance and filter-env up blew up in roughly a
// third of realistic settings.
//
// The fix, in every Rodeo voice: gain-stage the sources to sum to +/-1.0, and
// call SetDrive(0.5) once at init (pre_drive_ = 0.05). These tests pin that
// invariant — they run the real vendored DaisySP, since the thing being
// guarded is its numerical behaviour, not our arithmetic.

#include "gtest/gtest.h"

#include "adsr.h"
#include "dsp.h"
#include "oscillator.h"
#include "svf.h"

#include <cmath>

using namespace daisysp;

namespace
{
constexpr float kSr        = 48000.0f; // board::kSampleRate
constexpr int   kBlock     = 32;       // board::kBlockSize
constexpr float kDetune    = 1.005793f;
constexpr float kEnvModHz  = 8000.0f;
constexpr float kSvfDrive  = 0.5f;     // examples' kSvfDrive
constexpr float kOscAmp    = 0.5f;     // examples' kOscAmp (two oscs -> unity)

struct Result
{
    bool  blew_up = false;
    float peak    = 0.0f;
};

// Replays a Rodeo voice: a held note while the cutoff pot is swept 0 -> 1,
// exactly the gesture that triggered the crash on the bench.
Result SweepCutoff(float osc_amp,
                   float drive,      // < 0 => never call SetDrive (DaisySP stock)
                   float res,
                   float env_amt,
                   float note_hz,
                   float seconds = 2.0f)
{
    Oscillator osc1, osc2;
    Svf        svf;
    Adsr       env;

    osc1.Init(kSr);
    osc1.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
    osc1.SetAmp(osc_amp);
    osc2.Init(kSr);
    osc2.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
    osc2.SetAmp(osc_amp);
    svf.Init(kSr);
    if(drive >= 0.0f)
        svf.SetDrive(drive); // once at init is enough: SetRes() reuses pre_drive_
    env.Init(kSr);

    osc1.SetFreq(note_hz);
    osc2.SetFreq(note_hz * kDetune);
    env.SetTime(ADSR_SEG_ATTACK, 0.01f);
    env.SetTime(ADSR_SEG_DECAY, 0.1f);
    env.SetSustainLevel(0.8f);
    env.SetTime(ADSR_SEG_RELEASE, 0.2f);

    Result    r;
    float     p_cut   = 800.0f; // the examples' initial value
    float     env_val = 0.0f;
    const int blocks  = static_cast<int>(kSr * seconds) / kBlock;

    for(int b = 0; b < blocks; ++b)
    {
        const float pot = static_cast<float>(b) / static_cast<float>(blocks);
        // the examples' Smooth() one-pole toward the mapped pot value
        p_cut += 0.05f * (fmap(pot, 40.0f, 12000.0f, Mapping::EXP) - p_cut);

        svf.SetFreq(p_cut + env_val * env_amt * kEnvModHz);
        svf.SetRes(res);

        for(int i = 0; i < kBlock; ++i)
        {
            env_val    = env.Process(true);
            const float s = osc1.Process() + osc2.Process();
            svf.Process(s);
            const float y = svf.Low();
            if(!std::isfinite(y))
            {
                r.blew_up = true;
                return r;
            }
            const float a = std::fabs(y * env_val);
            if(a > r.peak)
                r.peak = a;
        }
    }
    return r;
}

// The corners that actually broke on the bench, plus a spread of pitches.
const float kRes[]   = {0.0f, 0.5f, 0.8f, 0.92f};
const float kAmt[]   = {0.0f, 0.5f, 1.0f};
const float kNotes[] = {32.7f, 130.8f, 261.6f, 1046.5f, 2093.0f};
} // namespace

// THE regression test: the shipped voice configuration must never produce a
// non-finite sample, anywhere in the control range.
TEST(SvfStability, ShippedVoiceConfigNeverGoesNonFinite)
{
    for(float res : kRes)
        for(float amt : kAmt)
            for(float note : kNotes)
            {
                Result r = SweepCutoff(kOscAmp, kSvfDrive, res, amt, note);
                ASSERT_FALSE(r.blew_up)
                    << "filter diverged at res=" << res << " env_amt=" << amt
                    << " note=" << note << " Hz";
            }
}

// Margin: the fix must not be sitting right on the edge. Measured breaking
// point is ~4x the shipped input level; assert we survive 2x, so a future
// change that makes the voice somewhat hotter is caught here and not on a bench.
TEST(SvfStability, SurvivesTwiceTheShippedInputLevel)
{
    for(float res : kRes)
        for(float note : kNotes)
        {
            Result r = SweepCutoff(kOscAmp * 2.0f, kSvfDrive, res, 1.0f, note);
            ASSERT_FALSE(r.blew_up)
                << "no headroom left at res=" << res << " note=" << note;
        }
}

// The output must stay in a sane range too — removing the drive term entirely
// would also be "stable", but resonance would run to ~20x and clip hard.
TEST(SvfStability, ResonancePeakStaysBounded)
{
    float worst = 0.0f;
    for(float res : kRes)
        for(float amt : kAmt)
            for(float note : kNotes)
            {
                Result r = SweepCutoff(kOscAmp, kSvfDrive, res, amt, note);
                ASSERT_FALSE(r.blew_up);
                if(r.peak > worst)
                    worst = r.peak;
            }
    // Measured ~2.0. Well under the ~21 you get with the drive term removed.
    EXPECT_LT(worst, 4.0f) << "resonance gain has crept up: peak " << worst;
}

// Documents WHY the SetDrive() call exists: the DaisySP default is unstable for
// this voice. If a DaisySP bump ever fixes this upstream, this test flips and
// tells us the workaround can be revisited — it is not asserting a promise
// about DaisySP, it is recording the reason for our deviation from the default.
TEST(SvfStability, DaisySpDefaultDriveIsWhatBrokeUs)
{
    int blew = 0, total = 0;
    for(float res : kRes)
        for(float amt : kAmt)
            for(float note : kNotes)
            {
                // stock: no SetDrive call, two oscillators at unity (+/-2.0)
                ++total;
                if(SweepCutoff(1.0f, -1.0f, res, amt, note).blew_up)
                    ++blew;
            }
    EXPECT_GT(blew, 0) << "DaisySP's Svf no longer blows up with the stock "
                          "drive and a +/-2.0 input — the SetDrive(0.5) "
                          "workaround in the Rodeo voices can be revisited.";
    std::printf("      [ note ] stock DaisySP config blew up in %d of %d sweeps\n",
                blew, total);
}
