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

// ---------------------------------------------------------- analog behaviour
//
// Two things every analog polysynth does for free and a digital one has to be
// told to do. Both are deliberately small: this is meant to read as "alive",
// never as "out of tune".
//
// At NAMESPACE scope rather than inside the class on purpose. A class-static
// constexpr array that is indexed at runtime is odr-used and needs an
// out-of-line definition in C++14; clang optimised the reference away and
// arm-gcc did not, so it built on the host and failed to link for the board.

/** A fixed tuning error per voice, in cents -- what you get from eight
 *  oscillator cards that were never trimmed to each other. Irregular on
 *  purpose: evenly spaced offsets comb rather than thicken. Voice 0 is exactly
 *  in tune, because Mono only ever plays voice 0 and a lead should not be
 *  detuned from the rest of the band. */
constexpr float kVoiceCents[8] = {0.0f, 2.7f, -1.9f, 3.4f, -3.1f, 1.2f, -2.4f, 0.8f};

/** Where each voice sits in the stereo field, before Spread scales it.
 *
 *  THE ORDER IS THE WHOLE POINT. A linear ramp across all eight positions looks
 *  right and is wrong: Poly fills voices from 0 upward, so a four-note chord
 *  takes the first four positions and sits entirely on the left. Measured that
 *  way, a C major 7th came out L=371 against R=101. These alternate outward
 *  from the centre instead, so every EVEN number of voices is balanced and the
 *  first pair is only lightly spread -- a single held note lands 20% off
 *  centre, about a decibel, rather than hard left. */
