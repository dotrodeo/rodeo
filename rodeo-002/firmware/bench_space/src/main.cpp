// rodeo-002 bench_space — where does the Space slot's CPU go?
//
// The FX pass measured Space as most of the texture chain's cost: Reverb 21.9%,
// Cloud 13.9, Wash 13.6, Echo 10.1. Those numbers can't say WHICH part is
// expensive, and each answer points at a different fix:
//
//   * ReverbSc itself        -> replace it with a lighter in-house reverb
//   * the grain engine       -> restructure the grain loop
//   * SDRAM access           -> move hot data to faster memory
//   * Space running at mix 0 -> stop reading grains nobody can hear
//
// So this runs ~20 short cases (~40 s total), each isolating one of those. Four
// cheap voices supply signal; every other slot is bypassed (and so costs
// nothing). Each line reports the load, its cost over the all-bypassed
// baseline, and the average number of grains playing -- which turns the grain
// cases into a cost-per-grain.
//
//   SP,case,load_pct,cost_pct,avg_grains,meter_avg_pct
//
// `load_pct` is measured here from raw timer ticks; `meter_avg_pct` is
// libDaisy's CpuLoadMeter, which reported 0.0 for every case on the first run
// and is kept only to show whether it agrees.
//
// Flash: tap RESET, tap BOOT while the LED pulses, then `make program-dfu`.
// Serial log at 115200. Audio plays throughout.

#include <cmath>
#include <cstdio>
#include <new>

#include "daisy_seed.h"
#include "daisysp.h"

#include "engines.h"
#include "texture_chain.h"

using namespace daisy;
using namespace rodeo002;
using fx::Space;
using fx::TextureChain;

static constexpr size_t kBlockSize  = 48;
static constexpr float  kSampleRate = 48000.f;
static constexpr int    kVoices     = 4;

// Space's buffers and TWO reverbs in SDRAM: the chain's, and a bare one for the
// "ReverbSc alone" case. Raw storage, constructed after hw.Init() brings the
// SDRAM up.
float DSY_SDRAM_BSS g_grain_l[Space::kGrainLen];
float DSY_SDRAM_BSS g_grain_r[Space::kGrainLen];
float DSY_SDRAM_BSS g_pre_l[Space::kPreLen];
float DSY_SDRAM_BSS g_pre_r[Space::kPreLen];
alignas(daisysp::ReverbSc) static uint8_t DSY_SDRAM_BSS
    g_reverb_mem[sizeof(daisysp::ReverbSc)];
alignas(daisysp::ReverbSc) static uint8_t DSY_SDRAM_BSS
    g_bare_reverb_mem[sizeof(daisysp::ReverbSc)];

DaisySeed          hw;
CpuLoadMeter       meter;
TextureChain       chain;
daisysp::ReverbSc* g_bare_reverb = nullptr;

// A plain voice: engine -> amp. Signal only; its cost is in the baseline.
struct Voice
{
    Engine          eng;
    daisysp::Adsr   env;
    void Init(float freq)
    {
        eng.Init(EngineId::kVariableSaw, kSampleRate);
        eng.SetParams(freq, 0.5f, 0.5f, 0.5f);
        env.Init(kSampleRate, 1);
        env.SetAttackTime(0.01f);
        env.SetSustainLevel(0.8f);
    }
};
Voice voices[kVoices];

volatile bool     g_chain_on = false;
volatile bool     g_bare_on  = false; // the "ReverbSc alone" case
volatile uint32_t g_blocks   = 0;

// Load measured HERE, from raw timer ticks, rather than trusting
// CpuLoadMeter: its first run reported 0.0 for every case (an infinite
// ticksPerBlockInv_ prints as 0 through an int cast). Both are reported so the
// log says which one to believe.
volatile uint32_t g_busy_ticks  = 0; // ticks spent inside the callback
volatile uint32_t g_busy_blocks = 0;
static uint32_t   g_tick_freq   = 0;
static float      g_block_ticks = 1.f; // ticks available per block

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    const uint32_t t_start = System::GetTick();
    meter.OnBlockStart();
    for(size_t i = 0; i < size; i++)
    {
        float s = 0.f;
        for(int v = 0; v < kVoices; v++)
            s += voices[v].eng.Process() * voices[v].env.Process(true) * 0.15f;
        out[0][i] = out[1][i] = s;
    }
    if(g_chain_on)
        chain.ProcessBlock(out[0], out[1], size);
    if(g_bare_on)
        for(size_t i = 0; i < size; i++)
            g_bare_reverb->Process(out[0][i] * 0.5f, out[1][i] * 0.5f, &out[0][i], &out[1][i]);
    meter.OnBlockEnd();
    g_busy_ticks += System::GetTick() - t_start;
    g_busy_blocks++;
    g_blocks++;
}

