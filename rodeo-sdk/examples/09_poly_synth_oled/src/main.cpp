// Rodeo bring-up 09 — 6-voice poly synth with OLED feedback (10 pots + MIDI).
//
// 06_poly_synth proved the synth, 08_oled_i2c proved the screen. This is the
// first example that runs BOTH on one main loop — audio, a 10-pot control
// surface, MIDI, and a 128x64 display, with none of them starving the others.
// That combination is what rodeo-001 needs next (its main.cpp still lists "OLED
// feedback" as a TODO), and the interesting part is the scheduling, not the DSP.
//
//   Voice: identical to 06 — osc1 + osc2 (detuned ~10 cents) -> resonant Svf
//          low-pass (cutoff modulated by the amp envelope, scaled by the
//          Filter-Env-Amt pot) -> Adsr. 6 voices, last-note priority.
//
//   Screen, two modes:
//     - Turning a knob  -> that parameter's name, value, and knob position, for
//                          ~800 ms after you stop moving it.
//     - Otherwise       -> a live oscilloscope of the signal actually going out
//                          the line-out jack, captured from the audio callback.
//     A header line (voice count / waveform / held notes) stays up in both.
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
//   OSC waveform      kOscWave      ch23  J29   <- 06 had Master Volume here
//
// The tenth knob is spent on a 4-way waveform selector (saw / square / tri /
// sine) so the scope has something to show off; master volume becomes the fixed
// kMasterLevel below. Same dumb rodeo-potentiometer satellite on every channel —
// plug ten into J12/J13/J15/J16/J17/J23/J24/J25/J26/J29. The ADC scan is masked
// to exactly these 10 channels so each revisits faster than a full 24-ch scan.
//
// --- Why the display doesn't wreck the knobs --------------------------------
// libDaisy's stock SSD130xI2CTransport sends one 2-byte I2C transaction PER
// DISPLAY BYTE — ~1024 blocking transactions per frame, on the order of 100 ms
// at 400 kHz. Since this same loop pumps the ADC scan and MIDI, that would mean
// ~100 ms of frozen knobs and late notes every frame. So this example uses
// common/oled_ssd130x_bulk.h: one transaction per 128-byte page, plus a yield
// hook that runs PumpIo() between pages. Worst-case stall is one page (~3 ms).
//
//   PASS: chords sound and the scope traces them; every knob pops up its own
//         name and value while you turn it and reverts to the scope ~800 ms
//         after you stop; the waveform pot steps saw/square/tri/sine (audibly
//         AND on the scope) without flickering on the zone boundaries; knobs
//         stay responsive and notes stay prompt while the screen refreshes.
//
//   FAIL modes: blank screen -> the OLED module must supply its own I2C
//         pull-ups (the Rodeo board has none, Issue 2); serial prints
//         "OLED: no ACK" in that case. Sluggish knobs -> SetOledYield() lost.
//
// MIDI IN is USART1 (PB6/D13 TX, PB7/D14 RX) — libDaisy default, no overrides.
// Notes + velocity, pitch bend (+/-2 semitones), and CC 120/123 panic.
//
// Flash: put the Daisy in DFU (hold BOOT, tap RESET), then `make program-dfu`.

#include <cmath>

#include "daisy_seed.h"
#include "daisysp.h"
#include "dev/oled_ssd130x.h" // OLED driver (not pulled in by daisy_seed.h)
#include "board.h"
#include "rodeo_adc.h"
#include "status_led.h"
#include "pot_ui.h"
#include "oled_ssd130x_bulk.h"

using namespace daisy;
using namespace daisysp;

// Bulk transport instead of daisy::SSD130xI2c128x64Driver — see the header
// block above. Swapping in daisy::SSD130xI2CTransport here is a one-line A/B
// test if the display ever misbehaves.
using OledType
    = OledDisplay<SSD130xDriver<128, 64, rodeo::Ssd130xI2cBulkTransport>>;

DaisySeed        hw;
MidiUartHandler  midi;
rodeo::RodeoAdc  adc;
rodeo::StatusLed status;
OledType         display;

// --- Parameters ------------------------------------------------------------
// One flat list: the pot order, the ADC channel each lives on, and the name the
// OLED shows. Keeping them parallel means the popup can't drift out of sync
// with the channel map.
enum Param
{
    kPOsc1,
    kPOsc2,
    kPCutoff,
    kPReso,
    kPEnvAmt,
    kPAttack,
    kPDecay,
    kPSustain,
    kPRelease,
    kPWave,
    kNumParams
};

