// rodeo-002 bench — how many voices of each engine does this board actually run?
//
// The concept doc targets 8 voices and says so as an ESTIMATE. This program
// replaces the estimate with a measurement, and its numbers decide both the
// voice count and the final engine list before anything is committed to.
//
// What it does: for every candidate engine, and for a rising number of voices,
// it holds a sustained chord and reports the audio-callback load from
// libDaisy's CpuLoadMeter. It runs the whole sweep twice -- once with the
// texture chain bypassed (voices only), once with the full chain including the
// granular delay -- because the chain's cost is what decides whether 8 voices
// survives contact with the rest of the instrument.
//
//   Voice under test = one engine -> Svf low-pass -> amp ADSR, with a filter
//   ADSR at block rate. That is exactly the rodeo-002 voice from the control
//   map, so the numbers transfer directly.
//
// Reading the output: rows are CSV on the serial log, plus a summary at the
// end giving, per engine, the largest voice count that stayed under 70% average
// load. 70% is deliberate, not shyness -- NOTHING ELSE IS RUNNING during this
// test. The real instrument also polls 16 pots over SPI, polls the encoder hub
// over I2C, redraws the OLED and handles MIDI. Treat the 70% column as the
// honest voice count and the 90% one as the cliff.
//
// PASS FX (first, ~40 s): the cost of every texture-chain effect type -- the
// REAL effects in ../common, the ones the instrument will ship. Four cheap
// voices supply signal; every slot is bypassed (and so costs nothing) except
// the one under test, which is pushed to its most expensive settings. Then the
// most expensive type of each slot runs all at once: the worst-case chain.
// The engine sweep's "fx on" pass then runs with THAT chain, so its voice
// counts are the real ones.
//
// Flash: this builds for the Daisy bootloader by default (see the Makefile and
// ../README.md). Tap RESET, tap BOOT while the LED pulses (holds the bootloader
// in DFU), then `make program-dfu`. Open the serial log (115200) to watch.
// Audio comes out of the jacks throughout, so you can hear each effect type
// and each engine as it is measured.

#include <cmath>
#include <cstdio>
#include <new>

#include "daisy_seed.h"
#include "daisysp.h"

#include "engines.h"
#include "texture_chain.h"

using namespace daisy;
using namespace daisysp;
using namespace rodeo002;

using fx::Space;
using fx::TextureChain;

// Space's big buffers live in SDRAM. ReverbSc (~396 KB) too -- as raw storage,
// constructed only after hw.Init() has brought the SDRAM up: a global object
// with a constructor would be written to before the SDRAM exists.
float DSY_SDRAM_BSS g_grain_l[Space::kGrainLen];
float DSY_SDRAM_BSS g_grain_r[Space::kGrainLen];
float DSY_SDRAM_BSS g_pre_l[Space::kPreLen];
float DSY_SDRAM_BSS g_pre_r[Space::kPreLen];
alignas(daisysp::ReverbSc) static uint8_t DSY_SDRAM_BSS
    g_reverb_mem[sizeof(daisysp::ReverbSc)];

static constexpr size_t kBlockSize  = 48;
static constexpr float  kSampleRate = 48000.f;

// Voice counts to try, in order. The sweep abandons an engine as soon as one
// of these exceeds the cliff, so heavy engines cost little bench time.
#ifdef BOOT_APP
// Under the Daisy bootloader (APP_TYPE=BOOT_SRAM, which defines BOOT_APP),
// zero-initialised globals live in the 128 KB DTCM, shared with the stack, and
// 16 voices overflow it by ~5 KB. Twelve is enough to confirm the numbers carry
// over to that mode: only four engines ever ran past 12 anyway.
static constexpr int kMaxVoices     = 12;
static constexpr int kVoiceSteps[]  = {1, 2, 4, 6, 8, 10, 12};
static constexpr int kNumVoiceSteps = 7;
#else
static constexpr int kMaxVoices     = 16;
static constexpr int kVoiceSteps[]  = {1, 2, 4, 6, 8, 10, 12, 16};
static constexpr int kNumVoiceSteps = 8;
#endif

