// rodeo-001 — voice firmware (scaffold).
//
// The rodeo-001 INSTRUMENT, built on the generic rodeo-motherboard boilerplate
// (consume-not-copy: this app links the vendored libDaisy/DaisySP and the shared
// common/ drivers via ../../rodeo-motherboard/firmware; it does not fork them).
//
// Voice v2 target = "less-complicated MiniFreak":
//   - 12-voice polyphony
//   - per osc: ONE flat model list (Option A, Derek 2026-08-23) that the
//     Wave-encoder steps through — Saw / Pulse / Triangle / Sine / FM / Formant.
//     The single Timbre pot morphs whatever the active model's main parameter is
//     (PW on Pulse, FM-index on FM, formant-pos on Formant; subtle/no-op on the
//     pure waveforms). Classic subtractive waveforms and the FM/formant engines
//     share one uniform list, MiniFreak-style.
//   - polyphonic SVF (per voice), selectable LP/BP/HP/notch
//   - Filter ADSR + Amp ADSR (independent) + 1 LFO, single selectable destination
//
// SCAFFOLD STATUS — what this first pass wires vs. what is stubbed:
//   DONE : build/consume plumbing; MIDI poly (12 voices); all 22 frozen pots ->
//          params; two independent ADSRs; poly SVF; global LFO -> cutoff; the
//          four subtractive waveforms live via ApplyModel() (Timbre = PW on Pulse).
//   TODO : FM + formant osc engines (their model slots fall back to a base
//          waveform for now); Vowel/formant filter; encoder-hub reads
//          (detune/glide/octave/model-select — so live model cycling waits on the
//          hub, parts ~1 week out); OLED feedback incl. Honey's requirement that
//          model-select surface the live per-model Timbre meaning; QSPI persistence.
//
// MIDI IN is USART1 (PB6/D13 TX, PB7/D14 RX) — libDaisy default, no overrides.
// Flash: put the Daisy in DFU (hold BOOT, tap RESET), then `make program-dfu`.

#include <cmath>

#include "daisy_seed.h"
#include "daisysp.h"
#include "board.h"
#include "rodeo_adc.h"
#include "status_led.h"
#include "control_map.h"

using namespace daisy;
using namespace daisysp;

DaisySeed        hw;
MidiUartHandler  midi;
rodeo::RodeoAdc  adc;
rodeo::StatusLed status;

// --- Voice model ------------------------------------------------------------
static constexpr int kMaxVoices = 12;

// Oscillator model select (Option A locked by Derek 2026-08-23): the Wave-encoder
// (hub slots 7/8) steps through ONE flat list of models — classic subtractive
// waveforms live right alongside the FM/formant engines, MiniFreak-style. The
// single Timbre pot morphs whatever the active model's main parameter is.
// Live now: the four subtractive waveforms (Timbre = PW on Pulse; a gentle
// symmetry/shape morph on Saw/Tri; no-op on Sine). kFm/kFormant are placeholders
// that fall back to their base waveform until the engines are built.
enum class OscModel
{
    kSaw,      // Timbre = (subtle) — WAVE_POLYBLEP_SAW
    kPulse,    // Timbre = pulse width — WAVE_POLYBLEP_SQUARE
    kTriangle, // Timbre = (subtle) — WAVE_POLYBLEP_TRI
    kSine,     // Timbre = (no-op)  — WAVE_SIN
    kFm,       // Timbre = FM index          (TODO — falls back to sine)
    kFormant,  // Timbre = formant position  (TODO — falls back to saw)
    kCount
};

// LFO single destination (no mod matrix). Default = cutoff.
enum class LfoDest
{
    kCutoff,
    kPitch,
    kAmp,
    kTimbre,
    kCount
};

// SVF output mode (Filter Type cycle-button).
enum class FilterType
{
    kLowpass,
    kBandpass,
    kHighpass,
    kNotch,
    kCount
};

// Voice Mode (cycle-button). Poly is live; mono/unison allocation is TODO.
enum class VoiceMode
{
    kPoly,
    kMono,
    kUnison,
    kCount
};

