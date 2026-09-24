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
        case VoiceMode::kPoly: return "POL";
        case VoiceMode::kMono: return "MON";
        default: return "UNI";
    }
}

/** LFO destinations -- one LFO, one destination (no mod matrix). */
enum class LfoDest
{
    kPitch = 0,
    kTimbre,
    kMorph,
    kCutoff,
    kAmp,
    kSpaceMix,
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
        default: return "Space mix";
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
    float env_to_timbre = 0.5f; // 0.5 = no modulation; either side signs it
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
    int   midi_channel = 0;   // 0 = omni
    float volume       = 0.8f;
};

} // namespace rodeo002
