// Rodeo bring-up 06 — 6-voice polyphonic subtractive synth (10 live pots).
//
// The first "musical" example: MIDI IN drives a 6-voice poly synth, each voice
// = 2 detuned PolyBLEP saws -> resonant state-variable low-pass -> ADSR. Ten
// panel pots (read through the RodeoAdc scanner over U3/U4) shape the sound in
// real time. Play chords on a MIDI keyboard and sweep the knobs.
//
//   Voice: osc1 + osc2 (osc2 detuned ~10 cents for width) -> Svf low-pass
//          (cutoff modulated by the amp envelope, scaled by the Filter-Env-Amt
//          pot) -> Adsr amplitude. Velocity scales voice level.
//   Poly:  6 voices, last-note-priority stealing (reuse same note, then a free
//          voice, else steal the oldest).
//
//   The filter envelope reuses the amp ADSR *shape* (one ADSR per voice) scaled
//   by the Filter-Env-Amt pot — a deliberate simplification so the whole synth
//   fits in 10 pots (no separate filter-ADSR knobs on this build).
//
// --- The 10 pots (this example's local `chan` map -> JST-SH connector) ------
//   OSC1 level        kMixOsc1      ch6   J12
//   OSC2 level        kMixOsc2      ch7   J13
//   Cutoff            kFilterCutoff ch9   J15
//   Resonance         kFilterReso   ch10  J16
//   Filter Env amount kFilterEnvAmt ch11  J17
//   Amp Env Attack    kAmpEnvA      ch17  J23
//   Amp Env Decay     kAmpEnvD      ch18  J24
//   Amp Env Sustain   kAmpEnvS      ch19  J25
//   Amp Env Release   kAmpEnvR      ch20  J26
//   Master volume     kMasterVolume ch23  J29
// The ADC scan is masked to exactly these 10 channels, so each revisits faster
// than a full 24-channel scan (fewer mux hops). Pots are the identical dumb
// rodeo-potentiometer satellite — plug your 10 into J12/J13/J15/J16/J17/J23/
// J24/J25/J26/J29 and they land on the right parameter.
//
//   PASS: play chords -> up to 6 notes sound at pitch; Cutoff/Resonance sweep
//         audibly; the Amp ADSR shapes note on/off; Filter-Env-Amt opens the
//         filter on attack; Master volume scales output; releasing all keys
//         goes silent.
//
// MIDI IN is USART1 (PB6/D13 TX, PB7/D14 RX) — libDaisy default, no overrides.
// Flash: put the Daisy in DFU (hold BOOT, tap RESET), then `make program-dfu`.

#include "daisy_seed.h"
#include "daisysp.h"
#include "board.h"
#include "rodeo_adc.h"
#include "status_led.h"

using namespace daisy;
using namespace daisysp;

DaisySeed        hw;
MidiUartHandler  midi;
rodeo::RodeoAdc  adc;
rodeo::StatusLed status;

// --- This example's channel map (app-level, local to the demo) --------------
// board.h is generic rodeo-motherboard boilerplate and deliberately does NOT
// assign controls to channels. This bring-up picks its own 10 channels here.
// Index = chip*12 + AIN; the JST connector each lands on is fixed motherboard
// wiring (see the ch->connector topology in board.h).
namespace chan
{
constexpr int kMixOsc1      = 6;   // J12  OSC1 level
constexpr int kMixOsc2      = 7;   // J13  OSC2 level
constexpr int kFilterCutoff = 9;   // J15  Cutoff
constexpr int kFilterReso   = 10;  // J16  Resonance
constexpr int kFilterEnvAmt = 11;  // J17  Filter Env amount
constexpr int kAmpEnvA      = 17;  // J23  Amp Env Attack
constexpr int kAmpEnvD      = 18;  // J24  Amp Env Decay
constexpr int kAmpEnvS      = 19;  // J25  Amp Env Sustain
constexpr int kAmpEnvR      = 20;  // J26  Amp Env Release
constexpr int kMasterVolume = 23;  // J29  Master volume
} // namespace chan

// --- Voice model ------------------------------------------------------------
static constexpr int kMaxVoices = 6;

struct Voice
{
    Oscillator osc1, osc2;
    Svf        svf;
    Adsr       env;
    int        note    = -1;   // MIDI note, -1 = free
    bool       gate    = false;
    float      vel     = 0.f;  // 0..1 velocity level
    float      freq    = 0.f;  // base pitch (Hz)
    float      env_val = 0.f;  // last env sample, for control-rate filter mod
    uint32_t   age     = 0;    // allocation order, for oldest-voice stealing
};

Voice    voices[kMaxVoices];
uint32_t g_age = 0; // monotonically increasing "voice birth" counter

// Fixed osc2 detune (~10 cents up) — a touch of width without a detune knob.
static const float kDetuneRatio = 1.005793f; // 2^(10/1200)

// Env -> cutoff modulation range (Hz) at full Filter-Env-Amt.
static constexpr float kEnvModHz  = 8000.0f;
// Overall mix headroom for summing up to 6 voices before master volume.
static constexpr float kVoiceGain = 0.20f;

// --- Shared control parameters (written in main loop, read in audio ISR) ----
// Aligned 32-bit float access is atomic on the M7; single-writer/single-reader.
float p_osc1 = 0.f, p_osc2 = 0.f;      // osc levels 0..1
float p_cut  = 800.f;                  // filter cutoff (Hz), smoothed
float p_res  = 0.f;                    // resonance 0..~0.92
float p_amt  = 0.f;                    // filter env amount 0..1
float p_a = 0.01f, p_d = 0.1f, p_s = 0.8f, p_r = 0.2f; // amp ADSR
float p_master = 0.f;                  // master volume 0..1