// Index = chip*12 + AIN; the JST connector each lands on is fixed motherboard
// wiring (see the ch->connector topology in board.h). board.h is generic
// boilerplate and deliberately does NOT assign controls to channels.
static const uint8_t kParamChan[kNumParams]
    = {6, 7, 9, 10, 11, 17, 18, 19, 20, 23};

static const char *const kParamName[kNumParams] = {"OSC1 LEVEL",
                                                   "OSC2 LEVEL",
                                                   "CUTOFF",
                                                   "RESONANCE",
                                                   "FILT ENV",
                                                   "ATTACK",
                                                   "DECAY",
                                                   "SUSTAIN",
                                                   "RELEASE",
                                                   "WAVEFORM"};

// ADC scan mask: only the 10 populated channels this synth uses.
static constexpr uint32_t kUsedMask
    = (1u << 6) | (1u << 7) | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 17)
      | (1u << 18) | (1u << 19) | (1u << 20) | (1u << 23);

// --- Oscillator waveform (the tenth pot) ------------------------------------
enum Wave
{
    kWaveSaw,
    kWaveSquare,
    kWaveTri,
    kWaveSine,
    kNumWaves
};
static const uint8_t kWaveForm[kNumWaves] = {Oscillator::WAVE_POLYBLEP_SAW,
                                             Oscillator::WAVE_POLYBLEP_SQUARE,
                                             Oscillator::WAVE_POLYBLEP_TRI,
                                             Oscillator::WAVE_SIN};
static const char *const kWaveName[kNumWaves] = {"SAW", "SQR", "TRI", "SIN"};

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
    float      freq    = 0.f;  // base pitch (Hz), before bend
    float      env_val = 0.f;  // last env sample, for control-rate filter mod
    uint32_t   age     = 0;    // allocation order, for oldest-voice stealing
};

Voice    voices[kMaxVoices];
uint32_t g_age = 0; // monotonically increasing "voice birth" counter
float    g_sr  = 48000.0f; // set in main(), used to re-init a poisoned filter

// Fixed osc2 detune (~10 cents up) — a touch of width without a detune knob.
static const float kDetuneRatio = 1.005793f; // 2^(10/1200)

// --- Filter stability (do not "tidy" these away) -----------------------------
// DaisySP's Svf is a double-sampled Chamberlin with a cubic drive term
// (band_ -= drive_ * band_^3) meant to tame resonance. That term DIVERGES when
// the filter is driven hard at a high cutoff: the state runs to NaN, and since
// nothing ever clears low_/band_/high_, the voice's filter stays dead until the
// next power cycle. Measured on the host against the vendored DaisySP: with the
// stock drive this voice breaks at a per-osc amplitude of 0.65 -- and it used to
// run at 1.0, i.e. inside the unstable region. Sweeping the cutoff past roughly
// 2-3 kHz with resonance and filter-env up reliably blew it up. (Same bug was in
// 06_poly_synth, fixed there too.)
//
// Two changes give a measured 4.1x margin at no audible cost (worst-case peak
// after the filter went 2.39 -> 2.00):
//   - unity gain staging: two oscillators at 0.5 sum to +/-1.0, not +/-2.0
//   - a gentler drive: SetDrive(0.5) stores pre_drive_ = 0.05 (stock is 0.5)
// SetDrive() only has to be called once -- SetRes() recomputes drive_ from the
// stored pre_drive_, so it stays gentle for the life of the voice.
static constexpr float kOscAmp   = 0.5f;
static constexpr float kSvfDrive = 0.5f;

// Env -> cutoff modulation range (Hz) at full Filter-Env-Amt.
static constexpr float kEnvModHz  = 8000.0f;
// Overall mix headroom for summing up to 6 voices before master volume.
static constexpr float kVoiceGain = 0.20f;
// The tenth pot became the waveform selector, so master volume is fixed here.
static constexpr float kMasterLevel = 0.8f;

