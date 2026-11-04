// The sound. See synth.h.
#include "synth.h"

#include <math.h>
#include <string.h>
#include <new>
#include "daisysp.h"
#include "daisysp-lgpl.h"
#include "daisy_seed.h"
#include "samples.h"

using namespace daisysp;

namespace synth
{
namespace
{
float sr = 48000.f;
constexpr size_t kControl = 8; // samples between filter updates

//------------- Notes from the main loop -------------//

// A single-producer, single-consumer queue: the main loop pushes, Process()
// pops. Indices only ever grow; the barrier makes an event's contents visible
// before its index is.
enum class Ev : uint8_t
{
    On,
    Off,
    Sustain,
    Bend,
    AllOff,
    DropSamples,
};
struct Event
{
    Ev    type;
    int   note, velocity;
    float value;
};
constexpr uint32_t kQueue = 64;
Event              queue[kQueue];
volatile uint32_t  q_head = 0, q_tail = 0;

void Push(const Event& e)
{
    if(q_head - q_tail >= kQueue)
        return; // full: a burst of 64 notes inside 1 ms; drop the rest
    queue[q_head % kQueue] = e;
    __sync_synchronize();
    q_head = q_head + 1;
}

inline float SoftClip(float x)
{
    // tanh-like, unity gain near zero, flat at +-1
    if(x > 3.f)
        return 1.f;
    if(x < -3.f)
        return -1.f;
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
}

//------------- A ladder low-pass -------------//

// Four one-pole stages with feedback from the last, solved without a delay in
// the loop (zero-delay feedback), so it tunes and resonates right with no
// oversampling: DaisySP's LadderFilter oversamples 4x and cost about 18 % of the
// CPU a voice. The one nonlinearity is a soft clip where the feedback meets the
// input, which is where a transistor ladder saturates.
struct Ladder
{
    float s[4] = {};
    float G = 0.f, k = 0.f, drive = 1.f;
    bool  two_pole = false; // the 12 dB tap

    void SetFreq(float fc, float rate)
    {
        float g = tanf((float)M_PI * fc / rate);
        G       = g / (1.f + g);
    }
    void SetRes(float res) { k = 4.f * res; } // 4: self-oscillation

    float Process(float x)
    {
        x *= drive;
        float b = 1.f - G, G2 = G * G, G4 = G2 * G2;
        // The last stage's output as it will be, from the input and the states
        float S  = G2 * G * b * s[0] + G2 * b * s[1] + G * b * s[2] + b * s[3];
        float in = x * (1.f + 0.5f * k); // makes up the bass that resonance takes
        float y4 = (G4 * in + S) / (1.f + k * G4);
        float u  = SoftClip(in - k * y4);
        float y  = u, tap = 0.f;
        for(int i = 0; i < 4; i++)
        {
            float v = (y - s[i]) * G;
            y       = v + s[i];
            s[i]    = y + v;
            if(i == 1)
                tap = y;
        }
        return two_pole ? tap : y;
    }
};

//------------- A voice -------------//

struct Voice
{
    Oscillator   osc[2], sub;
    Svf          filter;
    Ladder       ladder;
    Adsr         amp, fenv;

    int      note = -1;
    float    level = 0.f;   // from velocity
    float    pan   = 0.5f;  // 0 left, 1 right
    bool     gate = false;  // key down, or held by the sustain pedal
    bool     key  = false;  // key down
    uint32_t started = 0;   // for stealing the oldest
    bool     use_osc = false, use_sample = false;

    // The sample, if any: an integer frame and a fraction, so a long sample
    // keeps its pitch (a float position loses it after a few seconds).
    const samples::Sample* smp = nullptr;
    uint32_t pos  = 0;
    float    frac = 0.f;
    float    speed = 1.f;
    bool     one_shot = false; // a kit's sample plays to its end whatever the key does

    float pitch = 0.f;  // the note sounding, in semitones: gliding to `note` in mono
    float hp_x = 0.f, hp_y = 0.f; // the high-pass's last input and output

    // The FM shape: phases (0..1) of the body's carrier and modulator, then the
    // tine's; the tine's index, which dies away fast; velocity, 0..1
    float ph[4] = {};
    float tine  = 0.f;
    float vel   = 0.f;

    float env[7] = {-1.f}; // the envelope settings last set, as Envelopes() lists them