// One-pole smoothing toward a target (control-rate zipper suppression).
static inline void Smooth(float &state, float target)
{
    state += 0.05f * (target - state);
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
    V.osc2.SetFreq(V.freq * kDetuneRatio);
    V.env.Retrigger(false); // restart attack (soft) even when stealing a held voice
    hw.PrintLine("NoteOn  %-3s vel=%d  -> voice %d",
                 NoteName(n.note),
                 n.velocity,
                 v);
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

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    (void)in;

    // Control-rate: push params + per-voice filter/env settings once per block.
    for(int i = 0; i < kMaxVoices; ++i)
    {
        Voice &V = voices[i];
        if(V.note < 0)
            continue;
        V.env.SetTime(ADSR_SEG_ATTACK, p_a);
        V.env.SetTime(ADSR_SEG_DECAY, p_d);
        V.env.SetSustainLevel(p_s);
        V.env.SetTime(ADSR_SEG_RELEASE, p_r);
        float fc = p_cut + V.env_val * p_amt * kEnvModHz; // env opens the filter
        V.svf.SetFreq(fc);
        V.svf.SetRes(p_res);
    }

    for(size_t n = 0; n < size; ++n)
    {
        float mix = 0.f;
        for(int i = 0; i < kMaxVoices; ++i)
        {
            Voice &V = voices[i];
            if(V.note < 0)
                continue;
            float e   = V.env.Process(V.gate);
            V.env_val = e;
            float s   = V.osc1.Process() * p_osc1 + V.osc2.Process() * p_osc2;
            V.svf.Process(s);
            mix += V.svf.Low() * e * V.vel;
        }
        mix           = fclamp(mix * kVoiceGain * p_master, -1.f, 1.f);
        out[0][n]     = mix;
        out[1][n]     = mix;
    }

    // Retire voices whose release has finished (ADSR back to IDLE).
    for(int i = 0; i < kMaxVoices; ++i)
        if(!voices[i].gate && !voices[i].env.IsRunning())
            voices[i].note = -1;
}

// ADC scan mask: only the 10 populated channels this synth uses.
static constexpr uint32_t kUsedMask
    = (1u << chan::kMixOsc1) | (1u << chan::kMixOsc2)
      | (1u << chan::kFilterCutoff) | (1u << chan::kFilterReso)
      | (1u << chan::kFilterEnvAmt) | (1u << chan::kAmpEnvA)
      | (1u << chan::kAmpEnvD) | (1u << chan::kAmpEnvS)
      | (1u << chan::kAmpEnvR) | (1u << chan::kMasterVolume);

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(board::kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    const float sr = hw.AudioSampleRate();
    for(int i = 0; i < kMaxVoices; ++i)
    {
        Voice &V = voices[i];
        V.osc1.Init(sr);
        V.osc1.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
        V.osc1.SetAmp(1.0f);
        V.osc2.Init(sr);
        V.osc2.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
        V.osc2.SetAmp(1.0f);
        V.svf.Init(sr);
        V.env.Init(sr); // per-sample ADSR (blockSize defaults to 1)
    }

    rodeo::RodeoAdc::Config cfg;
    cfg.populated_mask = kUsedMask; // scan only the 10 live channels
    adc.Init(cfg);

    MidiUartHandler::Config midi_cfg; // defaults: USART1, PB6 TX / PB7 RX
    midi.Init(midi_cfg);
    midi.StartReceive();

    hw.StartLog(false);
    status.Init();
    hw.StartAudio(AudioCallback);

    hw.PrintLine("=== Rodeo 06_poly_synth (6-voice, 10 pots) ===");
    hw.PrintLine("U3 (ch0-11):  %s   U4 (ch12-23): %s",
                 adc.ChipPresent(0) ? "present" : "FAILED",
                 adc.ChipPresent(1) ? "present" : "FAILED");
    hw.PrintLine("Pots: OSC1/2 lvl J12/J13, Cutoff J15, Reso J16, FiltEnvAmt "
                 "J17, AmpADSR J23-J26, Master J29");
    hw.PrintLine("Play a MIDI keyboard on USART1; sweep the knobs.");

    for(;;)
    {
        adc.Update(); // pump the non-blocking ADC scan

        // Map the 10 pots -> smoothed control params.
        Smooth(p_osc1, adc.GetNorm(chan::kMixOsc1));
        Smooth(p_osc2, adc.GetNorm(chan::kMixOsc2));
        Smooth(p_cut,
               fmap(adc.GetNorm(chan::kFilterCutoff),
                    40.0f,
                    12000.0f,
                    Mapping::EXP));
        Smooth(p_res, adc.GetNorm(chan::kFilterReso) * 0.92f);
        Smooth(p_amt, adc.GetNorm(chan::kFilterEnvAmt));
        Smooth(p_master, adc.GetNorm(chan::kMasterVolume));
        // Envelope times: exp map, direct (time zipper is inaudible).
        p_a = fmap(adc.GetNorm(chan::kAmpEnvA), 0.001f, 4.0f, Mapping::EXP);
        p_d = fmap(adc.GetNorm(chan::kAmpEnvD), 0.001f, 4.0f, Mapping::EXP);
        p_s = adc.GetNorm(chan::kAmpEnvS);
        p_r = fmap(adc.GetNorm(chan::kAmpEnvR), 0.005f, 6.0f, Mapping::EXP);

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