// --- Shared control parameters (written in main loop, read in audio ISR) ----
// Aligned 32-bit float access is atomic on the M7; single-writer/single-reader.
float p_osc1 = 0.f, p_osc2 = 0.f;      // osc levels 0..1
float p_cut  = 800.f;                  // filter cutoff (Hz), smoothed
float p_res  = 0.f;                    // resonance 0..~0.92
float p_amt  = 0.f;                    // filter env amount 0..1
float p_a = 0.01f, p_d = 0.1f, p_s = 0.8f, p_r = 0.2f; // amp ADSR
float g_bend = 1.0f;                   // pitch-bend frequency ratio
int   g_wave = kWaveSaw;               // current oscillator waveform

// One-pole smoothing toward a target (control-rate zipper suppression).
static inline void Smooth(float &state, float target)
{
    state += 0.05f * (target - state);
}

// --- Scope capture ----------------------------------------------------------
// The ISR fills this forward and stops when it is full; the main loop reads the
// completed buffer and only then re-arms. One buffer, no tearing, no atomics:
// the ISR never writes while g_cap_ready is true, and the main loop never reads
// while it is false.
//
// 1024 samples = 21 ms of history @ 48 kHz. That is 128 trace points at the
// slowest time base (stride 6 = 768 samples) plus 256 samples of headroom for
// the trigger search.
static constexpr int kCapLen = 1024;
static float         g_cap[kCapLen];
static volatile int  g_cap_idx   = 0;
static volatile bool g_cap_ready = false;

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
    V.env.Retrigger(false); // restart attack (soft) even when stealing a held voice
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

    // Control-rate: push params + per-voice osc/filter/env settings once/block.
    const uint8_t form = kWaveForm[g_wave];
    const float   bend = g_bend;
    for(int i = 0; i < kMaxVoices; ++i)
    {
        Voice &V = voices[i];
        if(V.note < 0)
            continue;
        // Belt and braces: the settings above should keep the Svf in its stable
        // region, but a NaN there is permanent and silent -- and it would also
        // poison the scope trace. Recover instead of leaving the voice dead.
        if(!std::isfinite(V.svf.Low()))
        {
            V.svf.Init(g_sr);
            V.svf.SetDrive(kSvfDrive);
        }
        V.osc1.SetWaveform(form);
        V.osc2.SetWaveform(form);
        V.osc1.SetFreq(V.freq * bend);
        V.osc2.SetFreq(V.freq * bend * kDetuneRatio);
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
        mix       = fclamp(mix * kVoiceGain * kMasterLevel, -1.f, 1.f);
        out[0][n] = mix;
        out[1][n] = mix;

        // Feed the scope with exactly what leaves the callback.
        if(!g_cap_ready)
        {
            g_cap[g_cap_idx] = mix;
            if(++g_cap_idx >= kCapLen)
                g_cap_ready = true;
        }
    }

    // Retire voices whose release has finished (ADSR back to IDLE).
    for(int i = 0; i < kMaxVoices; ++i)
        if(!voices[i].gate && !voices[i].env.IsRunning())
            voices[i].note = -1;
}

// --- MIDI -------------------------------------------------------------------
// Drained both from the main loop and from the OLED yield hook, so a frame in
// flight can't delay a note. Both callers are the main thread, so this is never
// re-entered.
static void DrainMidi()
{
    midi.Listen();
    while(midi.HasEvents())
    {
        MidiEvent ev = midi.PopEvent();
        switch(ev.type)
        {
            case NoteOn: HandleNoteOn(ev.AsNoteOn()); break;
            case NoteOff: HandleNoteOff(ev.AsNoteOff().note); break;
            case PitchBend:
            {
                // value is int16_t, -8192..8191 (MidiEvent.h). +/-2 semitones.
                const float b = static_cast<float>(ev.AsPitchBend().value)
                                / 8192.0f;
                g_bend = powf(2.0f, b * 2.0f / 12.0f);
                break;
            }
            case ChannelMode:
            {
                // NOTE: libDaisy's parser does NOT deliver CC 120-127 as
                // ControlChange — midi_parser.cpp intercepts them and emits
                // ChannelMode instead. A `case ControlChange:` here would
                // silently never fire.
                const ChannelModeEvent cm = ev.AsChannelMode();
                if(cm.event_type == AllNotesOff) // CC 123 — let tails ring
                {
                    for(int i = 0; i < kMaxVoices; ++i)
                        voices[i].gate = false;
                }
                else if(cm.event_type == AllSoundOff) // CC 120 — cut now
                {
                    for(int i = 0; i < kMaxVoices; ++i)
                    {
                        voices[i].gate = false;
                        voices[i].note = -1;
                    }
                }
                break;
            }
            default: break;
        }
    }
}

