#pragma once
#include <cstdint>
#include "daisysp.h"
#include "engine/patch.h"

// One synth voice: Osc1 + Osc2 -> mix -> resonant low-pass ladder filter ->
// amp VCA, with per-voice filter and amp envelopes.
//
// Per-voice filter (+ per-voice filter envelope) is the agreed change from the
// original global-filter spec: it gives true polyphonic filter sweeps once the
// VoiceManager arrives in M2. In M1 exactly one Voice is used, monophonically.
class Voice
{
  public:
    void Init(float sample_rate);
    void NoteOn(uint8_t note, uint8_t velocity, const Patch& patch);
    void NoteOff();

    float Process(const Patch& patch); // one sample
    bool  IsActive() const;
    uint8_t Note() const { return note_; }

  private:
    daisysp::Oscillator   osc1_;
    daisysp::Oscillator   osc2_;
    daisysp::LadderFilter filter_;
    daisysp::Adsr         amp_env_;
    daisysp::Adsr         filt_env_;

    uint8_t note_     = 0;
    float   velocity_ = 0.0f; // 0..1
    bool    gate_     = false;
};
