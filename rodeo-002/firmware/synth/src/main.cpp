// rodeo-002 — the instrument. MIDI in, voices, texture chain, audio out.
//
// FIRST PLAYABLE BUILD. The panel is not wired yet, so every parameter the
// panel will own is driven over MIDI CC in the meantime (map printed at
// startup, and listed below). When the pots and encoders arrive they write the
// same Patch fields, and this CC layer stays as a remote control.
//
//   MIDI IN (TRS, USART1) -> VoiceManager -> TextureChain -> LINE OUT
//
// Polyphony is not fixed: CostModel recomputes it from the measured cost table
// whenever the engine or a slot's type changes, so choosing Reverb can cost
// you voices. The serial log prints the count, the predicted load and the
// measured load side by side -- if those two disagree, the cost table is stale
// and the benchmarks need re-running.
//
// Flash: tap RESET, tap BOOT while the LED pulses, then `make program-dfu`.

#include <cmath>
#include <new>

#include "daisy_seed.h"
#include "daisysp.h"

#include "cost_model.h"
#include "patch.h"
#include "texture_chain.h"
#include "voice.h"

using namespace daisy;
using namespace rodeo002;
using fx::Space;
using fx::TextureChain;

static constexpr size_t kBlockSize  = 48;
static constexpr float  kSampleRate = 48000.f;

// Space's buffers and ReverbSc (~396 KB) in SDRAM; the reverb is constructed
// after hw.Init() brings the SDRAM up.
float DSY_SDRAM_BSS g_grain_l[Space::kGrainLen];
float DSY_SDRAM_BSS g_grain_r[Space::kGrainLen];
float DSY_SDRAM_BSS g_pre_l[Space::kPreLen];
float DSY_SDRAM_BSS g_pre_r[Space::kPreLen];
alignas(daisysp::ReverbSc) static uint8_t DSY_SDRAM_BSS
    g_reverb_mem[sizeof(daisysp::ReverbSc)];

// libDaisy's MIDI handler is ~37 KB -- a 256-entry event queue, each entry
// carrying a sysex buffer -- which does not fit in DTCM beside 8 voices and the
// chain. Its DMA buffer is a pointer to a shared buffer elsewhere, so the
// object itself can live in SDRAM; MIDI arrives far too slowly for the slower
// memory to matter.
alignas(MidiUartHandler) static uint8_t DSY_SDRAM_BSS g_midi_mem[sizeof(MidiUartHandler)];

DaisySeed        hw;
MidiUartHandler* midi = nullptr;
VoiceManager    voices;
TextureChain    chain;
Patch           patch;
Globals         globals;
EngineId        current_engine = EngineId::kVirtualAnalog;

volatile bool     g_voices_locked = false; // engine swap in progress
volatile uint32_t g_blocks        = 0;
volatile uint32_t g_busy_ticks    = 0;
volatile uint32_t g_busy_blocks   = 0;
static uint32_t   g_block_ticks   = 1;

static float g_mix[kBlockSize];

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    const uint32_t t0 = System::GetTick();

    if(g_voices_locked)
        for(size_t i = 0; i < size; i++)
            g_mix[i] = 0.f;
    else
        voices.Render(g_mix, size);

    for(size_t i = 0; i < size; i++)
        out[0][i] = out[1][i] = g_mix[i];
    chain.ProcessBlock(out[0], out[1], size);

    g_busy_ticks += System::GetTick() - t0;
    g_busy_blocks++;
    g_blocks++;
}

// --------------------------------------------------------------- CC map
//
// Mirrors the panel (control map, *Analog*): 70-85 are the main layer in panel
// order, 90-105 the shift layer in the same order, 110+ the discrete choices.

enum : uint8_t
{
    // main layer, panel order
    kCcHarmonics = 70, kCcTimbre, kCcMorph, kCcCharacterAmt,
    kCcCutoff, kCcResonance, kCcSpread, kCcMovementAmt,
    kCcAttack, kCcRelease, kCcVolume, kCcTextureAmt,
    kCcSpaceTime, kCcSpaceFeedback, kCcSpaceGrain, kCcSpaceMix,
    // shift layer, same order
    kCcEnvToTimbre = 90, kCcEnvToMorph, kCcLfoRate, kCcCharacterBias,
    kCcFilterType, kCcFilterEnv, kCcLfoDest, kCcMovementRate,
    kCcDecay, kCcSustain, kCcLfoDepth, kCcTextureTone,
    kCcGrainPitch, kCcGrainSpread, kCcTilt, kCcSpaceDamp,
    // discrete
    kCcEngine = 110, kCcCharacterType, kCcMovementType, kCcTextureType, kCcSpaceType,
    kCcVoiceMode, kCcFreeze, kCcBypassBase // 117-120: one per slot
};