// The OLED yield hook: runs between display pages so a frame in flight never
// stalls the control surface. Must not touch the display and must not block —
// in particular, no hw.PrintLine() in here.
static void PumpIo()
{
    adc.Update();
    DrainMidi();
}

// --- Screen -----------------------------------------------------------------
static constexpr int kScreenW  = 128;
static constexpr int kScreenH  = 64;
static constexpr int kHeaderH  = 11; // header rows 0..9, separator on row 10
static constexpr int kPlotTop  = 12;
static constexpr int kPlotBot  = kScreenH - 1;
static constexpr int kPlotMid  = (kPlotTop + kPlotBot) / 2;
static constexpr int kPlotHalf = (kPlotBot - kPlotTop) / 2;

// Below this the capture is silence: don't trigger on noise, don't normalize by
// it, just draw a flat line.
static constexpr float kSilenceThresh = 0.002f;

// Pre-rendered scope trace (y per column), rebuilt whenever a capture lands so
// the frame rate is independent of the capture rate.
static uint8_t g_trace[kScreenW];
static bool    g_trace_valid = false;
static float   g_peak_hold   = 0.f;

// Raw (unsmoothed) pot readings — what the knob-watcher looks at, so the popup
// appears on the first move instead of lagging behind the one-pole.
static float            g_raw[kNumParams] = {0.f};
static rodeo::KnobWatch g_knobs;

// Rebuild g_trace from a completed capture, then re-arm the ISR.
static void BuildTrace(float sr)
{
    if(!g_cap_ready)
        return;

    // Peak of the capture, with a slow-release hold so the trace doesn't jump
    // scale between frames.
    float peak = 0.f;
    for(int i = 0; i < kCapLen; ++i)
    {
        const float a = fabsf(g_cap[i]);
        if(a > peak)
            peak = a;
    }
    if(peak > g_peak_hold)
        g_peak_hold = peak; // instant attack
    else
        g_peak_hold += 0.10f * (peak - g_peak_hold); // ~0.2 s release
    // (captures land every kCapLen/sr = 21 ms, so 0.10 is a ~10-capture tail)

    // Time base: fit ~2 cycles of the lowest sounding voice across the width,
    // so the trace shows a musically meaningful shape rather than a fragment.
    float lowest = 0.f;
    for(int i = 0; i < kMaxVoices; ++i)
        if(voices[i].note >= 0 && (lowest == 0.f || voices[i].freq < lowest))
            lowest = voices[i].freq;

    int stride = 3; // ~C4 default when nothing is sounding
    if(lowest > 0.f)
    {
        const float want = (sr / lowest) * 2.0f / static_cast<float>(kScreenW);
        stride           = static_cast<int>(want + 0.5f);
        if(stride < 1)
            stride = 1;
        if(stride > 6)
            stride = 6; // 128*6 = 768 <= kCapLen - trigger margin
    }

    const bool silent = g_peak_hold < kSilenceThresh;

    // Trigger on the first rising zero-crossing so the trace stands still
    // instead of sliding sideways every frame. Skipped in silence, where the
    // only crossings are noise and the trace would jitter convincingly.
    int start = 0;
    if(!silent)
    {
        const int limit = kCapLen - stride * kScreenW;
        for(int i = 1; i < limit; ++i)
        {
            if(g_cap[i - 1] <= 0.f && g_cap[i] > 0.f)
            {
                start = i;
                break;
            }
        }
    }

    const float scale
        = silent ? 0.f
                 : (static_cast<float>(kPlotHalf)
                    / (g_peak_hold > kSilenceThresh ? g_peak_hold : 1.0f));

    for(int x = 0; x < kScreenW; ++x)
    {
        int idx = start + x * stride;
        if(idx >= kCapLen)
            idx = kCapLen - 1;
        int y = kPlotMid - static_cast<int>(g_cap[idx] * scale);
        if(y < kPlotTop)
            y = kPlotTop;
        if(y > kPlotBot)
            y = kPlotBot;
        g_trace[x] = static_cast<uint8_t>(y);
    }
    g_trace_valid = true;

    g_cap_idx   = 0; // re-arm
    g_cap_ready = false;
}

