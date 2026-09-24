// rodeo-002 — the factory bank: the sounds a blank instrument boots with.
//
// These are the defaults handed to the preset store, so an instrument that has
// never been saved to still has somewhere to start, and a store that is erased
// comes back with them rather than sixteen empty slots.
//
// ENGINE CHOICE IS DELIBERATE. Grainlet, Particle, String and Karplus are on
// the chopping block (Derek, 2026-09-24), so nothing here uses one: if they go,
// no factory preset is orphaned and the bank needs no revision. Presets stored
// by hand are safe either way -- engines are named by stable id (preset.h) --
// but a preset that falls back to virtual-analog is still a preset that lost
// its sound, and the factory bank should not be able to.

#pragma once

#include "preset.h"

namespace rodeo002
{
/** Fill `b` with the factory bank: eight sounds, eight empty slots. */
inline void BuildFactoryBank(PresetBank& b)
{
    // Every byte set, including the header's own: the bank is compared byte for
    // byte against what is in flash to decide whether a write is needed.
    b.magic   = PresetBank::kMagic;
    b.version = PresetBank::kVersion;
    b.slots   = PresetBank::kNumSlots;

    // Empty everywhere first: an unused slot still holds a valid patch, so a
    // corrupt `used` byte cannot produce a nonsense sound.
    for(int i = 0; i < PresetBank::kNumSlots; i++)
    {
        PatchToPreset(Patch{}, "-- empty --", b.slot[i]);
        b.slot[i].used = 0;
    }

    int n = 0;
    auto add = [&](const char* name, const Patch& p) {
        if(n < PresetBank::kNumSlots)
            PatchToPreset(p, name, b.slot[n++]);
    };

    using namespace fx;

    // 1. Somewhere to start from: the raw voice, nothing in the chain.
    {
        Patch p;
        add("Init", p);
    }

    // 2. The house sound: additive pad into chorus and a wash.
    {
        Patch p;
        p.engine     = EngineId::kHarmonic;
        p.harmonics  = 0.40f;
        p.spread     = 0.35f;
        p.cutoff     = 0.55f;
        p.resonance  = 0.15f;
        p.attack     = 0.45f;
        p.decay      = 0.50f;
        p.sustain    = 0.80f;
        p.release    = 0.60f;
        p.lfo_dest   = LfoDest::kMorph;
        p.lfo_rate   = 0.15f;
        p.lfo_depth  = 0.25f;
        p.type[TextureChain::kMovement]      = Movement::kChorus;
        p.amount[TextureChain::kMovement]    = 0.45f;
        p.secondary[TextureChain::kMovement] = 0.25f; // rate
        p.type[TextureChain::kSpace]         = Space::kWash;
        p.amount[TextureChain::kSpace]       = 0.50f;
        p.space_time                         = 0.60f;
        p.space_feedback                     = 0.55f;
        p.space_grain                        = 0.60f;
        p.tilt                               = 0.55f;
        add("Chroma Pad", p);
    }

    // 3. FM tines through tape and a short echo.
    {
        Patch p;
        p.engine    = EngineId::kFm2;
        p.harmonics = 0.55f; // ratio
        p.timbre    = 0.45f; // index
        p.cutoff    = 0.85f;
        p.resonance = 0.10f;
        p.attack    = 0.02f;
        p.decay     = 0.35f;
        p.sustain   = 0.35f;
        p.release   = 0.30f;
        p.type[TextureChain::kCharacter]   = Character::kWarm;
        p.amount[TextureChain::kCharacter] = 0.30f;
        p.type[TextureChain::kTexture]     = Texture::kTape;
        p.amount[TextureChain::kTexture]   = 0.25f;
        p.type[TextureChain::kSpace]       = Space::kEcho;
        p.amount[TextureChain::kSpace]     = 0.35f;
        p.space_time                       = 0.28f;
        p.space_feedback                   = 0.35f;
        p.space_grain                      = 0.25f;
        add("Glass Keys", p);
    }

    // 4. Vowels, smeared into a grain cloud.
    {
        Patch p;
        p.engine    = EngineId::kVosim;
        p.timbre    = 0.60f;
        p.morph     = 0.45f;
        p.spread    = 0.30f;
        p.cutoff    = 0.70f;
        p.attack    = 0.30f;
        p.sustain   = 0.85f;
        p.release   = 0.50f;
        p.type[TextureChain::kCharacter]     = Character::kWarm;
        p.amount[TextureChain::kCharacter]   = 0.35f;
        p.type[TextureChain::kMovement]      = Movement::kVibrato;
        p.amount[TextureChain::kMovement]    = 0.30f;
        p.secondary[TextureChain::kMovement] = 0.20f;
        p.type[TextureChain::kTexture]       = Texture::kTape;
        p.amount[TextureChain::kTexture]     = 0.40f;
        p.type[TextureChain::kSpace]         = Space::kCloud;
        p.amount[TextureChain::kSpace]       = 0.50f;
        p.space_time                         = 0.50f;
        p.space_feedback                     = 0.45f;
        p.space_grain                        = 0.55f;
        p.grain_spread                       = 0.60f;
        add("Tape Choir", p);
    }

    // 5. Struck, with octaves rising out of the tail.
    {
        Patch p;
        p.engine    = EngineId::kZOsc;
        p.harmonics = 0.60f;
        p.morph     = 0.60f;
        p.cutoff    = 0.90f;
        p.attack    = 0.00f;
        p.decay     = 0.50f;
        p.sustain   = 0.20f;
        p.release   = 0.70f;
        p.type[TextureChain::kSpace]      = Space::kReverb;
        p.amount[TextureChain::kSpace]    = 0.55f;
        p.secondary[TextureChain::kSpace] = 0.35f; // damp
        p.space_time                      = 0.35f; // pre-delay
        p.space_feedback                  = 0.80f; // decay
        p.space_grain                     = 0.70f; // shimmer
        p.grain_pitch                     = 0.75f; // up
        p.option[TextureChain::kSpace]    = Space::kOctaves;
        add("Shimmer Bell", p);
    }

    // 6. Everything wrong, on purpose.
    {
        Patch p;
        p.engine    = EngineId::kVariableSaw;
        p.cutoff    = 0.60f;
        p.resonance = 0.30f;
        p.attack    = 0.01f;
        p.decay     = 0.30f;
        p.sustain   = 0.60f;
        p.release   = 0.20f;
        p.type[TextureChain::kCharacter]     = Character::kFuzz;
        p.amount[TextureChain::kCharacter]   = 0.55f;
        p.secondary[TextureChain::kCharacter] = 0.60f; // bias
        p.type[TextureChain::kMovement]      = Movement::kTremolo;
        p.amount[TextureChain::kMovement]    = 0.35f;
        p.secondary[TextureChain::kMovement] = 0.50f;
        p.type[TextureChain::kTexture]       = Texture::kCrush;
        p.amount[TextureChain::kTexture]     = 0.50f;
        p.secondary[TextureChain::kTexture]  = 0.40f;
        p.type[TextureChain::kSpace]         = Space::kEcho;
        p.amount[TextureChain::kSpace]       = 0.45f;
        p.space_time                         = 0.22f;
        p.space_feedback                     = 0.55f;
        p.tilt                               = 0.40f; // darker
        add("Broken Radio", p);
    }

    // 7. One voice, driven, with a slide.
    {
        Patch p;
        p.engine        = EngineId::kVariableSaw;
        p.voice_mode    = VoiceMode::kMono;
        p.glide         = 0.25f;
        p.cutoff        = 0.50f;
        p.resonance     = 0.45f;
        p.filter_env    = 0.75f;
        p.vel_to_filter = 0.50f;
        p.attack        = 0.01f;
        p.decay         = 0.35f;
        p.sustain       = 0.50f;
        p.release       = 0.20f;
        p.type[TextureChain::kCharacter]   = Character::kDrive;
        p.amount[TextureChain::kCharacter] = 0.45f;
        p.type[TextureChain::kSpace]       = Space::kEcho;
        p.amount[TextureChain::kSpace]     = 0.25f;
        p.space_time                       = 0.30f;
        p.space_feedback                   = 0.30f;
        add("Mono Lead", p);
    }

    // 8. Hold a chord and let it become weather.
    {
        Patch p;
        p.engine    = EngineId::kFormant;
        p.timbre    = 0.55f;
        p.spread    = 0.50f;
        p.cutoff    = 0.60f;
        p.attack    = 0.50f;
        p.sustain   = 0.90f;
        p.release   = 0.80f;
        p.lfo_dest  = LfoDest::kCutoff;
        p.lfo_rate  = 0.10f;
        p.lfo_depth = 0.20f;
        p.type[TextureChain::kMovement]   = Movement::kPhaser;
        p.amount[TextureChain::kMovement] = 0.30f;
        p.type[TextureChain::kTexture]    = Texture::kDust;
        p.amount[TextureChain::kTexture]  = 0.20f;
        p.type[TextureChain::kSpace]      = Space::kCloud;
        p.amount[TextureChain::kSpace]    = 0.70f;
        p.space_time                      = 0.75f;
        p.space_feedback                  = 0.70f;
        p.space_grain                     = 0.70f;
        p.grain_spread                    = 0.75f;
        add("Freeze Cloud", p);
    }
}

} // namespace rodeo002
