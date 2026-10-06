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
    kUnknown       = 0,
    // 8 Grainlet, 9 Particle, 10 String, 11 Karplus -- RETIRED 2026-09-29.
    // Listed so nobody hands one of these out again: a preset saved by an
    // earlier build still carries the number, and it has to keep meaning "an
    // engine this firmware does not have" rather than whatever gets added next.
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
        // 8..11 are the retired engines: they fall through to the default and
        // come back as "not known to this build", which is the whole point of
        // never reusing a number.
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

    /** `release` occupied this slot until 2026-10-01. It had been dead since
     *  the envelope became ADS -- written in step with decay, read by nothing --
     *  so the patch gain took its four bytes rather than growing the struct.
     *  kVersion went to 3 for it, with a migration: a v2 bank is accepted and
     *  every slot's gain set to unity, so nothing saved is lost. */
    float harmonics, timbre, morph, spread, cutoff, resonance, attack, gain;
    float env_to_timbre, env_to_morph, decay, sustain, filter_env, lfo_rate, lfo_depth;
    float keytrack, glide, vel_to_filter;

    float   amount[fx::TextureChain::kNumSlots];
    float   secondary[fx::TextureChain::kNumSlots];
    uint8_t type[fx::TextureChain::kNumSlots];
    uint8_t option[fx::TextureChain::kNumSlots];
    uint8_t bypass[fx::TextureChain::kNumSlots];
    uint8_t freeze;
    uint8_t mod_dest;    // was reserved; 0 is LFO depth, which is what it always did
    /** The sub-oscillator, in the last two spare bytes (2026-09-29). Both were
     *  `reserved` and both have always been written as zero, so every preset
     *  ever saved decodes as: level 0 -- the sub off -- with interval -12 and a
     *  sine, which is what you would pick anyway. That is what let the sub
     *  arrive WITHOUT bumping PresetBank::kVersion, which would have rejected
     *  the stored bank and lost every saved preset.
     *
     *  Level is 0..255 over 0..1; that is finer than the pot reads. The second
     *  byte packs the waveform in the high nibble and the interval in the low
     *  one -- four and eight values, so a nibble each with room to spare. */
    uint8_t sub_level;
    uint8_t sub_shape; // wave << 4 | interval

    float   space_time, space_feedback, space_grain, grain_pitch, grain_spread;
    float   tilt;
};

// 20 bytes of header, 16 of choices and slack, 32 floats. If this fires, the
// layout changed: bump PresetBank::kVersion so old banks are not misread.
static_assert(sizeof(StoredPreset) == 164, "StoredPreset layout changed");

// THERE IS NO SLACK LEFT. The last two bytes went to the sub-oscillator on
// 2026-09-29. Anything added from here grows the struct, which means bumping
// PresetBank::kVersion, which means every saved preset is lost -- so the next
// thing that needs storage needs a migration first, not another field.

namespace detail
{
inline float Clamp01(float v)
{
    if(!(v >= 0.f)) // also catches NaN out of a corrupt store
        return 0.f;
    return v > 1.f ? 1.f : v;
}
inline float ClampGain(float g)
{
    if(!(g >= kGainMin)) // also catches NaN out of a corrupt store
        return 1.f;
    return g > kGainMax ? kGainMax : g;
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
    s.mod_dest = static_cast<uint8_t>(p.mod_dest);
    s.sub_level
        = static_cast<uint8_t>(detail::Clamp01(p.sub_level) * 255.f + 0.5f);
    s.sub_shape = static_cast<uint8_t>((static_cast<int>(p.sub_wave) << 4)
                                       | static_cast<int>(p.sub_interval));
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
    s.gain      = detail::ClampGain(p.gain);

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
    p.gain      = detail::ClampGain(s.gain);

    p.sub_level    = static_cast<float>(s.sub_level) * (1.f / 255.f);
    p.sub_interval = static_cast<SubInterval>(
        detail::ClampEnum(s.sub_shape & 0x0F, static_cast<int>(SubInterval::kCount)));
    p.sub_wave = static_cast<SubWave>(
        detail::ClampEnum((s.sub_shape >> 4) & 0x0F, static_cast<int>(SubWave::kCount)));
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
    // Taken from a byte that used to be padding, so a preset written before the
    // mod wheel had a destination reads 0 -- LFO depth, which is what the wheel
    // did then. No version bump, no preset lost.
    p.mod_dest = static_cast<ModDest>(
        detail::ClampEnum(s.mod_dest, static_cast<int>(ModDest::kCount)));
    p.tilt           = detail::Clamp01(s.tilt);
}

/** The bank, as it sits in flash. One 64 KB QSPI sector holds it comfortably. */
struct PresetBank
{
    /** 16 factory sounds and 8 empty slots to save your own into. Empty rather
     *  than copies of Init: an empty slot says so and refuses to load, where a
     *  copy of Init looks like a sound somebody chose. */
    static constexpr int      kNumSlots = 24;
    static constexpr uint32_t kMagic    = 0x52325042u; // "R2PB"
    /** 2: 24 slots rather than 16, which changes this struct's size. A stored
     *  bank from v1 is rejected and the factory sixteen come back -- no
     *  migration, by Derek's call (2026-09-29). */
    static constexpr uint16_t kVersion = 3;

    uint32_t     magic;
    uint16_t     version;
    uint16_t     slots;
    StoredPreset slot[kNumSlots];

    bool Valid() const
    {
        return magic == kMagic && version == kVersion && slots == kNumSlots;
    }

    /** Bring a v2 bank up to v3 in place, and say whether anything was done.
     *
     *  v3 reinterprets one field: the four bytes that held `release` now hold
     *  the patch gain. The LAYOUT IS UNCHANGED -- same offset, same size -- so
     *  the whole migration is to put unity in that field and move the version
     *  on. Nothing saved is lost, which is the only reason the version could be
     *  bumped at all (2026-10-01).
     *
     *  A v2 slot holds a copy of `decay` there, which as a gain would be a
     *  quiet preset that nobody asked for, so it cannot simply be left. */
    bool MigrateFromV2()
    {
        if(magic != kMagic || version != 2 || slots != kNumSlots)
            return false;
        for(int i = 0; i < kNumSlots; i++)
            slot[i].gain = 1.f;
        version = kVersion;
        return true;
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

/** Copy any preset the bank does not have into the empty slots. Returns how
 *  many were filled.
 *
 *  This is how new factory sounds reach an instrument that has already been
 *  saved to. The defaults in flash are only written while the store has never
 *  been used, so after the first save a later firmware's factory bank would
 *  otherwise never be seen. Nothing that is already there is touched -- an
 *  occupied slot is left exactly as it is, whether it holds a factory sound or
 *  one of yours. The only thing it cannot tell is a slot deliberately left
 *  empty from one that has never been filled, and it will fill it; there is no
 *  gesture for emptying a slot, so today that cannot arise. */
inline int FillEmptySlots(PresetBank& bank, const PresetBank& factory)
{
    if(!bank.Valid() || !factory.Valid())
        return 0;
    int filled = 0;
    for(int i = 0; i < PresetBank::kNumSlots; i++)
        if(!bank.slot[i].used && factory.slot[i].used)
        {
            bank.slot[i] = factory.slot[i];
            filled++;
        }
    return filled;
}

} // namespace rodeo002