static constexpr float kTargetLoad = 70.f; // usable, leaves room for the UI
static constexpr float kCliffLoad  = 90.f; // stop climbing

DaisySeed    hw;
CpuLoadMeter meter;

// --- the voice under test ---------------------------------------------------

struct BenchVoice
{
    Engine eng;
    Svf    svf;
    Adsr   amp_env;
    Adsr   filt_env;
    float  freq = 110.f;
    bool   gate = false;

    void Init(EngineId id, float f)
    {
        freq = f;
        eng.Init(id, kSampleRate);
        eng.SetParams(freq, 0.5f, 0.5f, 0.5f);
        svf.Init(kSampleRate);
        svf.SetRes(0.4f);
        svf.SetDrive(0.2f);
        // Amp envelope runs per sample; the filter envelope runs per block,
        // which is how the real firmware will do it (a per-sample SetFreq costs
        // a tanf per voice per sample and buys nothing audible).
        amp_env.Init(kSampleRate, 1);
        amp_env.SetAttackTime(0.01f);
        amp_env.SetDecayTime(0.2f);
        amp_env.SetSustainLevel(0.7f);
        amp_env.SetReleaseTime(0.4f);
        filt_env.Init(kSampleRate, static_cast<int>(kBlockSize));
        filt_env.SetAttackTime(0.02f);
        filt_env.SetDecayTime(0.3f);
        filt_env.SetSustainLevel(0.5f);
        filt_env.SetReleaseTime(0.5f);
    }

    void RenderAdd(float* out, size_t size)
    {
        // Control rate: filter cutoff from the filter envelope, once a block.
        const float fe = filt_env.Process(gate);
        svf.SetFreq(fclamp(300.f + fe * 6000.f, 20.f, 12000.f));

        for(size_t i = 0; i < size; i++)
        {
            const float e = amp_env.Process(gate);
            svf.Process(eng.Process());
            out[i] += svf.Low() * e * 0.15f;
        }
    }
};

BenchVoice   voices[kMaxVoices];
TextureChain chain; // DTCM: the slots' small state, voices beside it

// --- sweep state ------------------------------------------------------------

volatile int      g_active_voices = 0;
volatile bool     g_fx_on         = false;
volatile bool     g_overload      = false;
volatile uint32_t g_blocks        = 0; // audio blocks rendered; our clock

static uint32_t g_block_budget = 0; // System ticks available per block
static int      g_over_blocks  = 0; // consecutive blocks that ran over

// A block that overruns starves everything below the audio interrupt --
// including SysTick, which is what System::GetNow() and System::Delay() count.
// So an overloaded step freezes the main thread mid-measurement and the sweep
// dies with the chord still sounding. (That is exactly what happened on the
// first hardware run.) Two consequences for this harness:
//
//   1. Timing comes from the audio callback, not from SysTick: g_blocks is
//      incremented per block, and at 48 samples / 48 kHz one block is one
//      millisecond exactly, so block counts ARE milliseconds.
//   2. The callback polices itself. Sixteen consecutive over-budget blocks and
//      it drops every voice, which frees the CPU, unwedges the main thread and
//      lets the sweep record the step as OVER and move on.
static constexpr int kOverBudgetPct   = 95;
static constexpr int kOverBlocksPanic = 16;

static float g_mix[kBlockSize];

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    const uint32_t t_start = System::GetTick();
    meter.OnBlockStart();

    for(size_t i = 0; i < size; i++)
        g_mix[i] = 0.f;

    const int n = g_active_voices;
    for(int v = 0; v < n; v++)
        voices[v].RenderAdd(g_mix, size);

    for(size_t i = 0; i < size; i++)
    {
        out[0][i] = g_mix[i];
        out[1][i] = g_mix[i];
    }
    if(g_fx_on)
        chain.ProcessBlock(out[0], out[1], size);

    meter.OnBlockEnd();

    // Self-policing. Unsigned subtraction, so the free-running tick wrapping is
    // harmless.
    const uint32_t dt = System::GetTick() - t_start;
    if(dt > (g_block_budget / 100) * kOverBudgetPct)
    {
        if(++g_over_blocks >= kOverBlocksPanic)
        {
            g_active_voices = 0; // panic: give the CPU back
            g_overload      = true;
        }
    }
    else
    {
        g_over_blocks = 0;
    }

    g_blocks++;
}