static bool  g_discrete_changed = true;
static float Norm(uint8_t v) { return static_cast<float>(v) / 127.f; }
static int   Step(uint8_t v, int n) { return (static_cast<int>(v) * n) / 128; }

static void HandleCc(uint8_t cc, uint8_t value)
{
    const float f = Norm(value);
    switch(cc)
    {
        case 1: voices.SetModWheel(f); return;
        case 64: voices.SetSustain(value >= 64); return;

        case kCcHarmonics: patch.harmonics = f; return;
        case kCcTimbre: patch.timbre = f; return;
        case kCcMorph: patch.morph = f; return;
        case kCcCharacterAmt: patch.amount[TextureChain::kCharacter] = f; return;
        case kCcCutoff: patch.cutoff = f; return;
        case kCcResonance: patch.resonance = f; return;
        case kCcSpread: patch.spread = f; return;
        case kCcMovementAmt: patch.amount[TextureChain::kMovement] = f; return;
        case kCcAttack: patch.attack = f; return;
        case kCcRelease: patch.release = f; return;
        case kCcVolume: globals.volume = f; return;
        case kCcTextureAmt: patch.amount[TextureChain::kTexture] = f; return;
        case kCcSpaceTime: patch.space_time = f; return;
        case kCcSpaceFeedback: patch.space_feedback = f; return;
        case kCcSpaceGrain: patch.space_grain = f; return;
        case kCcSpaceMix: patch.amount[TextureChain::kSpace] = f; return;

        case kCcEnvToTimbre: patch.env_to_timbre = f; return;
        case kCcEnvToMorph: patch.env_to_morph = f; return;
        case kCcLfoRate: patch.lfo_rate = f; return;
        case kCcCharacterBias: patch.secondary[TextureChain::kCharacter] = f; return;
        case kCcFilterType:
            patch.filter = static_cast<FilterType>(
                Step(value, static_cast<int>(FilterType::kCount)));
            return;
        case kCcFilterEnv: patch.filter_env = f; return;
        case kCcLfoDest:
            patch.lfo_dest
                = static_cast<LfoDest>(Step(value, static_cast<int>(LfoDest::kCount)));
            return;
        case kCcMovementRate: patch.secondary[TextureChain::kMovement] = f; return;
        case kCcDecay: patch.decay = f; return;
        case kCcSustain: patch.sustain = f; return;
        case kCcLfoDepth: patch.lfo_depth = f; return;
        case kCcTextureTone: patch.secondary[TextureChain::kTexture] = f; return;
        case kCcGrainPitch: patch.grain_pitch = f; return;
        case kCcGrainSpread: patch.grain_spread = f; return;
        case kCcTilt: patch.tilt = f; return;
        case kCcSpaceDamp: patch.secondary[TextureChain::kSpace] = f; return;

        case kCcEngine:
            patch.engine = static_cast<EngineId>(Step(value, static_cast<int>(EngineId::kCount)));
            g_discrete_changed = true;
            return;
        case kCcVoiceMode:
            patch.voice_mode
                = static_cast<VoiceMode>(Step(value, static_cast<int>(VoiceMode::kCount)));
            return;
        case kCcFreeze: patch.freeze = value >= 64; return;
        default: break;
    }
    if(cc >= kCcCharacterType && cc <= kCcSpaceType)
    {
        patch.type[cc - kCcCharacterType] = Step(value, 5);
        g_discrete_changed                = true;
    }
    else if(cc >= kCcBypassBase && cc < kCcBypassBase + TextureChain::kNumSlots)
    {
        patch.bypass[cc - kCcBypassBase] = value >= 64;
        g_discrete_changed               = true;
    }
}

static void HandleMidi(MidiEvent m) // libDaisy's accessors are non-const
{
    if(globals.midi_channel > 0 && m.channel != globals.midi_channel - 1)
        return;
    switch(m.type)
    {
        case NoteOn:
        {
            const NoteOnEvent n = m.AsNoteOn();
            voices.NoteOn(n.note, static_cast<float>(n.velocity) / 127.f);
            break;
        }
        case NoteOff: voices.NoteOff(m.AsNoteOff().note); break;
        case ControlChange:
        {
            const ControlChangeEvent c = m.AsControlChange();
            if(c.control_number == 123 || c.control_number == 120)
                voices.AllNotesOff();
            else
                HandleCc(c.control_number, c.value);
            break;
        }
        case PitchBend:
            voices.SetBend(static_cast<float>(m.AsPitchBend().value) / 8192.f
                           * static_cast<float>(patch.bend_range));
            break;
        case ChannelPressure:
            voices.SetAftertouch(static_cast<float>(m.AsChannelPressure().pressure) / 127.f);
            break;
        default: break;
    }
}