struct Voice
{
    Oscillator osc1, osc2;
    WhiteNoise noise;
    Svf        svf;
    Adsr       filt_env; // -> filter cutoff modulation
    Adsr       amp_env;  // -> amplitude
    int        note         = -1;   // MIDI note, -1 = free
    bool       gate         = false;
    float      vel          = 0.f;  // 0..1 velocity level
    float      freq         = 0.f;  // base pitch (Hz)
    float      filt_env_val = 0.f;  // last filter-env sample, for control-rate mod
    uint32_t   age          = 0;    // allocation order, for oldest-voice stealing
};

Voice    voices[kMaxVoices];
float    g_sr = 48000.0f; // set in main(), used to re-init a poisoned filter
uint32_t g_age = 0; // monotonically increasing "voice birth" counter

// Fixed osc2 detune (~7 cents up) for width until the Spread/Detune controls
// (hub OSC2 Detune + Spread pots) drive it. TODO: wire Spread/hub-detune.
static const float kBaseDetune = 1.004047f; // 2^(7/1200)

// --- Filter stability (do not "tidy" these away) -----------------------------
// DaisySP's Svf is a double-sampled Chamberlin with a cubic drive term
// (band_ -= drive_ * band_^3) meant to tame resonance. That term DIVERGES when
// the filter is driven hard at a high cutoff: the state runs to NaN, and since
// nothing ever clears low_/band_/high_, the voice's filter stays dead until the
// next power cycle. Measured on the host against the vendored DaisySP: with the
// stock drive and three sources summed at unity (+/-3.0 into a filter that
// breaks at 0.65) SEVENTY PERCENT of realistic res/env/pitch settings blew up
// while sweeping the cutoff. Same bug was found and fixed in examples 06 and 09.
//
// Two changes give a measured 3.2x margin:
//   - unity gain staging: osc1 + osc2 + noise at 1/3 each sum to +/-1.0
//   - a gentler drive: SetDrive(0.5) stores pre_drive_ = 0.05 (stock is 0.5)
// SetDrive() only has to be called once -- SetRes() recomputes drive_ from the
// stored pre_drive_, so it stays gentle for the life of the voice.
static constexpr float kSrcAmp   = 1.0f / 3.0f; // osc1 + osc2 + noise -> unity
static constexpr float kSvfDrive = 0.5f;

// Env -> cutoff modulation range (Hz) at full Filter-Env-Amt.
static constexpr float kEnvModHz = 8000.0f;
// LFO -> cutoff modulation range (Hz) at full LFO Amount (when dest = cutoff).
static constexpr float kLfoCutHz = 4000.0f;
// Overall mix headroom for summing up to 12 voices before master volume.
static constexpr float kVoiceGain = 0.12f;

// --- Shared control parameters (written in main loop, read in audio ISR) ----
// Aligned 32-bit float access is atomic on the M7; single-writer/single-reader.
float p_osc1 = 0.f, p_osc2 = 0.f, p_noise = 0.f; // levels 0..1
float p_tim1 = 0.5f, p_tim2 = 0.5f;              // per-osc Timbre 0..1 (meaning = active model)
float p_cut  = 800.f;                            // filter cutoff (Hz), smoothed
float p_res  = 0.f;                              // resonance 0..~0.92
float p_amt  = 0.f;                              // filter env amount 0..1
float p_fa = 0.01f, p_fd = 0.1f, p_fs = 0.8f, p_fr = 0.2f; // filter ADSR
float p_aa = 0.01f, p_ad = 0.1f, p_as = 0.8f, p_ar = 0.2f; // amp ADSR
float p_lfo_amt = 0.f;                           // LFO amount 0..1
float p_master  = 0.f;                           // master volume 0..1

