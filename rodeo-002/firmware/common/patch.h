// rodeo-002 — a patch: everything the panel can set, plus what a preset stores.
//
// Field order follows the control map so the two can be read side by side.
// Pot parameters are 0..1 exactly as the panel delivers them; enums are ints.
//
// NOT in here, deliberately (concept doc, *Preset system*): MIDI channel,
// master volume and pot calibration are GLOBAL. A preset that carried the MIDI
// channel could leave the synth deaf; one that carried the volume would jump
// the output level on load.

#pragma once

#include "engines.h"
#include "fx_character.h"
#include "fx_movement.h"
#include "fx_space.h"
#include "fx_texture.h"
#include "texture_chain.h"

namespace rodeo002
{
enum class VoiceMode
{
    kPoly = 0,
    kMono,
    kUnison,
    kCount
};

inline const char* VoiceModeName(VoiceMode m)
{
    switch(m)
    {
        case VoiceMode::kPoly: return "Poly";
        case VoiceMode::kMono: return "Mono";
        default: return "Unison";
    }
}

/** Three letters, for the header, where the preset's name has the room. */
inline const char* VoiceModeShort(VoiceMode m)
{
    switch(m)
    {
        case VoiceMode::kPoly: return "POLY";
        case VoiceMode::kMono: return "MONO";
        default: return "UNI";
    }
}

/** The sub-oscillator's interval, in the order the knob steps through it.
 *
 *  MONOTONIC, and index 0 is -12 on purpose: the two stored bytes this rides in
 *  were `reserved`, so every preset ever saved reads them as zero, and zero has
 *  to decode to the most useful setting rather than to an accident. -24 is not
 *  on the list -- an octave below a bass note is already subsonic, and two is
 *  just wasted travel. */
enum class SubInterval
{
    kMinus12 = 0,
    kMinus7,
    kMinus5,
    kUnison,
    kPlus5,
    kPlus7,
    kPlus12,
    kPlus19,
    kCount
};

inline float SubIntervalSemitones(SubInterval i)
{
    switch(i)
    {
        case SubInterval::kMinus12: return -12.f;
        case SubInterval::kMinus7: return -7.f;
        case SubInterval::kMinus5: return -5.f;
        case SubInterval::kUnison: return 0.f;
        case SubInterval::kPlus5: return 5.f;
        case SubInterval::kPlus7: return 7.f;
        case SubInterval::kPlus12: return 12.f;
        default: return 19.f;
    }
}

inline const char* SubIntervalName(SubInterval i)
{
    switch(i)
    {
        case SubInterval::kMinus12: return "-12";
        case SubInterval::kMinus7: return "-7";
        case SubInterval::kMinus5: return "-5";
        case SubInterval::kUnison: return "unison";
        case SubInterval::kPlus5: return "+5";
        case SubInterval::kPlus7: return "+7";
        case SubInterval::kPlus12: return "+12";
        default: return "+19";
    }
}

/** The sub's waveform. Sine is index 0, so a zeroed byte gives the one that
 *  adds weight without adding harmonics -- what a sub is usually for. */
enum class SubWave
{
    kSine = 0,
    kTriangle,
    kSaw,
    kSquare,
    kCount
};

inline const char* SubWaveName(SubWave w)
{
    switch(w)
    {
        case SubWave::kSine: return "sine";
        case SubWave::kTriangle: return "triangle";
        case SubWave::kSaw: return "saw";
        default: return "square";
    }
}

/** LFO destinations -- one LFO, one destination (no mod matrix). */
/** One LFO, one destination (no mod matrix).
 *
 *  NEW ONES GO ON THE END. The value is stored in presets by index, so
 *  inserting in the middle would silently re-point every saved patch. */
enum class LfoDest
{
    kPitch = 0,
    kTimbre,
    kMorph,
    kCutoff,
    kAmp,
    kSpaceMix,
    kHarmonics,
    kResonance,
    kCharacterAmt,
    kCount
};

inline const char* LfoDestName(LfoDest d)
{
    switch(d)
    {
        case LfoDest::kPitch: return "Pitch";
        case LfoDest::kTimbre: return "Timbre";
        case LfoDest::kMorph: return "Morph";
        case LfoDest::kCutoff: return "Cutoff";
        case LfoDest::kAmp: return "Amp";
        case LfoDest::kSpaceMix: return "Space mix";
        case LfoDest::kHarmonics: return "Harmonics";
        case LfoDest::kResonance: return "Resonance";
        default: return "Character";
    }
}

enum class FilterType
{
    kLowPass = 0,
    kBandPass,
    kHighPass,
    kCount
};

inline const char* FilterTypeName(FilterType f)
{
    switch(f)
    {
        case FilterType::kLowPass: return "Low pass";
        case FilterType::kBandPass: return "Band pass";
        default: return "High pass";
    }
}

/** What the mod wheel does. It used to be welded to LFO depth, which is a fine
 *  default and a poor rule -- a wheel is the most expressive thing a keyboard
 *  has (Derek, 2026-09-25). Stored in a preset, so NEW ONES GO ON THE END. */
enum class ModDest
{
    kLfoDepth = 0,
    kCutoff,
    kTimbre,
    kMorph,
    kSpaceMix,
    kCharacterAmt,
    kCount
};

inline const char* ModDestName(ModDest d)
{
    switch(d)
    {
        case ModDest::kLfoDepth: return "LFO depth";
        case ModDest::kCutoff: return "Cutoff";
        case ModDest::kTimbre: return "Timbre";
        case ModDest::kMorph: return "Morph";
        case ModDest::kSpaceMix: return "Space mix";
        default: return "Character";
    }
}

struct Patch
{
    // --- voice, main layer (pot rows 1-2) ----------------------------------
    float harmonics = 0.5f;
    float timbre    = 0.5f;
    float morph     = 0.5f;
    float spread    = 0.f;
    float cutoff    = 0.8f;
    float resonance = 0.2f;
    float attack    = 0.05f;
    float release   = 0.25f;