// Timing from the audio callback, not SysTick -- see bench_voices for why.
static void WaitBlocks(uint32_t n)
{
    const uint32_t start = g_blocks;
    while((g_blocks - start) < n)
        hw.SetLed(((g_blocks / 100) % 2) == 0);
}

// ------------------------------------------------------------ the cases

struct SpaceSettings
{
    int   type;
    float mix, time, feedback, grain, pitch;
    bool  bypass, freeze;
};

// Defaults are the FX pass's "heaviest" settings, so the cases line up with
// the numbers that raised the question.
static SpaceSettings Heavy(int type, float grain)
{
    return {type, 1.f, 0.5f, 0.8f, grain, 0.75f, false, false};
}

enum Special
{
    kNone = 0,
    kBaseline, // everything bypassed
    kBare,     // no chain; a bare ReverbSc on the mix
};

struct Case
{
    const char*   label;
    Special       special;
    SpaceSettings sp;
};

// clang-format off
static const Case kCases[] = {
    {"baseline - all bypassed",             kBaseline, {}},
    {"space bypassed - idle",               kNone, {Space::kEcho, 1, .5f, .8f, 1, .75f, true, false}},

    // Grain engine: cost scales with grains playing?
    {"echo grain 0",                        kNone, Heavy(Space::kEcho, 0.f)},
    {"echo grain 0.5",                      kNone, Heavy(Space::kEcho, 0.5f)},
    {"echo grain 1",                        kNone, Heavy(Space::kEcho, 1.f)},
    {"echo grain 1 pitch unison",           kNone, {Space::kEcho, 1, .5f, .8f, 1, .5f, false, false}},
    {"cloud grain 0",                       kNone, Heavy(Space::kCloud, 0.f)},
    {"cloud grain 0.5",                     kNone, Heavy(Space::kCloud, 0.5f)},
    {"cloud grain 1",                       kNone, Heavy(Space::kCloud, 1.f)},
    {"reverse grain 1",                     kNone, Heavy(Space::kReverse, 1.f)},
    {"wash grain 0",                        kNone, Heavy(Space::kWash, 0.f)},
    {"wash grain 1",                        kNone, Heavy(Space::kWash, 1.f)},

    // Does Space cost the same when nobody can hear it?
    {"echo grain 1 - MIX 0",                kNone, {Space::kEcho, 0, .5f, .8f, 1, .75f, false, false}},

    // Reverb: ReverbSc vs shimmer vs pre-delay
    {"reverbsc alone - bare - no chain",    kBare, {}},
    {"reverb grain 0 (no shimmer) time 0",  kNone, {Space::kReverb, 1, 0, .8f, 0, .75f, false, false}},
    {"reverb grain 0 (no shimmer) time 1",  kNone, {Space::kReverb, 1, 1, .8f, 0, .75f, false, false}},
    {"reverb grain 0.5 (shimmer)",          kNone, Heavy(Space::kReverb, 0.5f)},
    {"reverb grain 1 (full shimmer)",       kNone, Heavy(Space::kReverb, 1.f)},
    {"reverb grain 1 - frozen",             kNone, {Space::kReverb, 1, .5f, .8f, 1, .75f, false, true}},
};
// clang-format on

static void Configure(const Case& c)
{
    g_chain_on = false;
    g_bare_on  = false;
    WaitBlocks(3); // the callback has left the chain
    for(int s = 0; s < TextureChain::kNumSlots; s++)
        chain.SetBypass(s, true);
    if(c.special == kNone)
    {
        const SpaceSettings& p  = c.sp;
        Space&               sp = chain.space();
        chain.SetType(TextureChain::kSpace, p.type);
        chain.SetBypass(TextureChain::kSpace, p.bypass);
        chain.SetAmount(TextureChain::kSpace, p.mix);
        chain.SetSecondary(TextureChain::kSpace, 0.5f);
        chain.SetOption(TextureChain::kSpace, 0);
        sp.SetTime(p.time);
        sp.SetFeedback(p.feedback);
        sp.SetGrain(p.grain);
        sp.SetPitch(p.pitch);
        sp.SetSpread(1.f);
        sp.SetFreeze(false);
    }
    chain.space().Silence();
    chain.SnapToTargets();
    if(c.special == kBare)
    {
        g_bare_reverb->Init(kSampleRate); // fresh tail
        g_bare_reverb->SetFeedback(0.6f + 0.39f * 0.8f);
        g_bare_reverb->SetLpFreq(8000.f);
        g_bare_on = true;
    }
    else
        g_chain_on = true;
    // Freeze after the buffer has filled, as a player would.
    if(c.special == kNone && c.sp.freeze)
    {
        WaitBlocks(1000);
        chain.space().SetFreeze(true);
    }
}