// Discrete state — osc model select is driven by the Wave-encoder (hub slots
// 7/8); the hub read is still stubbed (parts ~1 week out), so these hold their
// default until it lands. The rest are driven live by the DIG_2..6 cycle-buttons.
OscModel   g_model1 = OscModel::kSaw; // classic default; encoder steps the list
OscModel   g_model2 = OscModel::kSaw;
FilterType g_filter    = FilterType::kLowpass;
LfoDest    g_lfo_dest  = LfoDest::kCutoff;
VoiceMode  g_voice_mode = VoiceMode::kPoly; // cycled by button; poly live, mono/unison TODO
bool       g_sync       = false;            // OSC2 hard-sync; toggled by hub slot-6 press (hub read + DSP TODO)
float      g_keytrack   = 0.f;              // 0 / 0.5 / 1.0 filter keytrack amount

// LFO waveform options cycled by the LFO Shape button.
const uint8_t kLfoWaves[]
    = {Oscillator::WAVE_TRI, Oscillator::WAVE_SIN, Oscillator::WAVE_SAW,
       Oscillator::WAVE_SQUARE, Oscillator::WAVE_RAMP};
int g_lfo_wave_idx = 0;

// Filter keytrack reference: no tracking at middle C, ± from there.
static const float kKeytrackRefHz = 261.63f; // mtof(60)

// Global LFO — one per instrument, single destination. Run at control rate
// (once per audio block), so init it with the control-rate sample rate.
Oscillator g_lfo;
float      g_lfo_val = 0.f; // latest bipolar LFO sample (-1..1)

// One-pole smoothing toward a target (control-rate zipper suppression).
static inline void Smooth(float &state, float target)
{
    state += 0.05f * (target - state);
}

// --- Cycle-buttons (DIG_2..6, active-low, internal pull-up) ------------------
// rodeo-button = momentary-to-GND with a 100 nF hardware debounce on the
// satellite, so a plain falling-edge detect is sufficient (no SW debounce).
struct Btn
{
    daisy::GPIO gpio;
    bool        prev = true; // idle-high (released) with the internal pull-up

    void Init(daisy::Pin pin)
    {
        gpio.Init(pin, daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::PULLUP);
        prev = true;
    }
    // True once on each press (high->low transition).
    bool Pressed()
    {
        bool now  = gpio.Read();   // true = high = released, false = pressed
        bool edge = prev && !now;  // falling edge = press
        prev      = now;
        return edge;
    }
};

// Order matches rodeo001::Button (kFilterType, kKeytrack, kLfoShape, kLfoDest,
// kVoiceMode) -> DIG_2..6 -> seed::D2..D6.
Btn buttons[static_cast<int>(rodeo001::Button::kCount)];

// Advance an "enum class" value by one, wrapping at kCount.
template <typename E>
static inline E CycleEnum(E v)
{
    return static_cast<E>((static_cast<int>(v) + 1) % static_cast<int>(E::kCount));
}

// --- Voice allocation -------------------------------------------------------
// Priority: a voice already on this note (retrigger), else any free voice,
// else steal the oldest-sounding voice.
static int AllocVoice(int note)
{
    for(int i = 0; i < kMaxVoices; ++i)
        if(voices[i].note == note)
            return i;
    for(int i = 0; i < kMaxVoices; ++i)
        if(voices[i].note < 0)
            return i;
    int oldest = 0;
    for(int i = 1; i < kMaxVoices; ++i)
        if(voices[i].age < voices[oldest].age)
            oldest = i;
    return oldest;
}

// Apply an oscillator model: set its waveform, then morph its main parameter
// with the single Timbre pot (Option A). The four subtractive waveforms are
// live; kFm/kFormant fall back to a base waveform until those engines are built.
static inline void ApplyModel(Oscillator &osc, OscModel model, float timbre)
{
    switch(model)
    {
        case OscModel::kPulse:
            osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SQUARE);
            // Timbre = pulse width 0.05..0.95 (avoid degenerate 0/1 duty).
            osc.SetPw(fmap(timbre, 0.05f, 0.95f));
            break;
        case OscModel::kTriangle:
            osc.SetWaveform(Oscillator::WAVE_POLYBLEP_TRI);
            break;
        case OscModel::kSine:
            osc.SetWaveform(Oscillator::WAVE_SIN);
            break;
        case OscModel::kFm:      // TODO: real FM engine (Timbre = FM index)
            osc.SetWaveform(Oscillator::WAVE_SIN);
            break;
        case OscModel::kFormant: // TODO: real formant engine (Timbre = formant pos)
            osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
            break;
        case OscModel::kSaw:
        default:
            osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
            break;
    }
}