/** Wait `n` audio blocks (= n ms), aborting early on overload. Deliberately
 *  does NOT use System::Delay -- see the note above. */
static void WaitBlocks(uint32_t n)
{
    const uint32_t start = g_blocks;
    while((g_blocks - start) < n && !g_overload)
        hw.SetLed(((g_blocks / 100) % 2) == 0);
}

/** Start a measurement: (re)build `count` voices of `id` and gate them on as a
 *  spread chord, so every voice is genuinely busy. */
static void StartStep(EngineId id, int count)
{
    g_active_voices = 0; // silence while we rebuild

    // A wide voicing -- low notes make the physical models work hardest, and
    // spreading the pitches keeps any shared-state caching honest.
    static const int kChord[16] = {36, 43, 48, 52, 55, 60, 64, 67,
                                           69, 72, 76, 79, 81, 84, 88, 91};
    for(int v = 0; v < count; v++)
    {
        voices[v].Init(id, mtof(static_cast<float>(kChord[v])));
        voices[v].gate = true;
        voices[v].eng.Trig();
    }
    g_active_voices = count;
}

// ------------------------------------------------------------ FX pass

static void PrintPct(const char* prefix, float v)
{
    hw.PrintLine("%s%d.%d",
                 prefix,
                 static_cast<int>(v),
                 static_cast<int>((v - floorf(v)) * 10.f));
}

static constexpr int kAllSlots = -1;
static constexpr int kNoSlots  = -2;

/** Configure the chain with the audio callback kept out of it. With `only`
 *  >= 0, that slot runs types[only] and the rest are bypassed; kAllSlots runs
 *  all four, kNoSlots bypasses everything. Active slots get their most
 *  expensive settings. */
static void ConfigureChain(const int* types, int only)
{
    g_fx_on = false;
    WaitBlocks(3); // make sure the callback has left the chain
    for(int s = 0; s < TextureChain::kNumSlots; s++)
    {
        const bool on = (only == kAllSlots || s == only);
        chain.SetBypass(s, !on);
        chain.SetType(s, types[s]);
        chain.SetAmount(s, 1.f);
        chain.SetSecondary(s, 0.5f);
        chain.SetOption(s, 0);
    }
    // Space at its heaviest: dense long grains, pitched (every grain is
    // resampled), wide, fed back. For Reverb, Grain = full shimmer.
    Space& sp = chain.space();
    sp.SetFreeze(false);
    sp.SetTime(0.5f);
    sp.SetFeedback(0.8f);
    sp.SetGrain(1.f);
    sp.SetPitch(0.75f); // +12 semitones
    sp.SetSpread(1.f);
    sp.Silence();       // no leftover tail from the previous measurement
    chain.SnapToTargets();
    g_fx_on = true;
}

/** Settle 300 ms, measure 1.2 s. Returns false on overload. */
static bool Measure(float* avg, float* max)
{
    g_overload    = false;
    g_over_blocks = 0;
    WaitBlocks(300);
    meter.Reset();
    WaitBlocks(1200);
    *avg = meter.GetAvgCpuLoad() * 100.f;
    *max = meter.GetMaxCpuLoad() * 100.f;
    return !g_overload;
}

/** Cost of every effect type in isolation, then the worst-case chain. Fills
 *  worst[] with the most expensive type per slot. */
