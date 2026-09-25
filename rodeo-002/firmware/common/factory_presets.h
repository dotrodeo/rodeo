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
/** Fill `b` with the factory bank: sixteen sounds, one per slot. */
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
    // Taken by value so the fall time can be normalised: the envelope is ADS
    // (Derek, 2026-09-24), so decay IS the release, and a preset must not carry
    // a release the panel cannot express. Where the two used to differ, the
    // decay below carries whichever mattered more to the sound -- the tail for
    // the pads, the fall to sustain for the struck ones.
    auto add = [&](const char* name, Patch p) {
        p.release = p.decay;
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
        p.decay      = 0.60f;
        p.sustain    = 0.80f;
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
        p.decay    = 0.50f;
        p.sustain   = 0.85f;
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
        p.decay     = 0.60f;
        p.sustain   = 0.20f;
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
        p.decay     = 0.28f;
        p.sustain   = 0.60f;
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
        p.decay         = 0.30f;
        p.sustain       = 0.50f;
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
        p.decay    = 0.80f;
        p.sustain   = 0.90f;
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

    // 9. Hard sync, hit short. Harmonics is the sync sweep on this engine, so
    //    it is the knob that makes this one move.
    {
        Patch p;
        p.engine     = EngineId::kVirtualAnalog;
        p.harmonics  = 0.55f;
        p.timbre     = 0.40f;
        p.cutoff     = 0.80f;
        p.resonance  = 0.25f;
        p.attack     = 0.00f;
        p.decay      = 0.22f;
        p.sustain    = 0.15f;
        p.filter_env = 0.70f;
        p.type[TextureChain::kCharacter]   = Character::kDrive;
        p.amount[TextureChain::kCharacter] = 0.35f;
        p.type[TextureChain::kSpace]       = Space::kEcho;
        p.amount[TextureChain::kSpace]     = 0.30f;
        p.space_time                       = 0.24f;
        p.space_feedback                   = 0.40f;
        add("Sync Stab", p);
    }

    // 10. Hold one note and leave the room.
    {
        Patch p;
        p.engine     = EngineId::kHarmonic;
        p.voice_mode = VoiceMode::kUnison;
        p.spread     = 0.55f;
        p.harmonics  = 0.55f;
        p.cutoff     = 0.50f;
        p.attack     = 0.70f;
        p.decay     = 0.90f;
        p.sustain    = 1.00f;
        p.lfo_dest   = LfoDest::kMorph;
        p.lfo_rate   = 0.08f;
        p.lfo_depth  = 0.35f;
        p.type[TextureChain::kSpace]   = Space::kWash;
        p.amount[TextureChain::kSpace] = 0.65f;
        p.space_time                   = 0.80f;
        p.space_feedback               = 0.75f;
        p.space_grain                  = 0.70f;
        add("Wash Drone", p);
    }

    // 11. Every note answers itself backwards.
    {
        Patch p;
        p.engine    = EngineId::kFm2;
        p.harmonics = 0.35f;
        p.timbre    = 0.35f;
        p.cutoff    = 0.75f;
        p.attack    = 0.02f;
        p.decay     = 0.38f;
        p.sustain   = 0.40f;
        p.type[TextureChain::kSpace]   = Space::kReverse;
        p.amount[TextureChain::kSpace] = 0.55f;
        p.space_time                   = 0.45f;
        p.space_feedback               = 0.45f;
        p.space_grain                  = 0.50f;
        add("Reverse Keys", p);
    }

    // 12. Struck and dusty: the noise floor is part of the instrument.
    {
        Patch p;
        p.engine    = EngineId::kZOsc;
        p.harmonics = 0.30f;
        p.morph     = 0.35f;
        p.cutoff    = 0.70f;
        p.attack    = 0.00f;
        p.decay     = 0.55f;
        p.sustain   = 0.10f;
        p.type[TextureChain::kTexture]    = Texture::kDust;
        p.amount[TextureChain::kTexture]  = 0.35f;
        p.type[TextureChain::kSpace]      = Space::kReverb;
        p.amount[TextureChain::kSpace]    = 0.45f;
        p.secondary[TextureChain::kSpace] = 0.50f; // damp
        p.space_time                      = 0.20f;
        p.space_feedback                  = 0.55f;
        p.space_grain                     = 0.00f; // no shimmer: just the room
        add("Dust Piano", p);
    }

    // 13. One voice, low, folded until it buzzes.
    {
        Patch p;
        p.engine     = EngineId::kVirtualAnalog;
        p.voice_mode = VoiceMode::kMono;
        p.glide      = 0.08f;
        p.harmonics  = 0.20f;
        p.cutoff     = 0.35f;
        p.resonance  = 0.35f;
        p.filter_env = 0.80f;
        p.attack     = 0.00f;
        p.decay      = 0.25f;
        p.sustain    = 0.45f;
        p.type[TextureChain::kCharacter]      = Character::kFold;
        p.amount[TextureChain::kCharacter]    = 0.40f;
        p.secondary[TextureChain::kCharacter] = 0.40f;
        add("Fold Bass", p);
    }

    // 14. Vowels, moving slowly, in a wide room.
    {
        Patch p;
        p.engine    = EngineId::kFormant;
        p.timbre    = 0.45f;
        p.morph     = 0.60f;
        p.spread    = 0.40f;
        p.cutoff    = 0.65f;
        p.attack    = 0.25f;
        p.decay    = 0.55f;
        p.sustain   = 0.85f;
        p.lfo_dest  = LfoDest::kTimbre;
        p.lfo_rate  = 0.20f;
        p.lfo_depth = 0.30f;
        p.type[TextureChain::kMovement]      = Movement::kChorus;
        p.amount[TextureChain::kMovement]    = 0.40f;
        p.secondary[TextureChain::kMovement] = 0.30f;
        p.type[TextureChain::kSpace]         = Space::kWash;
        p.amount[TextureChain::kSpace]       = 0.45f;
        p.space_time                         = 0.55f;
        p.space_feedback                     = 0.50f;
        add("Vowel Pad", p);
    }

    // 15. Plucked, then squashed flat -- the compressor is the sound.
    {
        Patch p;
        p.engine    = EngineId::kVariableSaw;
        p.timbre    = 0.65f;
        p.cutoff    = 0.70f;
        p.resonance = 0.20f;
        p.attack    = 0.00f;
        p.decay     = 0.22f;
        p.sustain   = 0.05f;
        p.type[TextureChain::kTexture]     = Texture::kSquash;
        p.amount[TextureChain::kTexture]   = 0.60f;
        p.secondary[TextureChain::kTexture] = 0.55f;
        p.type[TextureChain::kSpace]       = Space::kEcho;
        p.amount[TextureChain::kSpace]     = 0.40f;
        p.space_time                       = 0.33f;
        p.space_feedback                   = 0.50f;
        p.space_grain                      = 0.35f;
        add("Squash Pluck", p);
    }

    // 16. Nothing sits still: phaser under a cloud, both drifting.
    {
        Patch p;
        p.engine    = EngineId::kHarmonic;
        p.harmonics = 0.45f;
        p.spread    = 0.30f;
        p.cutoff    = 0.60f;
        p.attack    = 0.35f;
        p.decay    = 0.70f;
        p.sustain   = 0.90f;
        p.lfo_dest  = LfoDest::kCutoff;
        p.lfo_rate  = 0.12f;
        p.lfo_depth = 0.40f;
        p.type[TextureChain::kMovement]      = Movement::kPhaser;
        p.amount[TextureChain::kMovement]    = 0.50f;
        p.secondary[TextureChain::kMovement] = 0.15f;
        p.type[TextureChain::kSpace]         = Space::kCloud;
        p.amount[TextureChain::kSpace]       = 0.55f;
        p.space_time                         = 0.65f;
        p.space_feedback                     = 0.60f;
        p.space_grain                        = 0.45f;
        p.grain_spread                       = 0.65f;
        add("Phase Drift", p);
    }
}

} // namespace rodeo002