// MIDI note number -> "C4"-style name (note 60 = middle C = C4).
static const char *NoteName(uint8_t note)
{
    static const char *const kNames[12]
        = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static char buf[5];
    int         octave = static_cast<int>(note) / 12 - 1;
    const char *n      = kNames[note % 12];
    int         i      = 0;
    buf[i++]           = n[0];
    if(n[1])
        buf[i++] = n[1];
    if(octave < 0)
        buf[i++] = '-';
    buf[i++] = static_cast<char>('0' + (octave < 0 ? -octave : octave));
    buf[i]   = '\0';
    return buf;
}

void HandleNoteOn(const NoteOnEvent &n)
{
    if(n.velocity == 0) // running-status note-off
    {
        for(int i = 0; i < kMaxVoices; ++i)
            if(voices[i].note == n.note)
                voices[i].gate = false;
        return;
    }
    int    v = AllocVoice(n.note);
    Voice &V = voices[v];
    V.note   = n.note;
    V.gate   = true;
    V.vel    = static_cast<float>(n.velocity) / 127.0f;
    V.freq   = mtof(n.note);
    V.age    = ++g_age;
    V.osc1.SetFreq(V.freq);
    V.osc2.SetFreq(V.freq * kBaseDetune);
    V.filt_env.Retrigger(false);
    V.amp_env.Retrigger(false);
    hw.PrintLine("NoteOn  %-3s vel=%d -> voice %d", NoteName(n.note), n.velocity, v);
}

void HandleNoteOff(uint8_t note)
{
    for(int i = 0; i < kMaxVoices; ++i)
        if(voices[i].note == note && voices[i].gate)
        {
            voices[i].gate = false;
            break;
        }
}