// Format one parameter's current value for the popup. Daisy's logger and
// WriteString have no %f, so everything goes through FixedCapStr.
static void FormatValue(int p, FixedCapStr<20> &s)
{
    switch(p)
    {
        case kPOsc1:
        case kPOsc2:
        case kPEnvAmt:
        case kPSustain:
        case kPReso:
            s.AppendInt(static_cast<int>(g_raw[p] * 100.0f + 0.5f));
            s.Append("%");
            break;
        case kPCutoff:
            s.AppendInt(static_cast<int>(p_cut + 0.5f));
            s.Append(" Hz");
            break;
        case kPAttack:
        case kPDecay:
        case kPRelease:
        {
            const float t = (p == kPAttack) ? p_a : (p == kPDecay) ? p_d : p_r;
            if(t < 1.0f)
            {
                s.AppendInt(static_cast<int>(t * 1000.0f + 0.5f));
                s.Append(" ms");
            }
            else
            {
                s.AppendFloat(t, 2);
                s.Append(" s");
            }
            break;
        }
        case kPWave: s.Append(kWaveName[g_wave]); break;
        default: break;
    }
}

static void DrawHeader()
{
    int live = 0;
    for(int i = 0; i < kMaxVoices; ++i)
        if(voices[i].note >= 0)
            ++live;

    FixedCapStr<40> hdr;
    hdr.AppendInt(live);
    hdr.Append("v ");
    hdr.Append(kWaveName[g_wave]);
    hdr.Append(" ");
    // Up to four held notes — enough to read a chord at a glance.
    int shown = 0;
    for(int i = 0; i < kMaxVoices && shown < 4; ++i)
        if(voices[i].gate)
        {
            hdr.Append(" ");
            hdr.Append(NoteName(static_cast<uint8_t>(voices[i].note)));
            ++shown;
        }

    display.SetCursor(2, 2);
    display.WriteString(hdr, Font_4x6, true);
    display.DrawLine(0, kHeaderH - 1, kScreenW - 1, kHeaderH - 1, true);
}

static void DrawScope()
{
    if(!g_trace_valid)
    {
        display.DrawLine(0, kPlotMid, kScreenW - 1, kPlotMid, true);
        return;
    }
    // Join consecutive points so a steep saw edge reads as a vertical line
    // rather than two isolated pixels.
    for(int x = 1; x < kScreenW; ++x)
        display.DrawLine(static_cast<uint8_t>(x - 1),
                         g_trace[x - 1],
                         static_cast<uint8_t>(x),
                         g_trace[x],
                         true);
}

