// rodeo-002 — presets: what a patch looks like in flash, and the factory bank.
//
// The whole patch travels, minus what the concept doc calls global: MIDI
// channel, master volume and the panel wiring map stay out, because a preset
// that carried the MIDI channel could leave the synth deaf and one that carried
// the volume would jump the output level on load.
//
// ENGINES ARE STORED BY A STABLE ID, NOT BY ENUM POSITION. Derek is weighing up
// dropping Grainlet, Particle, String and Karplus (2026-09-24). Removing an
// entry from EngineId renumbers every engine after it, so a preset bank written
// today would come back as the wrong sounds tomorrow -- silently, which is the
// worst kind. Each engine therefore has a number of its own here that is
// assigned once and NEVER REUSED: drop an engine and its id simply becomes
// unknown, which a preset survives by falling back to virtual-analog and
// saying so. The same rule applies to anything else a preset names by index:
// every enum is range-checked on load, so a stale value cannot walk off the end
// of a table.
//
// Everything here is plain data and host-testable; nothing touches flash. The
// app owns the storage (see the concept doc's QSPI map).

#pragma once

#include <cstdint>

#include "patch.h"

namespace rodeo002
{
/** Engine identity in storage. Assigned once; a number is never reused, and a
 *  removed engine's number is simply never handed out again. */
enum class EngineStableId : uint8_t
{
    kVirtualAnalog = 1,
    kVariableSaw   = 2,
    kFm2           = 3,
    kFormant       = 4,
    kVosim         = 5,
    kHarmonic      = 6,
    kZOsc          = 7,
    kGrainlet      = 8,
    kParticle      = 9,
    kString        = 10,
    kKarplus       = 11,
    kUnknown       = 0,
};

inline uint8_t EngineToStableId(EngineId e)
{
    switch(e)
    {
        case EngineId::kVirtualAnalog: return 1;
        case EngineId::kVariableSaw: return 2;
        case EngineId::kFm2: return 3;
        case EngineId::kFormant: return 4;
        case EngineId::kVosim: return 5;
        case EngineId::kHarmonic: return 6;
        case EngineId::kZOsc: return 7;
        case EngineId::kGrainlet: return 8;
        case EngineId::kParticle: return 9;
        case EngineId::kString: return 10;
        case EngineId::kKarplus: return 11;
        default: return 0;
    }
}

/** The reverse. `known` comes back false for an engine this firmware no longer
 *  has, in which case the caller gets virtual-analog and should say so rather
 *  than pretend the preset loaded intact. */
inline EngineId EngineFromStableId(uint8_t id, bool* known = nullptr)
{
    EngineId e  = EngineId::kVirtualAnalog;
    bool     ok = true;
    switch(id)
    {
        case 1: e = EngineId::kVirtualAnalog; break;
        case 2: e = EngineId::kVariableSaw; break;
        case 3: e = EngineId::kFm2; break;
        case 4: e = EngineId::kFormant; break;
        case 5: e = EngineId::kVosim; break;
        case 6: e = EngineId::kHarmonic; break;
        case 7: e = EngineId::kZOsc; break;
        case 8: e = EngineId::kGrainlet; break;
        case 9: e = EngineId::kParticle; break;
        case 10: e = EngineId::kString; break;
        case 11: e = EngineId::kKarplus; break;
        default: ok = false; break;
    }
    // An id this build knows nothing about, or one whose engine has since been
    // removed from the enum: fall back rather than index past the end.
    if(static_cast<int>(e) >= static_cast<int>(EngineId::kCount))
    {
        e  = EngineId::kVirtualAnalog;
        ok = false;
    }
    if(known)
        *known = ok;
    return e;
}

/** One stored patch. Plain old data: it is memcpy'd into flash, and compared
 *  byte for byte to decide whether a save has anything to write -- so it must
 *  hold NO IMPLICIT PADDING. Padding bytes are whatever was on the stack, so
 *  two identical banks would compare unequal and every boot would rewrite the
 *  sector. The layout below puts every byte-sized field first, in a multiple of
 *  four, with the slack named and zeroed; the static_assert holds the line. */
struct StoredPreset
{
    static constexpr int kNameLen = 13; // 12 characters and a terminator

