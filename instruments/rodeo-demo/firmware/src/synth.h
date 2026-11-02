// The sound: eight voices shared between a small subtractive synth and the
// sampler, a chorus and a reverb.
//
// A voice is two oscillators (and a sub an octave down, for the shapes that have
// one) or a sample, through a resonant low-pass with its own envelope, then an
// amp envelope. The panel's parameters (Params) mean the same thing whichever is
// playing: a sample is filtered and enveloped just like an oscillator.
//
// Threading: notes arrive in the main loop (MIDI) and sound in the audio
// interrupt. NoteOn() and friends only queue an event; Process() takes them at
// the start of each block. Params are plain values the main loop writes and
// Process() reads once a block.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace synth
{
constexpr int kVoices = 8;

enum class Layer : uint8_t
{
    Synth,
    Sampler,
    Both,
};

enum class Fx : uint8_t
{
    Chorus,
    Reverb,
    Both,
};

/** The synth's waveforms, chosen on E1 while the synth is playing. */
enum Shape : uint8_t
{
    kSaw,
    kSquare,
    kPulse,
    kTriangle,
    kSawSub,
    kSquareSub,
    kShapes,
};
const char* ShapeName(int shape);

struct Params
{
    Layer layer     = Layer::Synth;
    int   shape     = kSaw;
    int   sound     = 0;    // which sample library sound the sampler plays
    float detune    = 0.08f; // semitones between the two oscillators
    int   octave    = 0;    // -2..2
    float attack    = 0.005f; // seconds
    float release   = 0.3f;   // seconds
    float cutoff    = 0.6f;   // 0..1, exponential 20 Hz..20 kHz
    float resonance = 0.2f;   // 0..1
    float env_amount = 0.3f;  // 0..1: up to 5 octaves of filter envelope
    bool  lfo       = false;  // a slow filter sweep and a touch of vibrato
    Fx    fx        = Fx::Both;
    float fx_mix    = 0.25f;  // 0..1
};

void Init(float sample_rate);

void NoteOn(int note, int velocity);
void NoteOff(int note);
void Sustain(bool down);
void PitchBend(float semitones);
void AllNotesOff();

/** Silence every voice playing a sample, and return once the audio callback has
 *  (a few blocks at most): before the sample library is rewritten. */
void DropSamples();

/** One block of the stereo mix, the parameters as they stand. From the audio callback. */
void Process(const Params& p, float* left, float* right, size_t frames);

/** Voices sounding now, for the panel's LED. */
int Active();
} // namespace synth
