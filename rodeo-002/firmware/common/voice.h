// rodeo-002 — the voices and how notes are handed to them.
//
//   engine -> Svf -> amp, with ONE ADSR per voice driving the amp directly and
//   the filter, Timbre and Morph through their amount controls, plus one
//   global LFO with a single destination (concept doc, *The voice*).
//
// Polyphony is not fixed: SetMaxVoices() comes from CostModel, so the count
// falls when an expensive engine or Space type is chosen. Mono and Unison are
// voice modes on top of that.
//
// SVF SAFETY: DaisySP's Svf diverges to NaN when driven hard, and it never
// recovers (see rodeo-motherboard's svf_stability_gtest). The platform's rule
// is to keep the source at +/-1 and call SetDrive(0.5); both are done here.
//
// CONTROL SMOOTHING: the panel's pots are read ~108 times a second, and a raw
// reading applied straight to an engine steps audibly -- worst on Harmonics,
// which sets the virtual-analog sync frequency. The macro parameters therefore
// ramp toward their target once per block (~1 kHz), so what the ear gets is a
// glide between readings rather than a staircase. Measured on hardware
// 2026-09-23: without this, turning a pot is plainly stepped.

#pragma once

#include <cstddef>
#include <cstdint>

#include "cost_model.h"
#include "daisysp.h"
#include "dsp_util.h"
#include "engines.h"
#include "patch.h"

namespace rodeo002
{
// The small helpers (Clamp, ExpMap, TimeCoeff, Lfo) live in the fx namespace.
using fx::Clamp;
using fx::Smoother;
using fx::ExpMap;
using fx::TimeCoeff;

class VoiceManager
{
  public:
    static constexpr int   kMaxVoices = CostModel::kMaxPoly;
    static constexpr float kSvfDrive  = 0.5f; // platform rule; do not raise

    void Init(float sample_rate, size_t block_size = 48)
    {
        sample_rate_ = sample_rate;
        // Control smoothers run once per BLOCK, so their "sample rate" is the
        // block rate. 20 ms is fast enough to feel immediate and slow enough to
        // bridge the ~9 ms between ADC readings.
        const float block_rate = sample_rate / static_cast<float>(block_size);
        sm_harmonics_.Init(block_rate, kSmoothSeconds, 0.5f);
        sm_timbre_.Init(block_rate, kSmoothSeconds, 0.5f);
        sm_morph_.Init(block_rate, kSmoothSeconds, 0.5f);
        sm_cutoff_.Init(block_rate, kSmoothSeconds, 0.8f);
        sm_resonance_.Init(block_rate, kSmoothSeconds, 0.2f);
        sm_spread_.Init(block_rate, kSmoothSeconds, 0.f);
        // ONCE PER BLOCK IS ITS SAMPLE RATE. Initialised at the audio rate it
        // advanced 1/48th as far per tick as it thought, so "20 Hz" was really
        // 0.42 Hz -- measured on the host, 2026-09-24, and the reason the LFO
        // felt stuck in the mud at every setting.
        lfo_.Init(block_rate);
        for(int v = 0; v < kMaxVoices; v++)
        {
            Voice& x = voices_[v];
            x.eng.Init(patch_.engine, sample_rate);
            x.svf.Init(sample_rate);
            x.svf.SetDrive(kSvfDrive);
            x.env.Init(sample_rate, 1);
            x.note = -1;
            x.gate = x.held = false;
            x.vel = x.freq = x.glide_freq = x.env_val = 0.f;
            x.age = 0;
        }
        SetEngine(patch_.engine);
    }

    /** Rebuilds every voice -- allocating, so never from the audio callback.
     *  The caller silences the voices first. */
    void SetEngine(EngineId id)
    {
        patch_.engine = id;
        for(int v = 0; v < kMaxVoices; v++)
        {
            voices_[v].eng.Init(id, sample_rate_);
            voices_[v].note = -1;
            voices_[v].gate = voices_[v].held = false;
            voices_[v].env_val                = 0.f;
        }
    }

    /** From CostModel: how many voices fit beside the current chain. */
    void SetMaxVoices(int n) { max_voices_ = Clampi(n, 1, kMaxVoices); }
    int  GetMaxVoices() const { return max_voices_; }