static void PrintCase(const char* label, float load, float cost, float grains, float meter_avg)
{
    auto whole = [](float v) { return static_cast<int>(v); };
    auto tenth = [](float v) { return static_cast<int>(fabsf(v - truncf(v)) * 10.f); };
    hw.PrintLine("SP,%s,%d.%d,%s%d.%d,%d.%d,%d.%d",
                 label,
                 whole(load), tenth(load),
                 cost < 0.f && cost > -1.f ? "-" : "", whole(cost), tenth(cost),
                 whole(grains), tenth(grains),
                 whole(meter_avg), tenth(meter_avg));
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    daisysp::ReverbSc* reverb = new(g_reverb_mem) daisysp::ReverbSc();
    g_bare_reverb             = new(g_bare_reverb_mem) daisysp::ReverbSc();
    g_bare_reverb->Init(kSampleRate);
    chain.Init(kSampleRate, {g_grain_l, g_grain_r, g_pre_l, g_pre_r, reverb});
    chain.SetVolume(1.f);
    chain.SetTilt(0.5f);

    static const int kChord[kVoices] = {48, 55, 60, 64};
    for(int v = 0; v < kVoices; v++)
        voices[v].Init(daisysp::mtof(static_cast<float>(kChord[v])));

    meter.Init(kSampleRate, static_cast<int>(kBlockSize));
    g_tick_freq   = System::GetTickFreq();
    g_block_ticks = static_cast<float>(g_tick_freq) * static_cast<float>(kBlockSize)
                    / kSampleRate;
    hw.StartLog(false);
    System::Delay(1500);

    hw.PrintLine("=== rodeo-002 bench_space ===");
    hw.PrintLine("4 x variable-saw; other slots bypassed; Space at FX-pass heavy");
    hw.PrintLine("settings unless named (mix 1, time .5, fb .8, pitch +12, spread 1)");
    hw.PrintLine("tick freq %d Hz, %d ticks per block",
                 static_cast<int>(g_tick_freq),
                 static_cast<int>(g_block_ticks));
    hw.PrintLine("SP,case,load_pct,cost_pct,avg_grains,meter_avg_pct");

    hw.StartAudio(AudioCallback);

    float base = 0.f;
    for(const Case& c : kCases)
    {
        hw.PrintLine("RUN,%s", c.label);
        Configure(c);
        WaitBlocks(300);
        meter.Reset();
        g_busy_ticks  = 0;
        g_busy_blocks = 0;
        // Average the grain count over the window, sampling once per block.
        uint64_t       grain_sum = 0;
        uint32_t       samples   = 0;
        const uint32_t start     = g_blocks;
        uint32_t       last      = start;
        while((g_blocks - start) < 1200)
        {
            if(g_blocks != last)
            {
                last = g_blocks;
                grain_sum += static_cast<uint32_t>(chain.space().ActiveGrains());
                samples++;
            }
        }
        // Our own figure: ticks spent in the callback / ticks available.
        const uint32_t blocks = g_busy_blocks;
        const float    load   = blocks ? static_cast<float>(g_busy_ticks)
                                          / (static_cast<float>(blocks) * g_block_ticks) * 100.f
                                       : 0.f;
        const float meter_avg = meter.GetAvgCpuLoad() * 100.f;
        if(c.special == kBaseline)
            base = load;
        const float grains = (g_chain_on && samples)
                                 ? static_cast<float>(grain_sum) / static_cast<float>(samples)
                                 : 0.f;
        PrintCase(c.label, load, load - base, grains, meter_avg);
    }

    g_chain_on = false;
    g_bare_on  = false;
    hw.PrintLine("=== done ===");
    while(1)
    {
        hw.SetLed(((System::GetNow() / 500) % 2) == 0);
        System::Delay(50);
    }
}