/** Push the patch into the voices and the chain. Control rate. */
static void ApplyPatch()
{
    voices.ApplyPatch(patch);
    for(int s = 0; s < TextureChain::kNumSlots; s++)
    {
        chain.SetAmount(s, patch.amount[s]);
        chain.SetSecondary(s, patch.secondary[s]);
        chain.SetOption(s, patch.option[s]);
        chain.SetType(s, patch.type[s]); // crossfades if it changed
        chain.SetBypass(s, patch.bypass[s]);
    }
    Space& sp = chain.space();
    sp.SetTime(patch.space_time);
    sp.SetFeedback(patch.space_feedback);
    sp.SetGrain(patch.space_grain);
    sp.SetPitch(patch.grain_pitch);
    sp.SetSpread(patch.grain_spread);
    sp.SetFreeze(patch.freeze);
    chain.SetTilt(patch.tilt);
    chain.SetVolume(globals.volume);
}

static void WaitBlocks(uint32_t n)
{
    const uint32_t start = g_blocks;
    while((g_blocks - start) < n)
        ;
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    daisysp::ReverbSc* reverb = new(g_reverb_mem) daisysp::ReverbSc();
    chain.Init(kSampleRate, {g_grain_l, g_grain_r, g_pre_l, g_pre_r, reverb});
    voices.Init(kSampleRate);
    ApplyPatch();

    g_block_ticks = static_cast<uint32_t>(
        static_cast<uint64_t>(System::GetTickFreq()) * kBlockSize / 48000u);

    midi = new(g_midi_mem) MidiUartHandler();
    MidiUartHandler::Config midi_cfg; // USART1, libDaisy defaults = the TRS jacks
    midi->Init(midi_cfg);
    midi->StartReceive();

    hw.StartLog(false);
    System::Delay(1000);
    hw.PrintLine("=== rodeo-002 synth ===");
    hw.PrintLine("MIDI IN on the TRS jack. Panel not wired yet -- CC map:");
    hw.PrintLine("  70-85  main layer, panel order (70 harmonics .. 85 space mix)");
    hw.PrintLine("  90-105 shift layer, same order (90 env>timbre .. 105 space damp)");
    hw.PrintLine("  110 engine  111-114 slot types  115 voice mode  116 freeze");
    hw.PrintLine("  117-120 slot bypass   1 mod wheel   64 sustain");

    hw.StartAudio(AudioCallback);

    uint32_t last_apply = 0, last_print = 0;
    while(1)
    {
        midi->Listen();
        while(midi->HasEvents())
            HandleMidi(midi->PopEvent());

        // An engine swap rebuilds every voice, so the callback is kept out of
        // the voices while it happens.
        if(g_discrete_changed)
        {
            g_discrete_changed = false;
            if(patch.engine != current_engine)
            {
                g_voices_locked = true;
                WaitBlocks(2);
                voices.SetEngine(patch.engine);
                current_engine  = patch.engine;
                g_voices_locked = false;
            }
            ApplyPatch();
            // Recompute the cap: an expensive Space type costs voices.
            voices.SetMaxVoices(CostModel::MaxVoices(patch.engine, CostModel::ChainCost(chain)));
        }

        if(g_blocks - last_apply >= 5) // ~200 Hz
        {
            last_apply = g_blocks;
            ApplyPatch();
        }

        if(g_blocks - last_print >= 2000) // every 2 s
        {
            last_print            = g_blocks;
            const uint32_t blocks = g_busy_blocks;
            const float    load   = blocks ? static_cast<float>(g_busy_ticks)
                                            / (static_cast<float>(blocks * g_block_ticks)) * 100.f
                                         : 0.f;
            g_busy_ticks  = 0;
            g_busy_blocks = 0;
            const float chain_cost = CostModel::ChainCost(chain);
            const float predicted
                = CostModel::PredictLoad(patch.engine, voices.ActiveVoices(), chain_cost);
            hw.PrintLine("%s %s | voices %d/%d | chain %s+%s+%s+%s | load %d%% (predicted %d%%)",
                         EngineName(patch.engine),
                         VoiceModeName(patch.voice_mode),
                         voices.ActiveVoices(),
                         voices.GetMaxVoices(),
                         TextureChain::TypeName(0, chain.GetType(0)),
                         TextureChain::TypeName(1, chain.GetType(1)),
                         TextureChain::TypeName(2, chain.GetType(2)),
                         TextureChain::TypeName(3, chain.GetType(3)),
                         static_cast<int>(load),
                         static_cast<int>(predicted));
        }
        hw.SetLed(voices.ActiveVoices() > 0);
    }
}