    /** Voices sounding right now -- for the OLED. */
    int ActiveVoices() const
    {
        int n = 0;
        for(int v = 0; v < max_voices_; v++)
            if(voices_[v].gate || voices_[v].env.IsRunning())
                n++;
        return n;
    }

    /** Control rate, once per block. */
    void ApplyPatch(const Patch& p)
    {
        patch_ = p;
        // Up to 40 Hz: fast enough for the buzzing, sideband end of modulation
        // rather than only slow sweeps. The block rate is 1 kHz, so even the top
        // of the range is 25 ticks a cycle.
        lfo_.SetFreq(ExpMap(p.lfo_rate, 0.05f, 40.f));
        lfo_.SetShape(p.lfo_shape);
        for(int v = 0; v < kMaxVoices; v++)
        {
            Voice& x = voices_[v];
            x.env.SetAttackTime(ExpMap(p.attack, 0.001f, 8.f));
            x.env.SetDecayTime(ExpMap(p.decay, 0.005f, 8.f));
            x.env.SetSustainLevel(p.sustain);
            // ADS (Derek, 2026-09-24): the Decay knob sets the fall time
            // whether or not the key is still down, so there is no separate
            // release. p.release is kept in the patch and moved in step by the
            // Ui, so the stored format is unchanged and a future firmware can
            // separate them again without a version bump.
            x.env.SetReleaseTime(ExpMap(p.decay, 0.005f, 12.f));
        }
        // Targets only: the smoothers walk toward these per block (see Render).
        sm_harmonics_.Set(p.harmonics);
        sm_timbre_.Set(p.timbre);
        sm_morph_.Set(p.morph);
        sm_cutoff_.Set(p.cutoff);
        sm_resonance_.Set(Clamp(p.resonance, 0.f, 0.95f));
        sm_spread_.Set(p.spread);
    }

    /** The smoothed control values actually in use -- for tests and the OLED,
     *  which should show where a parameter IS, not where it is heading. */
    float SmoothedHarmonics() const { return sm_harmonics_.cur; }
    float SmoothedCutoff() const { return sm_cutoff_.cur; }

    // --------------------------------------------------------------- MIDI

    void NoteOn(int note, float velocity)
    {
        if(velocity <= 0.f)
        {
            NoteOff(note);
            return;
        }
        switch(patch_.voice_mode)
        {
            case VoiceMode::kMono: MonoOn(note, velocity); break;
            case VoiceMode::kUnison: UnisonOn(note, velocity); break;
            default: PolyOn(note, velocity); break;
        }
    }

    void NoteOff(int note)
    {
        if(patch_.voice_mode == VoiceMode::kMono)
        {
            MonoOff(note);
            return;
        }
        for(int v = 0; v < kMaxVoices; v++)
            if(voices_[v].note == note && voices_[v].gate)
            {
                voices_[v].held = false;
                if(!sustain_)
                    voices_[v].gate = false;
            }
    }

    void SetSustain(bool on)
    {
        sustain_ = on;
        if(!on)
            for(int v = 0; v < kMaxVoices; v++)
                if(!voices_[v].held)
                    voices_[v].gate = false;
    }

    void AllNotesOff()
    {
        for(int v = 0; v < kMaxVoices; v++)
            voices_[v].gate = voices_[v].held = false;
        note_count_ = 0;
    }

    /** Pitch bend in semitones, mod wheel and aftertouch as 0..1. */
    void SetBend(float semitones) { bend_ = semitones; }
    void SetModWheel(float w) { mod_wheel_ = Clamp(w, 0.f, 1.f); }
    void SetAftertouch(float a) { aftertouch_ = Clamp(a, 0.f, 1.f); }

    /** The LFO's current value, so the chain can take Space-mix modulation. */
    float LfoValue() const { return lfo_value_; }

    // -------------------------------------------------------------- audio