constexpr float kVoicePan[8] = {-0.20f, 0.20f, -0.55f, 0.55f, -0.80f, 0.80f, -1.00f, 1.00f};

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
        sm_sub_.Init(block_rate, kSmoothSeconds, 0.f);
        // ONCE PER BLOCK IS ITS SAMPLE RATE. Initialised at the audio rate it
        // advanced 1/48th as far per tick as it thought, so "20 Hz" was really
        // 0.42 Hz -- measured on the host, 2026-09-24, and the reason the LFO
        // felt stuck in the mud at every setting.
        lfo_.Init(block_rate);
        for(int v = 0; v < kMaxVoices; v++)
        {
            Voice& x = voices_[v];
            x.eng.Init(patch_.engine, sample_rate);
            x.sub.Init(sample_rate);
            x.sub.SetAmp(1.f);
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
            // release. The stored `release` field was kept in step with it for
            // a while in case the two were ever separated again; nothing read
            // it, and on 2026-10-01 its four bytes became the patch gain.
            x.env.SetReleaseTime(ExpMap(p.decay, 0.005f, 12.f));
        }
        // Targets only: the smoothers walk toward these per block (see Render).
        sm_harmonics_.Set(p.harmonics);
        sm_timbre_.Set(p.timbre);
        sm_morph_.Set(p.morph);
        sm_cutoff_.Set(p.cutoff);
        sm_resonance_.Set(Clamp(p.resonance, 0.f, 0.95f));
        sm_spread_.Set(p.spread);
        sm_sub_.Set(p.sub_level);
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
    /** The mod wheel, for the chain destinations it can be pointed at. */
    float ModWheel() const { return mod_wheel_; }

    // -------------------------------------------------------------- audio

    /** Sums every voice into `l` and `r`.
     *
     *  Stereo at the voice, not just at the chain. Every voice used to land in
     *  the same place and the pair was two copies of one signal; now each voice
     *  sits where PanFor puts it, scaled by Spread, so a held chord opens out
     *  instead of stacking on one point. Spread at 0 is the old mono sum
     *  exactly (Derek, 2026-09-29: "a little more complexity/depth/richness"). */
    void Render(float* l, float* r, size_t n)
    {
        const Patch& p = patch_;

        // One step per block: the pots' ~108 Hz staircase becomes a ramp.
        const float harmonics = sm_harmonics_.Next();
        const float timbre_p  = sm_timbre_.Next();
        const float morph_p   = sm_morph_.Next();
        const float cutoff_p  = sm_cutoff_.Next();
        const float res_p     = sm_resonance_.Next();
        const float spread_p  = sm_spread_.Next();
        const float sub_level = sm_sub_.Next();

        // Mod wheel adds to LFO depth; aftertouch pushes Morph.
        // The mod wheel goes wherever the patch points it. LFO depth is the
        // default and was once the only option.
        const float wheel = mod_wheel_;
        const float depth = Clamp(p.lfo_depth
                                      + (p.mod_dest == ModDest::kLfoDepth ? wheel : 0.f),
                                  0.f,
                                  1.f);
        const float wheel_cutoff = p.mod_dest == ModDest::kCutoff ? wheel * 0.5f : 0.f;
        const float wheel_timbre = p.mod_dest == ModDest::kTimbre ? wheel * 0.5f : 0.f;
        const float wheel_morph  = p.mod_dest == ModDest::kMorph ? wheel * 0.5f : 0.f;
        const float morph = Clamp(morph_p + aftertouch_ * 0.5f + wheel_morph, 0.f, 1.f);
        const float glide_coeff
            = p.glide > 0.001f ? TimeCoeff(ExpMap(p.glide, 0.005f, 2.f), sample_rate_) : 1.f;

        for(size_t i = 0; i < n; i++)
            l[i] = r[i] = 0.f;

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
        const float harm_mod   = p.lfo_dest == LfoDest::kHarmonics ? lfo * 0.5f : 0.f;
        const float res_mod    = p.lfo_dest == LfoDest::kResonance ? lfo * 0.5f : 0.f;

        for(int v = 0; v < max_voices_; v++)
        {
            Voice& x = voices_[v];
            // A VOICE IS ONLY DONE ONCE IT HAS BEEN RELEASED AND FREED, which
            // is what `note < 0` marks (see the end of this loop). Skipping on
            // "silent and not running" alone was a trap: with sustain at zero
            // the envelope reaches IDLE *while the key is still held*, so the
            // note-off then skipped the voice entirely, DaisySP's Adsr never
            // saw the falling edge, and its internal gate_ stayed true for
            // ever. Every later note-on then had no rising edge and produced
            // silence -- the "randomly not responding to keypresses" seen on
            // hardware with Decay at zero (2026-09-24), which no preset change
            // could clear because it is state inside the envelope. Rendering
            // one more block of silence is what lets it wind down properly.
            if(!x.gate && !x.env.IsRunning() && x.note < 0)
                continue;

            // Glide toward the target pitch (mono/unison mostly).
            x.glide_freq += (x.freq - x.glide_freq) * glide_coeff;

            // Env -> Timbre/Morph are signed: 0.5 is none.
            const float env      = x.env_val;
            const float to_timb  = (p.env_to_timbre - 0.5f) * 2.f;
            const float to_morph = (p.env_to_morph - 0.5f) * 2.f;
            const float timbre
                = Clamp(timbre_p + env * to_timb * 0.5f + timbre_mod + wheel_timbre, 0.f, 1.f);
            const float morph_v = Clamp(morph + env * to_morph * 0.5f + morph_mod, 0.f, 1.f);
            // Unison detune. This was 0.06 -- SIX CENTS at full spread, under a
            // comment claiming a semitone, which is why Spread sounded like it
            // did nothing (Derek, 2026-09-22: "not getting a lot of value out of
            // the Spread control"). A quarter tone is what a unison stack wants.
            const float detune = x.detune * spread_p * 0.25f; // +/- 25 cents
            // The voice's own tuning error, plus its wander. Always on, always
            // small: this is what stops eight digital copies of one oscillator
            // summing to one loud oscillator.
            x.drift += (Noise() - x.drift) * kDriftPole;
            const float cents = kVoiceCents[v & 7]
                                + Clamp(x.drift * kDriftGain, -1.f, 1.f) * kDriftCents;
            x.eng.SetParams(x.glide_freq
                                * powf(2.f,
                                       (bend_ + pitch_mod + detune + cents * 0.01f) / 12.f),
                            Clamp(harmonics + harm_mod, 0.f, 1.f),
                            timbre,
                            morph_v);

            // Cutoff: pot, envelope amount (signed), keytrack, velocity, LFO.
            const float env_amt = (p.filter_env - 0.5f) * 2.f;
            const float keytrk  = (p.keytrack - 0.5f) * 2.f
                                 * (static_cast<float>(x.note - 60) / 24.f);
            x.svf.SetRes(Clamp(res_p + res_mod, 0.f, 1.f));
            const float cut = Clamp(cutoff_p + env * env_amt + keytrk
                                        + x.vel * p.vel_to_filter * 0.5f + cutoff_mod
                                        + wheel_cutoff,
                                    0.f,
                                    1.f);
            x.svf.SetFreq(ExpMap(cut, 30.f, 15000.f));

            // The sub tracks the voice exactly -- same glide, bend, detune and
            // drift -- so it stays locked to the note it is thickening rather
            // than beating against it. Only the interval separates them.
            const float sub_semis = SubIntervalSemitones(p.sub_interval);
            x.sub.SetWaveform(SubWaveform(p.sub_wave));
            x.sub.SetFreq(x.glide_freq
                          * powf(2.f,
                                 (bend_ + pitch_mod + detune + cents * 0.01f + sub_semis)
                                     / 12.f));

            const float level = x.vel * amp_mod * kVoiceGain;
            // Equal power, so moving a voice off centre does not change how
            // loud it is: a chord must not get quieter as Spread comes up.
            const float pan = Clamp(PanFor(v) * spread_p, -1.f, 1.f);
            const float gl  = sqrtf(0.5f * (1.f - pan));
            const float gr  = sqrtf(0.5f * (1.f + pan));
            for(size_t i = 0; i < n; i++)
            {
                const float e = x.env.Process(x.gate);
                // Both halved, so the pair reaches the filter at the same +/-1
                // the engine alone used to. The sub adds body without needing
                // the voice to be turned down to make room for it.
                x.svf.Process((x.eng.Process() + x.sub.Process() * sub_level) * 0.5f);
                const float sample = Pick(x.svf, p.filter) * e * level;
                l[i] += sample * gl;
                r[i] += sample * gr;
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
        /** The sub. A plain oscillator, not another Engine: that is the whole
         *  point of it -- a fraction of the cost, so turning it on does not
         *  halve the polyphony. */
        daisysp::Oscillator sub;
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
        /** Slow independent wander, one-pole filtered noise. See kDriftPole. */
        float         drift      = 0.f;
        uint32_t      age        = 0;
    };

    /** The wander on top of kVoiceCents. One pole at block rate, ~0.5 s to
     *  settle: slow enough to be heard as drift rather than as vibrato.
     *  kDriftGain puts the filtered noise back to roughly +/-1 before it is
     *  scaled to cents. These are scalars, so they can stay in the class. */
    static constexpr float kDriftPole  = 0.002f;
    static constexpr float kDriftGain  = 30.f;
    static constexpr float kDriftCents = 3.f;

    /** Cheap uniform noise in -1..1. Not for audio -- this runs once per voice
     *  per block, to move the drift along. */
    float Noise()
    {
        rng_ = rng_ * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(rng_)) * (1.f / 2147483648.f);
    }

    /** Where voice `v` sits, scaled later by Spread. By VOICE, not by pitch:
     *  the voice cards are what is spread out, so a chord opens up while the
     *  stereo image stays unhitched from what you are playing. Mono is centred
     *  -- it only ever runs voice 0. See kVoicePan for why the positions are
     *  in the order they are. */
    float PanFor(int v) const
    {
        if(patch_.voice_mode == VoiceMode::kMono)
            return 0.f;
        return kVoicePan[v & 7];
    }

    /** The sub's waveform, mapped to DaisySP's.
     *
     *  TRIANGLE IS THE NAIVE ONE, NOT POLYBLEP, on purpose. DaisySP's
     *  WAVE_POLYBLEP_TRI is the only waveform built on a leaky integrator, and
     *  `Oscillator::Init` never initialises `last_out_` -- so its first output
     *  is whatever was in that memory, and because the integrator feeds itself,
     *  a NaN there is a NaN for ever. On the board `voices` is a file-scope
     *  object, so BSS zeroing hides it; on the host it produced NaN from the
     *  first sample of two factory presets (2026-10-01). Depending on BSS
     *  zeroing for the correctness of a vendor class is not a thing to rely on.
     *
     *  Nothing is lost. The naive triangle is stateless, it measures within 1%
     *  of the polyblep one's level at every pitch the sub reaches, and a
     *  waveform that mostly runs an octave BELOW the note has no aliasing worth
     *  band-limiting. Saw and square stay polyblep: they are stateless too. */
    static uint8_t SubWaveform(SubWave w)
    {
        switch(w)
        {
            case SubWave::kTriangle: return daisysp::Oscillator::WAVE_TRI;
            case SubWave::kSaw: return daisysp::Oscillator::WAVE_POLYBLEP_SAW;
            case SubWave::kSquare: return daisysp::Oscillator::WAVE_POLYBLEP_SQUARE;
            default: return daisysp::Oscillator::WAVE_SIN;
        }
    }

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

    /** `legato` means a key was still down when this note arrived, so the voice
     *  should slide into it. Everything else starts AT pitch.
     *
     *  It used to decide that for itself, from whether the voice was silent --
     *  which is right in Poly, where a new note almost always lands on a free
     *  voice, and wrong everywhere else. Unison restarts the SAME voices for
     *  every note, so a voice ringing out its release still counted as sounding
     *  and every keypress glided in from the last note (Derek, 2026-09-24). A
     *  stolen Poly voice had the same problem, from an unrelated pitch. */
    void Start(Voice& x, int note, float vel, float detune, bool legato = false)
    {
        const float freq = daisysp::mtof(static_cast<float>(note));
        if(!legato)
            x.glide_freq = freq;
        x.note   = note;
        x.freq   = freq;
        x.vel    = vel;
        x.detune = detune;
        x.gate   = true;
        x.held   = true;
        x.age    = ++clock_;
        // RETRIGGER EXPLICITLY. DaisySP's Adsr only restarts on a rising gate
        // edge (`gate && !gate_`), and there is no edge when a voice is re-used
        // for the same note or stolen while it is still held -- its gate was
        // already high. The envelope then carried on from wherever it was,
        // which is inaudible with a long decay and completely silent with a
        // short one, because it had already collapsed to zero. That is the
        // "randomly not responding to keypresses" seen on hardware with Decay
        // at zero (2026-09-24); it survived a preset change because it is voice
        // state, not patch state. Soft, so a stolen voice ramps from where it
        // is rather than jumping to zero and clicking.
        x.env.Retrigger(false);
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
        // Every available voice on one note, detuned symmetrically. Gliding
        // only if a key was still down -- the same rule Mono plays by.
        const bool legato = voices_[0].gate;
        const int  n      = max_voices_ < kUnisonMax ? max_voices_ : kUnisonMax;
        for(int v = 0; v < n; v++)
        {
            const float spread = n > 1 ? (static_cast<float>(v) / (n - 1)) * 2.f - 1.f : 0.f;
            Start(voices_[v], note, vel, spread, legato);
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
    Smoother sm_sub_;
    uint32_t rng_ = 0x9E3779B9u;
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