static void DrawPopup(int p)
{
    display.SetCursor(2, 13);
    display.WriteString(kParamName[p], Font_7x10, true);

    FixedCapStr<20> val;
    FormatValue(p, val);
    display.SetCursor(4, 26);
    display.WriteString(val, Font_11x18, true);

    // Knob position bar, straight from the raw reading.
    display.DrawRect(2, 50, 125, 58, true);
    const int inner = static_cast<int>(g_raw[p] * 121.0f + 0.5f);
    if(inner > 0)
        display.DrawRect(3,
                         51,
                         static_cast<uint8_t>(3 + (inner > 121 ? 121 : inner)),
                         57,
                         true,
                         true);
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
        V.osc1.Init(sr);
        V.osc1.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
        V.osc1.SetAmp(kOscAmp);
        V.osc2.Init(sr);
        V.osc2.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
        V.osc2.SetAmp(kOscAmp);
        V.svf.Init(sr);
        V.svf.SetDrive(kSvfDrive); // see kSvfDrive -- keeps the filter stable
        V.env.Init(sr); // per-sample ADSR (blockSize defaults to 1)
    }

    rodeo::RodeoAdc::Config cfg;
    cfg.populated_mask = kUsedMask; // scan only the 10 live channels
    adc.Init(cfg);

    MidiUartHandler::Config midi_cfg; // defaults: USART1, PB6 TX / PB7 RX
    midi.Init(midi_cfg);
    midi.StartReceive();

    // Same bus contract as 08_oled_i2c: I2C1 on PB8/PB9 @ 0x3C, 400 kHz because
    // the Rodeo board carries no I2C pull-ups (Issue 2) — the module must.
    OledType::Config ocfg;
    auto            &tcfg          = ocfg.driver_config.transport_config;
    tcfg.i2c_config.periph         = I2CHandle::Config::Peripheral::I2C_1;
    tcfg.i2c_config.pin_config.scl = seed::D11; // PB8 — board.h::pin::kI2cScl
    tcfg.i2c_config.pin_config.sda = seed::D12; // PB9 — board.h::pin::kI2cSda
    tcfg.i2c_config.speed          = I2CHandle::Config::Speed::I2C_400KHZ;
    tcfg.i2c_address               = board::i2c::kOledAddr; // 0x3C
    display.Init(ocfg);
    display.Fill(false);
    display.Update();

    // Only now, once the display is up: let frames yield to the control surface.
    rodeo::SetOledYield(PumpIo);

    hw.StartLog(false);
    status.Init();
    hw.StartAudio(AudioCallback);

    hw.PrintLine("=== Rodeo 09_poly_synth_oled (6-voice, 10 pots, OLED) ===");
    hw.PrintLine("U3 (ch0-11):  %s   U4 (ch12-23): %s",
                 adc.ChipPresent(0) ? "present" : "FAILED",
                 adc.ChipPresent(1) ? "present" : "FAILED");
    hw.PrintLine("OLED: %s",
                 rodeo::OledLinkOk() ? "ok @ 0x3C"
                                     : "no ACK (module needs I2C pull-ups?)");
    hw.PrintLine("Pots: OSC1/2 lvl J12/J13, Cutoff J15, Reso J16, FiltEnvAmt "
                 "J17, AmpADSR J23-J26, Waveform J29");
    hw.PrintLine("Play a MIDI keyboard on USART1; sweep the knobs.");

    // Let the scan populate every channel before seeding the knob anchors,
    // otherwise the boot-time zero cache reads as a full-scale move on all ten.
    const uint32_t t_boot = System::GetNow();
    while(System::GetNow() - t_boot < 300)
        PumpIo();
    for(int p = 0; p < kNumParams; ++p)
        g_raw[p] = adc.GetNorm(kParamChan[p]);
    g_knobs.Reset(g_raw, kNumParams);
    g_wave = rodeo::StepWithHysteresis(g_raw[kPWave], kNumWaves, -1);

    static constexpr uint32_t kFrameMs = 50; // ~20 fps
    uint32_t                  last_frame = System::GetNow();

    for(;;)
    {
        PumpIo();

        // Raw readings first — the knob watcher and the waveform selector both
        // want the unsmoothed value.
        for(int p = 0; p < kNumParams; ++p)
            g_raw[p] = adc.GetNorm(kParamChan[p]);

        const uint32_t now    = System::GetNow();
        const bool     popup  = g_knobs.Update(g_raw, kNumParams, now);
        g_wave = rodeo::StepWithHysteresis(g_raw[kPWave], kNumWaves, g_wave);

        // Map the pots -> smoothed control params.
        Smooth(p_osc1, g_raw[kPOsc1]);
        Smooth(p_osc2, g_raw[kPOsc2]);
        Smooth(p_cut, fmap(g_raw[kPCutoff], 40.0f, 12000.0f, Mapping::EXP));
        Smooth(p_res, g_raw[kPReso] * 0.92f);
        Smooth(p_amt, g_raw[kPEnvAmt]);
        // Envelope times: exp map, direct (time zipper is inaudible).
        p_a = fmap(g_raw[kPAttack], 0.001f, 4.0f, Mapping::EXP);
        p_d = fmap(g_raw[kPDecay], 0.001f, 4.0f, Mapping::EXP);
        p_s = g_raw[kPSustain];
        p_r = fmap(g_raw[kPRelease], 0.005f, 6.0f, Mapping::EXP);

        BuildTrace(sr);

        if(now - last_frame >= kFrameMs)
        {
            last_frame = now;
            display.Fill(false);
            DrawHeader();
            if(popup)
                DrawPopup(g_knobs.active);
            else
                DrawScope();
            display.Update(); // 8 pages, PumpIo() between each
        }

        // Status LED lit while any voice is gated on.
        bool any = false;
        for(int i = 0; i < kMaxVoices; ++i)
            any = any || voices[i].gate;
        status.Set(any);
    }
}