// Select the active SVF output for the current Filter Type.
static inline float SvfOut(Svf &svf, FilterType type)
{
    switch(type)
    {
        case FilterType::kBandpass: return svf.Band();
        case FilterType::kHighpass: return svf.High();
        case FilterType::kNotch:    return svf.Notch();
        case FilterType::kLowpass:
        default:                    return svf.Low();
    }
}

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    (void)in;

    // Control-rate (once per block): advance the global LFO, then push per-voice
    // filter/env settings and compute the modulated cutoff.
    g_lfo_val         = g_lfo.Process();               // -1..1
    float lfo_cut_off = (g_lfo_dest == LfoDest::kCutoff)
                            ? g_lfo_val * p_lfo_amt * kLfoCutHz
                            : 0.f;

    for(int i = 0; i < kMaxVoices; ++i)
    {
        Voice &V = voices[i];
        if(V.note < 0)
            continue;
        V.filt_env.SetTime(ADSR_SEG_ATTACK, p_fa);
        V.filt_env.SetTime(ADSR_SEG_DECAY, p_fd);
        V.filt_env.SetSustainLevel(p_fs);
        V.filt_env.SetTime(ADSR_SEG_RELEASE, p_fr);
        V.amp_env.SetTime(ADSR_SEG_ATTACK, p_aa);
        V.amp_env.SetTime(ADSR_SEG_DECAY, p_ad);
        V.amp_env.SetSustainLevel(p_as);
        V.amp_env.SetTime(ADSR_SEG_RELEASE, p_ar);

        float fc = p_cut + V.filt_env_val * p_amt * kEnvModHz + lfo_cut_off
                   + g_keytrack * (V.freq - kKeytrackRefHz); // filter tracks pitch
        fc       = fclamp(fc, 20.f, 18000.f);
        // Belt and braces: the settings above should keep the Svf in its stable
        // region, but a NaN there is permanent and silent, so recover instead of
        // leaving the voice dead. Costs one compare per voice per block.
        if(!std::isfinite(V.svf.Low()))
        {
            V.svf.Init(g_sr);
            V.svf.SetDrive(kSvfDrive);
        }
        V.svf.SetFreq(fc);
        V.svf.SetRes(p_res);
        ApplyModel(V.osc1, g_model1, p_tim1);
        ApplyModel(V.osc2, g_model2, p_tim2);
    }

    for(size_t n = 0; n < size; ++n)
    {
        float mix = 0.f;
        for(int i = 0; i < kMaxVoices; ++i)
        {
            Voice &V = voices[i];
            if(V.note < 0)
                continue;
            float fe     = V.filt_env.Process(V.gate);
            V.filt_env_val = fe;
            float ae     = V.amp_env.Process(V.gate);
            float s       = V.osc1.Process() * p_osc1 + V.osc2.Process() * p_osc2
                      + V.noise.Process() * p_noise;
            V.svf.Process(s);
            mix += SvfOut(V.svf, g_filter) * ae * V.vel;
        }
        mix       = fclamp(mix * kVoiceGain * p_master, -1.f, 1.f);
        out[0][n] = mix;
        out[1][n] = mix;
    }

    // Retire voices whose amp release has finished (ADSR back to IDLE).
    for(int i = 0; i < kMaxVoices; ++i)
        if(!voices[i].gate && !voices[i].amp_env.IsRunning())
            voices[i].note = -1;
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(board::kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    const float sr = hw.AudioSampleRate();
    g_sr           = sr;
    for(int i = 0; i < kMaxVoices; ++i)
    {
        Voice &V = voices[i];
        // Waveform is set per-model at control rate by ApplyModel(); just init
        // the oscillators here (default model = kSaw).
        V.osc1.Init(sr);
        V.osc1.SetAmp(kSrcAmp);
        V.osc2.Init(sr);
        V.osc2.SetAmp(kSrcAmp);
        V.noise.Init();
        V.noise.SetAmp(kSrcAmp);
        V.svf.Init(sr);
        V.svf.SetDrive(kSvfDrive); // see kSvfDrive -- keeps the filter stable
        V.filt_env.Init(sr);
        V.amp_env.Init(sr);
    }

    // Global LFO runs once per audio block -> control-rate sample rate.
    g_lfo.Init(sr / static_cast<float>(board::kBlockSize));
    g_lfo.SetWaveform(Oscillator::WAVE_TRI);
    g_lfo.SetFreq(2.0f);
    g_lfo.SetAmp(1.0f);

    rodeo::RodeoAdc::Config cfg;
    cfg.populated_mask = rodeo001::kPopulatedMask; // scan the 22 live channels
    adc.Init(cfg);

    MidiUartHandler::Config midi_cfg; // defaults: USART1, PB6 TX / PB7 RX
    midi.Init(midi_cfg);
    midi.StartReceive();

    // Cycle-buttons on DIG_2..6 (Seed pins 3..7), order = rodeo001::Button.
    // DIG_1 / seed::D1 is spare since hard-sync moved to an encoder press. This
    // list binds BY POSITION, so it must track the enum exactly — asserted below.
    const Pin kBtnPins[] = {seed::D2, seed::D3, seed::D4, seed::D5, seed::D6};
    static_assert(sizeof(kBtnPins) / sizeof(kBtnPins[0])
                      == static_cast<size_t>(rodeo001::Button::kCount),
                  "kBtnPins must have one pin per rodeo001::Button");
    for(int i = 0; i < static_cast<int>(rodeo001::Button::kCount); ++i)
        buttons[i].Init(kBtnPins[i]);

    hw.StartLog(false);
    status.Init();
    hw.StartAudio(AudioCallback);

    hw.PrintLine("=== rodeo-001 voice scaffold (12-voice) ===");
    hw.PrintLine("U3 (ch0-11): %s   U4 (ch12-23): %s",
                 adc.ChipPresent(0) ? "present" : "FAILED",
                 adc.ChipPresent(1) ? "present" : "FAILED");
    hw.PrintLine("Play MIDI on USART1; sweep the 22 pots.");

    for(;;)
    {
        adc.Update(); // pump the non-blocking ADC scan

        // --- Map the 22 frozen pots -> smoothed control params ---------------
        using namespace rodeo001::pot;
        Smooth(p_osc1, adc.GetNorm(kOsc1Level));
        Smooth(p_osc2, adc.GetNorm(kOsc2Level));
        Smooth(p_noise, adc.GetNorm(kNoiseLevel));
        Smooth(p_tim1, adc.GetNorm(kOsc1Timbre));
        Smooth(p_tim2, adc.GetNorm(kOsc2Timbre));
        Smooth(p_cut,
               fmap(adc.GetNorm(kCutoff), 40.0f, 12000.0f, Mapping::EXP));
        Smooth(p_res, adc.GetNorm(kResonance) * 0.92f);
        Smooth(p_amt, adc.GetNorm(kFilterEnvAmt));
        Smooth(p_lfo_amt, adc.GetNorm(kLfoAmount));
        Smooth(p_master, adc.GetNorm(kMasterVol));

        // Envelope times: exp map, direct (time zipper is inaudible).
        p_fa = fmap(adc.GetNorm(kFiltEnvA), 0.001f, 4.0f, Mapping::EXP);
        p_fd = fmap(adc.GetNorm(kFiltEnvD), 0.001f, 4.0f, Mapping::EXP);
        p_fs = adc.GetNorm(kFiltEnvS);
        p_fr = fmap(adc.GetNorm(kFiltEnvR), 0.005f, 6.0f, Mapping::EXP);
        p_aa = fmap(adc.GetNorm(kAmpEnvA), 0.001f, 4.0f, Mapping::EXP);
        p_ad = fmap(adc.GetNorm(kAmpEnvD), 0.001f, 4.0f, Mapping::EXP);
        p_as = adc.GetNorm(kAmpEnvS);
        p_ar = fmap(adc.GetNorm(kAmpEnvR), 0.005f, 6.0f, Mapping::EXP);

        // LFO rate 0.05..20 Hz from its pot.
        g_lfo.SetFreq(fmap(adc.GetNorm(kLfoRate), 0.05f, 20.0f, Mapping::EXP));

        // --- Digital cycle-buttons (DIG_2..6; DIG_1 spare since 2026-09-13) ---
        using rodeo001::Button;
        if(buttons[static_cast<int>(Button::kFilterType)].Pressed())
            g_filter = CycleEnum(g_filter);
        if(buttons[static_cast<int>(Button::kKeytrack)].Pressed())
        {
            static const float kKtSteps[] = {0.f, 0.5f, 1.f};
            static int         kt         = 0;
            kt         = (kt + 1) % 3;
            g_keytrack = kKtSteps[kt];
        }
        if(buttons[static_cast<int>(Button::kLfoShape)].Pressed())
        {
            g_lfo_wave_idx = (g_lfo_wave_idx + 1)
                             % static_cast<int>(sizeof(kLfoWaves)
                                                / sizeof(kLfoWaves[0]));
            g_lfo.SetWaveform(kLfoWaves[g_lfo_wave_idx]);
        }
        if(buttons[static_cast<int>(Button::kLfoDest)].Pressed())
            g_lfo_dest = CycleEnum(g_lfo_dest);
        if(buttons[static_cast<int>(Button::kVoiceMode)].Pressed())
            g_voice_mode = CycleEnum(g_voice_mode); // TODO: mono/unison allocation

        // TODO: read encoder-hub (detune/glide/octave/model-select) + drive the
        // OLED (incl. per-model Timbre label on model-select). Wave-encoder slots
        // 7/8 will step g_model1/g_model2 through OscModel once the hub read lands.
        // A press on hub::kOsc2SyncPress (slot 6) toggles g_sync; slot 4 is spare.

        midi.Listen();
        while(midi.HasEvents())
        {
            MidiEvent ev = midi.PopEvent();
            switch(ev.type)
            {
                case NoteOn: HandleNoteOn(ev.AsNoteOn()); break;
                case NoteOff: HandleNoteOff(ev.AsNoteOff().note); break;
                default: break;
            }
        }

        // Status LED lit while any voice is gated on.
        bool any = false;
        for(int i = 0; i < kMaxVoices; ++i)
            any = any || voices[i].gate;
        status.Set(any);
    }
}