    // --- voice, shift layer -------------------------------------------------
    /** The sub-oscillator: a plain waveform under (or over) the note, mixed in
     *  before the filter. Not a second macro engine -- it costs a fraction of
     *  one, so switching it on barely moves the polyphony cap, and what it adds
     *  is weight rather than the beating Unison already gives. Level 0 is off
     *  and costs nothing at all. */
    float       sub_level    = 0.f;
    SubInterval sub_interval = SubInterval::kMinus12;
    SubWave     sub_wave     = SubWave::kSine;

    /** Env -> Timbre/Morph moved to the patch settings page on 2026-09-29: they
     *  are depths you set for a patch and leave, and the two knobs they held
     *  went to the sub-oscillator, which you do play with. Still signed, still
     *  0.5 for none. */
    float env_to_timbre = 0.5f;
    float env_to_morph  = 0.5f;
    float decay         = 0.4f;
    float sustain       = 0.7f;
    float filter_env    = 0.5f; // 0.5 = none
    float lfo_rate      = 0.3f;
    float lfo_depth     = 0.f;

    // --- patch settings page -------------------------------------------------
    float keytrack   = 0.5f;
    float glide      = 0.f;
    float vel_to_filter = 0.3f;
    int   bend_range = 2; // semitones
    ModDest mod_dest = ModDest::kLfoDepth;
    int   lfo_shape  = fx::Lfo::kSine;

    // --- discrete voice choices ---------------------------------------------
    EngineId   engine     = EngineId::kVirtualAnalog;
    VoiceMode  voice_mode = VoiceMode::kPoly;
    LfoDest    lfo_dest   = LfoDest::kCutoff;
    FilterType filter     = FilterType::kLowPass;

    // --- the texture chain ---------------------------------------------------
    float amount[fx::TextureChain::kNumSlots]    = {0.f, 0.f, 0.f, 0.f};
    float secondary[fx::TextureChain::kNumSlots] = {0.5f, 0.3f, 1.f, 0.3f};
    int   type[fx::TextureChain::kNumSlots]
        = {fx::Character::kWarm, fx::Movement::kChorus, fx::Texture::kCrush, fx::Space::kEcho};
    int  option[fx::TextureChain::kNumSlots] = {0, 0, 0, 0};
    bool bypass[fx::TextureChain::kNumSlots] = {false, false, false, false};

    // Space's own controls
    float space_time     = 0.35f;
    float space_feedback = 0.35f;
    float space_grain    = 0.4f;
    float grain_pitch    = 0.5f; // 0.5 = unison
    float grain_spread   = 0.5f;
    bool  freeze         = false;

    // --- output --------------------------------------------------------------
    float tilt = 0.5f; // flat
};

/** Global settings: never stored in a preset. */
struct Globals
{
    int   midi_channel     = 0; // 0 = omni
    int   midi_out_channel = 1; // what the panel sends on; no omni for output
    float volume           = 0.8f;
};

} // namespace rodeo002
