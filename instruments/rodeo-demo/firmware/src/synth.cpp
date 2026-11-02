// The sound. See synth.h.
#include "synth.h"

#include <math.h>
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

//------------- A voice -------------//

struct Voice
{
    Oscillator osc[2], sub;
    Svf        filter;
    Adsr       amp, fenv;

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

    float attack = -1.f, release = -1.f; // the envelope times last set

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

void Start(const Params& p, int note, int velocity)
{
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
    float vel    = velocity / 127.f;
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
    while(q_tail != q_head)
    {
        __sync_synchronize();
        Event e = queue[q_tail % kQueue];
        q_tail  = q_tail + 1;
        switch(e.type)
        {
            case Ev::On: Start(p, e.note, e.velocity); break;
            case Ev::Off:
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
                for(auto& v : voices)
                {
                    v.key = false;
                    Release(v);
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
    };
    for(auto& o : v.osc)
    {
        o.SetWaveform(kWave[shape]);
        o.SetPw(shape == kPulse ? 0.25f : 0.5f);
    }
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
} // namespace

const char* ShapeName(int shape)
{
    static const char* const kNames[kShapes]
        = {"saw", "square", "pulse", "triangle", "saw + sub", "square + sub"};
    return shape >= 0 && shape < kShapes ? kNames[shape] : "?";
}

void Init(float sample_rate)
{
    sr = sample_rate;
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
    float env_oct  = p.env_amount * 5.f;
    bool  sub      = p.shape == kSawSub || p.shape == kSquareSub;

    for(size_t i = 0; i < frames; i++)
        left[i] = right[i] = 0.f;

    for(auto& v : voices)
    {
        if(!v.Active())
            continue;
        SetShape(v, p.shape);
        if(v.attack != p.attack || v.release != p.release)
        {
            // each SetTime is an expf, so only when a knob has moved
            v.attack  = p.attack;
            v.release = p.release;
            v.amp.SetTime(ADSR_SEG_ATTACK, p.attack);
            v.amp.SetTime(ADSR_SEG_RELEASE, p.release);
            v.fenv.SetTime(ADSR_SEG_ATTACK, p.attack);
            v.fenv.SetTime(ADSR_SEG_RELEASE, p.release);
        }
        v.filter.SetRes(res);

        float note = v.note + 12.f * p.octave + bend + vib_semi;
        float f    = mtof(note);
        v.osc[0].SetFreq(f * powf(2.f, -p.detune / 24.f));
        v.osc[1].SetFreq(f * powf(2.f, p.detune / 24.f));
        v.sub.SetFreq(f * 0.5f);
        float gl = v.level * (1.f - v.pan) * 2.f, gr = v.level * v.pan * 2.f;

        for(size_t i = 0; i < frames; i++)
        {
            float l = 0.f, r = 0.f;
            if(v.use_osc)
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
            float fe = v.fenv.Process(v.gate);
            if(i % kControl == 0)
            {
                float fc = cutoff * powf(2.f, env_oct * fe);
                v.filter.SetFreq(fc < 18000.f ? fc : 18000.f);
            }
            // One filter, for a mono voice; a stereo sample is filtered on its mid
            // and keeps its side, which is cheaper than two and sounds the same.
            float mid = 0.5f * (l + r), side = 0.5f * (l - r);
            v.filter.Process(mid);
            float a = v.amp.Process(v.gate);
            float m = v.filter.Low() * a;
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