static void RunFxPass(int* worst)
{
    hw.PrintLine("--- FX pass: cost of each effect type (4 x variable-saw) ---");
    hw.PrintLine("FX,slot,type,avg_pct,max_pct,cost_pct");
    StartStep(EngineId::kVariableSaw, 4);

    // Baseline: every slot bypassed -- only the output stage runs.
    int types[TextureChain::kNumSlots] = {};
    ConfigureChain(types, kNoSlots);
    float base = 0.f, base_max = 0.f;
    Measure(&base, &base_max);
    PrintPct("FX,baseline (all bypassed),-,", base);

    for(int s = 0; s < TextureChain::kNumSlots; s++)
    {
        float worst_cost = -1.f;
        for(int t = 0; t < TextureChain::NumTypes(s); t++)
        {
            hw.PrintLine("RUN,FX,%s,%s", TextureChain::SlotName(s), TextureChain::TypeName(s, t));
            types[s] = t;
            ConfigureChain(types, s);
            float avg = 0.f, max = 0.f;
            if(!Measure(&avg, &max))
            {
                hw.PrintLine("FX,%s,%s,OVER,OVER,OVER",
                             TextureChain::SlotName(s),
                             TextureChain::TypeName(s, t));
                worst[s] = t;
                worst_cost = 1e9f;
                continue;
            }
            const float cost = avg - base;
            hw.PrintLine("FX,%s,%s,%d.%d,%d.%d,%d.%d",
                         TextureChain::SlotName(s),
                         TextureChain::TypeName(s, t),
                         static_cast<int>(avg),
                         static_cast<int>((avg - floorf(avg)) * 10.f),
                         static_cast<int>(max),
                         static_cast<int>((max - floorf(max)) * 10.f),
                         static_cast<int>(cost),
                         static_cast<int>((cost - floorf(cost)) * 10.f));
            if(cost > worst_cost)
            {
                worst_cost = cost;
                worst[s]   = t;
            }
        }
        types[s] = worst[s];
    }

    // All four at their most expensive, at once.
    ConfigureChain(worst, kAllSlots);
    float avg = 0.f, max = 0.f;
    const bool ok = Measure(&avg, &max);
    hw.PrintLine("FX,worst chain = %s + %s + %s + %s",
                 TextureChain::TypeName(0, worst[0]),
                 TextureChain::TypeName(1, worst[1]),
                 TextureChain::TypeName(2, worst[2]),
                 TextureChain::TypeName(3, worst[3]));
    if(ok)
    {
        PrintPct("FX,worst chain total load,", avg);
        PrintPct("FX,worst chain cost over baseline,", avg - base);
    }
    else
        hw.PrintLine("FX,worst chain,OVER");
    g_fx_on = false;
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    // SDRAM is up now; build the reverb in its storage, then the chain.
    daisysp::ReverbSc* reverb = new(g_reverb_mem) daisysp::ReverbSc();
    chain.Init(kSampleRate, {g_grain_l, g_grain_r, g_pre_l, g_pre_r, reverb});
    chain.SetVolume(1.f);
    chain.SetTilt(0.5f);

    meter.Init(kSampleRate, static_cast<int>(kBlockSize));

    // Real-time budget for one block, in System ticks (PCLK1*2). The audio
    // callback compares itself against this to detect an overrun.
    g_block_budget = static_cast<uint32_t>(
        static_cast<uint64_t>(System::GetTickFreq()) * kBlockSize
        / static_cast<uint32_t>(kSampleRate));

    hw.StartLog(false);
    System::Delay(1500);

    hw.PrintLine("=== rodeo-002 bench_voices ===");
    hw.PrintLine("sr=48k block=%d, grain buffer %d ms/ch in SDRAM, ReverbSc in SDRAM",
                 static_cast<int>(kBlockSize),
                 static_cast<int>(Space::kGrainLen * 1000 / 48000));
    hw.PrintLine("NOTE: no ADC scan / encoder poll / OLED / MIDI running --");
    hw.PrintLine("      budget ~%d%% load, not 100%%.",
                 static_cast<int>(kTargetLoad));
    hw.StartAudio(AudioCallback);

    int worst[TextureChain::kNumSlots] = {};
    RunFxPass(worst);

    hw.PrintLine("CSV,engine,fx,voices,avg_pct,max_pct");

    // Results: per engine, largest voice count under target / under cliff.
    int best_at_target[2][static_cast<int>(EngineId::kCount)] = {};
    int best_at_cliff[2][static_cast<int>(EngineId::kCount)]  = {};

    for(int pass = 0; pass < 2; pass++)
    {
        if(pass == 1)
            ConfigureChain(worst, kAllSlots); // the worst-case chain
        g_fx_on = (pass == 1);
        hw.PrintLine("--- pass %d: texture chain %s ---",
                     pass,
                     g_fx_on ? "ON (worst case per slot)" : "OFF");

        for(int e = 0; e < static_cast<int>(EngineId::kCount); e++)
        {
            const EngineId id = static_cast<EngineId>(e);

            for(int s = 0; s < kNumVoiceSteps; s++)
            {
                const int count = kVoiceSteps[s];

                // Announce the step BEFORE running it: if a step ever does wedge
                // the board, the log's last line names exactly what was playing.
                hw.PrintLine("RUN,%s,%d,%d",
                             EngineName(id),
                             g_fx_on ? 1 : 0,
                             count);

                g_overload    = false;
                g_over_blocks = 0;
                StartStep(id, count);

                // Settle, then measure. Percussive engines are re-struck during
                // the window so they never decay into near-silence (and cheap
                // idle state) while being measured.
                WaitBlocks(300);
                meter.Reset();
                const uint32_t start   = g_blocks;
                uint32_t       next_tr = start + 400;
                while((g_blocks - start) < 1200 && !g_overload)
                {
                    if(EngineIsPercussive(id) && g_blocks >= next_tr)
                    {
                        for(int v = 0; v < count; v++)
                            voices[v].eng.Trig();
                        next_tr += 400;
                    }
                    hw.SetLed(((g_blocks / 100) % 2) == 0);
                }

                if(g_overload)
                {
                    // Over 100% real-time: the step never produced a usable
                    // reading, and everything above it would be worse.
                    hw.PrintLine("CSV,%s,%d,%d,OVER,OVER",
                                 EngineName(id),
                                 g_fx_on ? 1 : 0,
                                 count);
                    g_active_voices = 0;
                    WaitBlocks(200); // let the panic settle before the next step
                    break;
                }

                const float avg = meter.GetAvgCpuLoad() * 100.f;
                const float max = meter.GetMaxCpuLoad() * 100.f;

                hw.PrintLine("CSV,%s,%d,%d,%d.%d,%d.%d",
                             EngineName(id),
                             g_fx_on ? 1 : 0,
                             count,
                             static_cast<int>(avg),
                             static_cast<int>((avg - floorf(avg)) * 10.f),
                             static_cast<int>(max),
                             static_cast<int>((max - floorf(max)) * 10.f));

                if(avg <= kTargetLoad)
                    best_at_target[pass][e] = count;
                if(avg <= kCliffLoad)
                    best_at_cliff[pass][e] = count;
                if(avg > kCliffLoad)
                    break; // no point climbing further
            }
        }
    }

    g_active_voices = 0;
    hw.PrintLine("");
    hw.PrintLine("=== SUMMARY: max voices (fx on = worst-case chain) ===");
    hw.PrintLine("engine,voices@70pct_fxoff,voices@70pct_fxon,voices@90pct_fxon");
    for(int e = 0; e < static_cast<int>(EngineId::kCount); e++)
    {
        hw.PrintLine("%s,%d,%d,%d",
                     EngineName(static_cast<EngineId>(e)),
                     best_at_target[0][e],
                     best_at_target[1][e],
                     best_at_cliff[1][e]);
    }
    hw.PrintLine("=== done ===");

    while(1)
    {
        hw.SetLed(((System::GetNow() / 500) % 2) == 0);
        System::Delay(50);
    }
}