    char    name[kNameLen];
    uint8_t used; // 0 = an empty slot, which the browser will not load
    uint8_t engine_id;
    uint8_t voice_mode, lfo_dest, filter, lfo_shape;
    int8_t  bend_range;

    float harmonics, timbre, morph, spread, cutoff, resonance, attack, release;
    float env_to_timbre, env_to_morph, decay, sustain, filter_env, lfo_rate, lfo_depth;
    float keytrack, glide, vel_to_filter;

    float   amount[fx::TextureChain::kNumSlots];
    float   secondary[fx::TextureChain::kNumSlots];
    uint8_t type[fx::TextureChain::kNumSlots];
    uint8_t option[fx::TextureChain::kNumSlots];
    uint8_t bypass[fx::TextureChain::kNumSlots];
    uint8_t freeze;
    uint8_t reserved[3]; // keeps the floats aligned with nothing implicit

    float   space_time, space_feedback, space_grain, grain_pitch, grain_spread;
    float   tilt;
};

// 20 bytes of header, 16 of choices and slack, 32 floats. If this fires, the
// layout changed: bump PresetBank::kVersion so old banks are not misread.
static_assert(sizeof(StoredPreset) == 164, "StoredPreset layout changed");

namespace detail
{
inline float Clamp01(float v)
{
    if(!(v >= 0.f)) // also catches NaN out of a corrupt store
        return 0.f;
    return v > 1.f ? 1.f : v;
}
inline uint8_t ClampEnum(uint8_t v, int count)
{
    return v < static_cast<uint8_t>(count) ? v : 0;
}
inline void CopyName(char* dst, const char* src)
{
    int i = 0;
    for(; i < StoredPreset::kNameLen - 1 && src[i]; i++)
        dst[i] = src[i];
    for(; i < StoredPreset::kNameLen; i++)
        dst[i] = '\0';
}
} // namespace detail

inline void PatchToPreset(const Patch& p, const char* name, StoredPreset& s)
{
    for(int i = 0; i < 3; i++)
        s.reserved[i] = 0; // never left to chance: it is compared
    detail::CopyName(s.name, name);
    s.used       = 1;
    s.engine_id  = EngineToStableId(p.engine);
    s.voice_mode = static_cast<uint8_t>(p.voice_mode);
    s.lfo_dest   = static_cast<uint8_t>(p.lfo_dest);
    s.filter     = static_cast<uint8_t>(p.filter);
    s.lfo_shape  = static_cast<uint8_t>(p.lfo_shape);
    s.bend_range = static_cast<int8_t>(p.bend_range);

    s.harmonics = p.harmonics;
    s.timbre    = p.timbre;
    s.morph     = p.morph;
    s.spread    = p.spread;
    s.cutoff    = p.cutoff;
    s.resonance = p.resonance;
    s.attack    = p.attack;
    s.release   = p.release;

    s.env_to_timbre = p.env_to_timbre;
    s.env_to_morph  = p.env_to_morph;
    s.decay         = p.decay;
    s.sustain       = p.sustain;
    s.filter_env    = p.filter_env;
    s.lfo_rate      = p.lfo_rate;
    s.lfo_depth     = p.lfo_depth;

    s.keytrack      = p.keytrack;
    s.glide         = p.glide;
    s.vel_to_filter = p.vel_to_filter;

    for(int i = 0; i < fx::TextureChain::kNumSlots; i++)
    {
        s.amount[i]    = p.amount[i];
        s.secondary[i] = p.secondary[i];
        s.type[i]      = static_cast<uint8_t>(p.type[i]);
        s.option[i]    = static_cast<uint8_t>(p.option[i]);
        s.bypass[i]    = p.bypass[i] ? 1 : 0;
    }

    s.space_time     = p.space_time;
    s.space_feedback = p.space_feedback;
    s.space_grain    = p.space_grain;
    s.grain_pitch    = p.grain_pitch;
    s.grain_spread   = p.grain_spread;
    s.freeze         = p.freeze ? 1 : 0;
    s.tilt           = p.tilt;
}

/** Load a stored preset into a patch. Everything is range-checked: a store can
 *  be older than this firmware, or corrupt, and neither may produce a patch
 *  that indexes past a table or holds a value the panel could not have set.
 *  `engine_known` comes back false if the preset names an engine this build no
 *  longer has. */
inline void PresetToPatch(const StoredPreset& s, Patch& p, bool* engine_known = nullptr)
{
    p.engine     = EngineFromStableId(s.engine_id, engine_known);
    p.voice_mode = static_cast<VoiceMode>(
        detail::ClampEnum(s.voice_mode, static_cast<int>(VoiceMode::kCount)));
    p.lfo_dest = static_cast<LfoDest>(
        detail::ClampEnum(s.lfo_dest, static_cast<int>(LfoDest::kCount)));
    p.filter = static_cast<FilterType>(
        detail::ClampEnum(s.filter, static_cast<int>(FilterType::kCount)));
    p.lfo_shape  = detail::ClampEnum(s.lfo_shape, fx::Lfo::kNumShapes);
    p.bend_range = s.bend_range < 0 ? 0 : (s.bend_range > 24 ? 24 : s.bend_range);

    p.harmonics = detail::Clamp01(s.harmonics);
    p.timbre    = detail::Clamp01(s.timbre);
    p.morph     = detail::Clamp01(s.morph);
    p.spread    = detail::Clamp01(s.spread);
    p.cutoff    = detail::Clamp01(s.cutoff);
    p.resonance = detail::Clamp01(s.resonance);
    p.attack    = detail::Clamp01(s.attack);
    p.release   = detail::Clamp01(s.release);

    p.env_to_timbre = detail::Clamp01(s.env_to_timbre);
    p.env_to_morph  = detail::Clamp01(s.env_to_morph);
    p.decay         = detail::Clamp01(s.decay);
    p.sustain       = detail::Clamp01(s.sustain);
    p.filter_env    = detail::Clamp01(s.filter_env);
    p.lfo_rate      = detail::Clamp01(s.lfo_rate);
    p.lfo_depth     = detail::Clamp01(s.lfo_depth);

    p.keytrack      = detail::Clamp01(s.keytrack);
    p.glide         = detail::Clamp01(s.glide);
    p.vel_to_filter = detail::Clamp01(s.vel_to_filter);

    for(int i = 0; i < fx::TextureChain::kNumSlots; i++)
    {
        p.amount[i]    = detail::Clamp01(s.amount[i]);
        p.secondary[i] = detail::Clamp01(s.secondary[i]);
        p.type[i]      = detail::ClampEnum(s.type[i], fx::TextureChain::NumTypes(i));
        p.option[i]    = detail::ClampEnum(s.option[i], fx::TextureChain::NumOptions(i));
        p.bypass[i]    = s.bypass[i] != 0;
    }

    p.space_time     = detail::Clamp01(s.space_time);
    p.space_feedback = detail::Clamp01(s.space_feedback);
    p.space_grain    = detail::Clamp01(s.space_grain);
    p.grain_pitch    = detail::Clamp01(s.grain_pitch);
    p.grain_spread   = detail::Clamp01(s.grain_spread);
    p.freeze         = s.freeze != 0;
    p.tilt           = detail::Clamp01(s.tilt);
}

/** The bank, as it sits in flash. One 64 KB QSPI sector holds it comfortably. */
struct PresetBank
{
    static constexpr int      kNumSlots = 16;
    static constexpr uint32_t kMagic    = 0x52325042u; // "R2PB"
    static constexpr uint16_t kVersion  = 1;

    uint32_t     magic;
    uint16_t     version;
    uint16_t     slots;
    StoredPreset slot[kNumSlots];

    bool Valid() const
    {
        return magic == kMagic && version == kVersion && slots == kNumSlots;
    }
    bool operator==(const PresetBank& o) const
    {
        const uint8_t* a = reinterpret_cast<const uint8_t*>(this);
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&o);
        for(size_t i = 0; i < sizeof(PresetBank); i++)
            if(a[i] != b[i])
                return false;
        return true;
    }
    bool operator!=(const PresetBank& o) const { return !(*this == o); }
};

} // namespace rodeo002