    /** Sums every voice into `out` (mono; the chain makes it stereo). */
    void Render(float* out, size_t n)
    {
        const Patch& p = patch_;

        // One step per block: the pots' ~108 Hz staircase becomes a ramp.
        const float harmonics = sm_harmonics_.Next();
        const float timbre_p  = sm_timbre_.Next();
        const float morph_p   = sm_morph_.Next();
        const float cutoff_p  = sm_cutoff_.Next();
        const float res_p     = sm_resonance_.Next();
        const float spread_p  = sm_spread_.Next();

        // Mod wheel adds to LFO depth; aftertouch pushes Morph.
        const float depth = Clamp(p.lfo_depth + mod_wheel_, 0.f, 1.f);
        const float morph = Clamp(morph_p + aftertouch_ * 0.5f, 0.f, 1.f);
        const float glide_coeff
            = p.glide > 0.001f ? TimeCoeff(ExpMap(p.glide, 0.005f, 2.f), sample_rate_) : 1.f;

        for(size_t i = 0; i < n; i++)
            out[i] = 0.f;

        // The LFO runs once per block at control rate: destinations are all
        // slow, and it keeps the per-voice loop tight.
        float a, b;
        lfo_.Process(0.f, &a, &b);
        lfo_value_        = a * depth;
        const float lfo   = lfo_value_;
        const float pitch_mod = p.lfo_dest == LfoDest::kPitch ? lfo * 2.f : 0.f;
        const float timbre_mod = p.lfo_dest == LfoDest::kTimbre ? lfo * 0.5f : 0.f;
        const float morph_mod  = p.lfo_dest == LfoDest::kMorph ? lfo * 0.5f : 0.f;
        const float cutoff_mod = p.lfo_dest == LfoDest::kCutoff ? lfo * 0.5f : 0.f;
        const float amp_mod    = p.lfo_dest == LfoDest::kAmp ? 1.f - 0.5f * (1.f - lfo) : 1.f;

        for(int v = 0; v < max_voices_; v++)
        {
            Voice& x = voices_[v];
            if(!x.gate && !x.env.IsRunning())
                continue;

            // Glide toward the target pitch (mono/unison mostly).
            x.glide_freq += (x.freq - x.glide_freq) * glide_coeff;

            // Env -> Timbre/Morph are signed: 0.5 is none.
            const float env      = x.env_val;
            const float to_timb  = (p.env_to_timbre - 0.5f) * 2.f;
            const float to_morph = (p.env_to_morph - 0.5f) * 2.f;
            const float timbre
                = Clamp(timbre_p + env * to_timb * 0.5f + timbre_mod, 0.f, 1.f);
            const float morph_v = Clamp(morph + env * to_morph * 0.5f + morph_mod, 0.f, 1.f);
            const float detune  = x.detune * spread_p * 0.06f; // +/- ~1 semitone
            x.eng.SetParams(x.glide_freq * powf(2.f, (bend_ + pitch_mod + detune) / 12.f),
                            harmonics,
                            timbre,
                            morph_v);

            // Cutoff: pot, envelope amount (signed), keytrack, velocity, LFO.
            const float env_amt = (p.filter_env - 0.5f) * 2.f;
            const float keytrk  = (p.keytrack - 0.5f) * 2.f
                                 * (static_cast<float>(x.note - 60) / 24.f);
            x.svf.SetRes(res_p);
            const float cut = Clamp(cutoff_p + env * env_amt + keytrk
                                        + x.vel * p.vel_to_filter * 0.5f + cutoff_mod,
                                    0.f,
                                    1.f);
            x.svf.SetFreq(ExpMap(cut, 30.f, 15000.f));

            const float level = x.vel * amp_mod * kVoiceGain;
            for(size_t i = 0; i < n; i++)
            {
                const float e = x.env.Process(x.gate);
                x.svf.Process(x.eng.Process() * 0.5f); // +/-1 into the filter
                out[i] += Pick(x.svf, p.filter) * e * level;
                if(i == 0)
                    x.env_val = e;
            }
            if(!x.gate && !x.env.IsRunning())
                x.note = -1;
        }
    }

  private:
    struct Voice
    {
        Engine        eng;
        daisysp::Svf  svf;
        daisysp::Adsr env;
        int           note       = -1;
        bool          gate       = false;
        bool          held       = false; // still physically down
        float         vel        = 0.f;
        float         freq       = 0.f;
        float         glide_freq = 0.f;
        float         env_val    = 0.f;
        float         detune     = 0.f;
        uint32_t      age        = 0;
    };