    void Init(float rate)
    {
        for(auto& o : osc)
            o.Init(rate);
        sub.Init(rate);
        sub.SetWaveform(Oscillator::WAVE_SIN);
        filter.Init(rate);
        amp.Init(rate);
        fenv.Init(rate);
        fenv.SetTime(ADSR_SEG_DECAY, 0.4f);
        fenv.SetSustainLevel(0.25f);
        amp.SetTime(ADSR_SEG_DECAY, 0.2f);
        amp.SetSustainLevel(0.85f);
    }

    bool Active() const { return note >= 0; }

    // A sample frame, interpolated, or false at its end.
    bool SampleFrame(float& l, float& r)
    {
        if(!smp || pos + 1 >= smp->frames)
            return false;
        const int16_t* d = smp->data;
        float          a, b;
        if(smp->channels == 2)
        {
            a = d[2 * pos] + frac * (d[2 * pos + 2] - d[2 * pos]);
            b = d[2 * pos + 1] + frac * (d[2 * pos + 3] - d[2 * pos + 1]);
        }
        else
        {
            a = b = d[pos] + frac * (d[pos + 1] - d[pos]);
        }
        l = a * (1.f / 32768.f);
        r = b * (1.f / 32768.f);
        frac += speed;
        uint32_t whole = (uint32_t)frac;
        pos += whole;
        frac -= (float)whole;
        return true;
    }
};

Voice    voices[kVoices];
uint32_t note_count = 0;

// A sine table for the FM shape: four sinf a sample for every voice is too dear
constexpr int kSine = 512;
float         sine_table[kSine + 1];
float         tine_fall = 0.f; // the tine's index, times this every sample

inline float Sine(float phase) // phase 0..1
{
    float x = phase * kSine;
    int   i = (int)x;
    return sine_table[i] + (x - i) * (sine_table[i + 1] - sine_table[i]);
}

inline float Wrap(float phase)
{
    phase -= (int)phase;
    return phase < 0.f ? phase + 1.f : phase;
}

// Mono: the keys held, oldest first; the last one sounds
constexpr int kHeld = 16;
int           held[kHeld];
int           n_held  = 0;
bool          in_mono = false;
float    bend       = 0.f; // semitones
bool     sustain    = false;
float    lfo_phase = 0.f, vib_phase = 0.f;

// The effects, built in the SDRAM once that is up (Init(), after hw.Init()):
// ReverbSc's delay lines are about 400 KB, and the chorus's 19 KB would crowd
// DTCM, which also holds the stack.
alignas(ReverbSc) uint8_t reverb_mem[sizeof(ReverbSc)] DSY_SDRAM_BSS;
alignas(Chorus) uint8_t   chorus_mem[sizeof(Chorus)] DSY_SDRAM_BSS;
ReverbSc*                 reverb = nullptr;
Chorus*                   chorus = nullptr;

Voice* Allocate(int note)
{
    for(auto& v : voices) // the same key again: retrigger it
        if(v.note == note)
            return &v;
    for(auto& v : voices)
        if(!v.Active())
            return &v;
    Voice* oldest = nullptr; // steal: the oldest released voice, else the oldest
    for(auto& v : voices)
        if(!v.gate && (!oldest || v.started < oldest->started))
            oldest = &v;
    if(oldest)
        return oldest;
    oldest = &voices[0];
    for(auto& v : voices)
        if(v.started < oldest->started)
            oldest = &v;
    return oldest;
}

bool Mono(const Params& p) { return p.mono && p.layer == Layer::Synth; }

void Forget(int note)
{
    int k = 0;
    for(int i = 0; i < n_held; i++)
        if(held[i] != note)
            held[k++] = held[i];
    n_held = k;
}

// Mono: voice 0 takes every note. A key played while another is held is legato:
// the pitch moves (gliding, if the preset says so) without starting the
// envelopes again.
void MonoStart(int note, int velocity)
{
    Forget(note);
    if(n_held == kHeld)
        Forget(held[0]);
    held[n_held++] = note;

    Voice& v      = voices[0];
    bool   legato = v.Active() && v.gate;
    if(note_count == 0)
        v.pitch = note; // nothing to glide from yet
    v.note    = note;
    v.key     = true;
    v.gate    = true;
    v.use_osc = true;
    v.use_sample = false;
    v.smp        = nullptr;
    if(legato)
        return;
    float vel = velocity / 127.f;
    v.vel     = vel;
    v.level   = 0.2f + 0.8f * vel * vel;
    v.started = ++note_count;
    v.tine    = 0.35f * vel;
    for(auto& x : v.ph)
        x = 0.f; // every FM note starts alike
    v.amp.Retrigger(false);
    v.fenv.Retrigger(false);
}

void Start(const Params& p, int note, int velocity)
{
    if(Mono(p))
        return MonoStart(note, velocity);

    bool want_osc    = p.layer != Layer::Sampler;
    bool want_sample = p.layer != Layer::Synth;
    const samples::Sample* smp = want_sample ? samples::ForNote(p.sound, note) : nullptr;
    if(!want_osc && !smp)
        return; // the sampler alone, and nothing on this key

    Voice& v     = *Allocate(note);
    v.note       = note;
    v.key        = true;
    v.gate       = true;
    v.started    = ++note_count;
    v.pitch      = note;
    float vel    = velocity / 127.f;
    v.vel        = vel;
    v.tine       = 0.35f * vel;
    for(auto& x : v.ph)
        x = 0.f;
    v.level      = 0.2f + 0.8f * vel * vel;
    v.use_osc    = want_osc;
    v.use_sample = smp != nullptr;
    v.smp        = smp;
    v.pos        = 0;
    v.frac       = 0.f;
    if(smp)
    {
        bool kit   = samples::GetSound(p.sound).kit;
        v.one_shot = kit;
        int  from  = kit ? note : note - samples::kRootNote;
        v.speed    = smp->speed * (kit ? 1.f : powf(2.f, (from + 12 * p.octave) / 12.f));
    }
    v.amp.Retrigger(false);
    v.fenv.Retrigger(false);
}

void Release(Voice& v)
{
    v.gate = false;
}

void Take(const Params& p)
{
    if(Mono(p) != in_mono) // into or out of mono: let everything go
    {
        in_mono = Mono(p);
        n_held  = 0;
        for(auto& v : voices)
        {
            v.key = false;
            Release(v);
        }
    }
    while(q_tail != q_head)
    {
        __sync_synchronize();
        Event e = queue[q_tail % kQueue];
        q_tail  = q_tail + 1;
        switch(e.type)
        {
            case Ev::On: Start(p, e.note, e.velocity); break;
            case Ev::Off:
                if(in_mono)
                {
                    Forget(e.note);
                    Voice& v = voices[0];
                    if(v.note != e.note || !v.key)
                        break;
                    if(n_held > 0)
                        v.note = held[n_held - 1]; // back to the key still held
                    else
                    {
                        v.key = false;
                        if(!sustain)
                            Release(v);
                    }
                    break;
                }
                for(auto& v : voices)
                    if(v.note == e.note && v.key)
                    {
                        v.key = false;
                        if(!sustain)
                            Release(v);
                    }
                break;
            case Ev::Sustain:
                sustain = e.value > 0.5f;
                if(!sustain)
                    for(auto& v : voices)
                        if(v.Active() && !v.key)
                            Release(v);
                break;
            case Ev::Bend: bend = e.value; break;
            case Ev::AllOff:
                n_held = 0;
                for(auto& v : voices)
                {
                    v.key = false;
                    Release(v);
                }
                break;
            case Ev::DropSamples:
                for(auto& v : voices)
                {
                    v.use_sample = false;
                    v.smp        = nullptr;
                    if(!v.use_osc)
                        v.gate = false, v.note = -1;
                }
                break;
        }
    }
}

void SetShape(Voice& v, int shape)
{
    static const uint8_t kWave[kShapes] = {
        Oscillator::WAVE_POLYBLEP_SAW, Oscillator::WAVE_POLYBLEP_SQUARE,
        Oscillator::WAVE_POLYBLEP_SQUARE, Oscillator::WAVE_POLYBLEP_TRI,
        Oscillator::WAVE_POLYBLEP_SAW, Oscillator::WAVE_POLYBLEP_SQUARE,
        Oscillator::WAVE_SIN, // kFm: not these oscillators at all
    };
    for(auto& o : v.osc)
    {
        o.SetWaveform(kWave[shape]);
        o.SetPw(shape == kPulse ? 0.25f : 0.5f);
    }
}

} // namespace

const char* ShapeName(int shape)
{
    static const char* const kNames[kShapes]
        = {"saw", "square", "pulse", "triangle", "saw + sub", "square + sub", "fm"};
    return shape >= 0 && shape < kShapes ? kNames[shape] : "?";
}

void Init(float sample_rate)
{
    sr = sample_rate;
    for(int i = 0; i <= kSine; i++)
        sine_table[i] = sinf(2.f * (float)M_PI * i / kSine);
    tine_fall = expf(-1.f / (0.04f * sr)); // a 40 ms fall
    for(int i = 0; i < kVoices; i++)
    {
        voices[i].Init(sr);
        voices[i].pan = i % 2 ? 0.35f : 0.65f; // a little stereo spread
    }
    chorus = new(chorus_mem) Chorus;
    chorus->Init(sr);
    chorus->SetLfoFreq(0.33f, 0.4f);
    chorus->SetLfoDepth(0.6f);
    chorus->SetDelayMs(9.f, 12.f);
    chorus->SetFeedback(0.1f);
    reverb = new(reverb_mem) ReverbSc;
    reverb->Init(sr);
    reverb->SetFeedback(0.86f);
    reverb->SetLpFreq(9000.f);
}

void NoteOn(int note, int velocity)
{
    if(velocity == 0)
        NoteOff(note);
    else
        Push({Ev::On, note, velocity, 0.f});
}
void NoteOff(int note) { Push({Ev::Off, note, 0, 0.f}); }
void Sustain(bool down) { Push({Ev::Sustain, 0, 0, down ? 1.f : 0.f}); }
void PitchBend(float semitones) { Push({Ev::Bend, 0, 0, semitones}); }
void AllNotesOff() { Push({Ev::AllOff, 0, 0, 0.f}); }

void DropSamples()
{
    Push({Ev::DropSamples, 0, 0, 0.f});
    uint32_t start = daisy::System::GetNow();
    while(q_tail != q_head && daisy::System::GetNow() - start < 20)
        ; // Process() takes the queue every block, 1/3 ms
}

int Active()
{
    int n = 0;
    for(auto& v : voices)
        n += v.Active();
    return n;
}

void Process(const Params& p, float* left, float* right, size_t frames)
{
    Take(p);

    // Once a block: what every voice shares.
    float block = frames / sr;
    lfo_phase += 0.35f * block; // the filter sweep, once every ~3 s
    vib_phase += 5.5f * block;
    lfo_phase -= floorf(lfo_phase);
    vib_phase -= floorf(vib_phase);
    float lfo_oct  = p.lfo ? 1.5f * (fabsf(2.f * lfo_phase - 1.f) * 2.f - 1.f) : 0.f;
    float vib_semi = p.lfo ? 0.08f * sinf(2.f * (float)M_PI * vib_phase) : 0.f;
    float cutoff   = 20.f * powf(2.f, p.cutoff * 10.f + lfo_oct); // 20 Hz .. 20 kHz
    float res      = p.resonance * 0.9f;
    bool  fm       = p.shape == kFm;
    float env_oct  = fm ? 0.f : p.env_amount * 5.f; // on FM, pot 3 is the body's brightness
    bool  sub      = p.shape == kSawSub || p.shape == kSquareSub;
    bool  ladder   = p.filter != Filter::Svf;
    float drive    = 1.f + 6.f * p.drive;      // into the state-variable filter
    float drive_out = 1.f / (1.f + 1.5f * p.drive);
    float hp_a     = p.hpf > 0.f ? expf(-2.f * (float)M_PI * p.hpf / sr) : 0.f;
    float glide    = p.glide > 0.f ? expf(-block / p.glide) : 0.f;
    float body_idx = p.env_amount * 0.6f; // cycles of phase, at full velocity
    const float env[7] = {p.attack, p.decay, p.sustain, p.release,
                          p.fattack < 0.f ? p.attack : p.fattack, p.fdecay, p.fsustain};

    for(size_t i = 0; i < frames; i++)
        left[i] = right[i] = 0.f;

    for(auto& v : voices)
    {
        if(!v.Active())
            continue;
        SetShape(v, p.shape);
        if(memcmp(v.env, env, sizeof env))
        {
            // each SetTime is an expf, so only when something has changed
            memcpy(v.env, env, sizeof env);
            v.amp.SetTime(ADSR_SEG_ATTACK, env[0]);
            v.amp.SetTime(ADSR_SEG_DECAY, env[1]);
            v.amp.SetSustainLevel(env[2]);
            v.amp.SetTime(ADSR_SEG_RELEASE, env[3]);
            v.fenv.SetTime(ADSR_SEG_ATTACK, env[4]);
            v.fenv.SetTime(ADSR_SEG_DECAY, env[5]);
            v.fenv.SetSustainLevel(env[6]);
            v.fenv.SetTime(ADSR_SEG_RELEASE, env[3]);
        }
        if(ladder)
        {
            v.ladder.two_pole = p.filter == Filter::Ladder12;
            v.ladder.SetRes(p.resonance * 1.05f); // self-oscillates at the top
            v.ladder.drive = 1.f + 3.f * p.drive;
        }
        else
            v.filter.SetRes(res);

        v.pitch    = v.note + (v.pitch - v.note) * glide;
        float note = v.pitch + 12.f * p.octave + bend + vib_semi;
        float f    = mtof(note);
        float f0 = f * powf(2.f, -p.detune / 24.f), f1 = f * powf(2.f, p.detune / 24.f);
        v.osc[0].SetFreq(f0);
        v.osc[1].SetFreq(f1);
        v.sub.SetFreq(f * 0.5f);
        float key_oct = p.keytrack * (v.pitch - 60.f) / 12.f;
        float d0 = f0 / sr, d1 = f1 / sr, bidx = body_idx * v.vel;
        float gl = v.level * (1.f - v.pan) * 2.f, gr = v.level * v.pan * 2.f;

        for(size_t i = 0; i < frames; i++)
        {
            float fe = v.fenv.Process(v.gate);
            float l = 0.f, r = 0.f;
            if(v.use_osc && fm)
            {
                // The body: 1:1, as bright as the filter envelope says. The tine:
                // the second (detuned) carrier, modulated at 14:1 for an instant.
                float body = Sine(Wrap(v.ph[0] + bidx * fe * Sine(v.ph[1])));
                float tine = Sine(Wrap(v.ph[2] + v.tine * Sine(v.ph[3])));
                v.tine *= tine_fall;
                v.ph[0] = Wrap(v.ph[0] + d0);
                v.ph[1] = Wrap(v.ph[1] + d0);
                v.ph[2] = Wrap(v.ph[2] + d1);
                v.ph[3] = Wrap(v.ph[3] + 14.f * d1);
                l = r = 0.35f * body + 0.15f * tine; // as loud as the saws through the filter
            }
            else if(v.use_osc)
            {
                float o = 0.5f * (v.osc[0].Process() + v.osc[1].Process());
                if(sub)
                    o = 0.7f * o + 0.5f * v.sub.Process();
                l = r = o;
            }
            if(v.use_sample)
            {
                float sl, sr_;
                if(v.SampleFrame(sl, sr_))
                {
                    l += sl;
                    r += sr_;
                }
                else
                {
                    v.use_sample = false; // the sample has ended
                    if(!v.use_osc)
                        v.gate = false, v.note = -1;
                }
                if(v.one_shot && !v.use_osc)
                    v.gate = v.use_sample; // a kit hit holds until its sample ends
            }
            if(!v.Active())
                break;

            // The filter follows its envelope every kControl samples: SetFreq
            // is a sinf and a powf, too dear for every sample of every voice.
            if(i % kControl == 0)
            {
                float fc = cutoff * powf(2.f, env_oct * fe + key_oct);
                fc       = fc < 18000.f ? fc : 18000.f;
                if(ladder)
                    v.ladder.SetFreq(fc, sr);
                else
                    v.filter.SetFreq(fc);
            }
            // One filter, for a mono voice; a stereo sample is filtered on its mid
            // and keeps its side, which is cheaper than two and sounds the same.
            float mid = 0.5f * (l + r), side = 0.5f * (l - r);
            if(hp_a > 0.f)
            {
                v.hp_y = hp_a * (v.hp_y + mid - v.hp_x);
                v.hp_x = mid;
                mid    = v.hp_y;
            }
            float lp;
            if(ladder)
                lp = v.ladder.Process(mid);
            else
            {
                v.filter.Process(p.drive > 0.f ? SoftClip(mid * drive) * drive_out : mid);
                lp = v.filter.Low();
            }
            float a = v.amp.Process(v.gate);
            float m = lp * a;
            float s = side * a;
            left[i] += (m + s) * gl;
            right[i] += (m - s) * gr;
        }
        if(v.Active() && !v.amp.IsRunning())
            v.note = -1; // released and silent
    }

    // Effects, and a soft clip at the end so a big chord can't wrap.
    bool  use_chorus = p.fx != Fx::Reverb, use_reverb = p.fx != Fx::Chorus;
    float mix = p.fx_mix;
    for(size_t i = 0; i < frames; i++)
    {
        float l = left[i], r = right[i];
        if(use_chorus)
        {
            chorus->Process(0.5f * (l + r));
            l = l * (1.f - 0.5f * mix) + chorus->GetLeft() * mix;
            r = r * (1.f - 0.5f * mix) + chorus->GetRight() * mix;
        }
        if(use_reverb)
        {
            float wl, wr;
            reverb->Process(l * mix, r * mix, &wl, &wr);
            l += wl;
            r += wr;
        }
        left[i]  = SoftClip(l);
        right[i] = SoftClip(r);
    }
}
} // namespace synth
