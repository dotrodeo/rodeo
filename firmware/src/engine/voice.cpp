#include "engine/voice.h"
#include <math.h>

using namespace daisysp;

void Voice::Init(float sample_rate)
{
    osc1_.Init(sample_rate);
    osc2_.Init(sample_rate);
    filter_.Init(sample_rate);
    filter_.SetFilterMode(LadderFilter::FilterMode::LP24);
    amp_env_.Init(sample_rate);
    filt_env_.Init(sample_rate);
}

void Voice::NoteOn(uint8_t note, uint8_t velocity, const Patch& patch)
{
    note_     = note;
    velocity_ = velocity / 127.0f;

    // Oscillator pitches. Osc2 gets coarse (semitone) + fine (cent) offsets.
    const float n1 = note + patch.osc1_octave * 12.0f;
    const float n2 = note + patch.osc2_octave * 12.0f + patch.osc2_semi
                     + patch.osc2_detune * 0.01f;
    osc1_.SetWaveform(patch.osc1_wave);
    osc2_.SetWaveform(patch.osc2_wave);
    osc1_.SetFreq(mtof(n1));
    osc2_.SetFreq(mtof(n2));

    // Envelope shapes + filter resonance are set on note-on (static patch in M1;
    // recomputed on parameter change once the control surface lands in M4).
    amp_env_.SetAttackTime(patch.amp_a);
    amp_env_.SetDecayTime(patch.amp_d);
    amp_env_.SetSustainLevel(patch.amp_s);
    amp_env_.SetReleaseTime(patch.amp_r);
    filt_env_.SetAttackTime(patch.filt_a);
    filt_env_.SetDecayTime(patch.filt_d);
    filt_env_.SetSustainLevel(patch.filt_s);
    filt_env_.SetReleaseTime(patch.filt_r);
    filter_.SetRes(patch.filt_res);

    gate_ = true;
}

void Voice::NoteOff()
{
    gate_ = false;
}

bool Voice::IsActive() const
{
    return amp_env_.IsRunning();
}

float Voice::Process(const Patch& patch)
{
    const float amp_e  = amp_env_.Process(gate_);
    const float filt_e = filt_env_.Process(gate_);

    const float mix = osc1_.Process() * patch.osc1_level
                      + osc2_.Process() * patch.osc2_level;

    // Envelope-swept cutoff, exponential (in octaves) about the base.
    // exp2f is cheaper than powf(2,x) and this runs per sample per voice.
    const float cutoff = fclamp(
        patch.filt_cutoff * exp2f(patch.filt_env_amount * filt_e),
        20.0f,
        18000.0f);
    filter_.SetFreq(cutoff);
    const float filtered = filter_.Process(mix);

    // Velocity affects amplitude only.
    return filtered * amp_e * velocity_;
}