    static int Clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    static float Pick(daisysp::Svf& svf, FilterType t)
    {
        switch(t)
        {
            case FilterType::kBandPass: return svf.Band();
            case FilterType::kHighPass: return svf.High();
            default: return svf.Low();
        }
    }

    void Start(Voice& x, int note, float vel, float detune)
    {
        const float freq = daisysp::mtof(static_cast<float>(note));
        // A voice that is not sounding starts at pitch rather than gliding in.
        if(!x.gate && !x.env.IsRunning())
            x.glide_freq = freq;
        x.note   = note;
        x.freq   = freq;
        x.vel    = vel;
        x.detune = detune;
        x.gate   = true;
        x.held   = true;
        x.age    = ++clock_;
        x.eng.Trig();
    }

    void PolyOn(int note, float vel)
    {
        // Same note first (retrigger), then a free voice, then the oldest.
        int reuse = -1, free_v = -1, oldest = 0;
        for(int v = 0; v < max_voices_; v++)
        {
            if(voices_[v].note == note)
                reuse = v;
            if(free_v < 0 && !voices_[v].gate && !voices_[v].env.IsRunning())
                free_v = v;
            if(voices_[v].age < voices_[oldest].age)
                oldest = v;
        }
        const int v = reuse >= 0 ? reuse : (free_v >= 0 ? free_v : oldest);
        Start(voices_[v], note, vel, 0.f);
    }

    void UnisonOn(int note, float vel)
    {
        // Every available voice on one note, detuned symmetrically.
        const int n = max_voices_ < kUnisonMax ? max_voices_ : kUnisonMax;
        for(int v = 0; v < n; v++)
        {
            const float spread = n > 1 ? (static_cast<float>(v) / (n - 1)) * 2.f - 1.f : 0.f;
            Start(voices_[v], note, vel, spread);
        }
        for(int v = n; v < kMaxVoices; v++)
            voices_[v].gate = false;
    }

    void MonoOn(int note, float vel)
    {
        // Last-note priority, and the note stack remembers what to fall back to.
        if(note_count_ < kNoteStack)
            note_stack_[note_count_++] = note;
        Voice& x        = voices_[0];
        const bool legato = x.gate;
        x.note          = note;
        x.freq          = daisysp::mtof(static_cast<float>(note));
        x.vel           = vel;
        x.held          = true;
        x.age           = ++clock_;
        if(!legato)
        {
            x.glide_freq = x.freq; // first note of a phrase: no glide in
            x.gate       = true;
            x.eng.Trig();
        }
        for(int v = 1; v < kMaxVoices; v++)
            voices_[v].gate = false;
    }

    void MonoOff(int note)
    {
        int n = 0;
        for(int i = 0; i < note_count_; i++)
            if(note_stack_[i] != note)
                note_stack_[n++] = note_stack_[i];
        note_count_ = n;
        if(note_count_ > 0)
        {
            // Fall back to the note still held, gliding if glide is up.
            voices_[0].note = note_stack_[note_count_ - 1];
            voices_[0].freq = daisysp::mtof(static_cast<float>(voices_[0].note));
        }
        else if(!sustain_)
        {
            voices_[0].gate = false;
            voices_[0].held = false;
        }
    }

    static constexpr int   kUnisonMax = 4;
    static constexpr int   kNoteStack = 16;
    static constexpr float kVoiceGain     = 0.25f; // headroom for 8 voices
    static constexpr float kSmoothSeconds = 0.02f; // control ramp

    Voice    voices_[kMaxVoices];
    Patch    patch_;
    Smoother sm_harmonics_, sm_timbre_, sm_morph_, sm_cutoff_, sm_resonance_, sm_spread_;
    fx::Lfo  lfo_;
    float    sample_rate_ = 48000.f;
    int      max_voices_  = kMaxVoices;
    int      note_stack_[kNoteStack] = {};
    int      note_count_  = 0;
    uint32_t clock_       = 0;
    float    bend_ = 0.f, mod_wheel_ = 0.f, aftertouch_ = 0.f, lfo_value_ = 0.f;
    bool     sustain_ = false;
};

} // namespace rodeo002
