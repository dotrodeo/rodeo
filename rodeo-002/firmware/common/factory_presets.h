// rodeo-002 — the factory bank: the sounds a blank instrument boots with.
//
// These are the defaults handed to the preset store, so an instrument that has
// never been saved to still has somewhere to start, and a store that is erased
// comes back with them rather than sixteen empty slots.
//
// REBUILT 2026-09-29, from nothing, for the instrument as it now is: seven
// engines, the sub-oscillator, Spread as stereo width, and voices that are
// tuned slightly wrong. The old bank predated all of that -- it was written when
// Spread did almost nothing, every voice landed in the same place and there was
// no sub -- so half of these controls sat at their defaults in every slot.
//
// The bank is meant to cover the instrument, not to be sixteen good sounds in
// the same corner of it: all seven engines, all three voice modes, all three
// filter types, every Space type, the sub above and below the note and off, and
// a mod-wheel destination worth reaching for in each.
//
// CHANGING THIS FILE DOES NOT CHANGE THE INSTRUMENT ON ITS OWN. The stored bank
// is only replaced when it is invalid or when you ask: the version is unchanged,
// so a board that already has a bank keeps it. Factory Reset in the menu is what
// loads these.

#pragma once

#include "preset.h"

namespace rodeo002
{
/** Somewhere to start from: the raw voice, nothing in the chain.
 *
 *  Named rather than inlined because it is the patch an EMPTY user slot plays.
 *  An empty slot is still somewhere to go -- NAV cycles onto it like any other
 *  -- and a slot that makes no sound is not somewhere to go (Derek,
 *  2026-09-29: "I don't think empty user slots should be silent"). */
inline Patch InitPatch()
{
    Patch p;
    // Off the waveshape midpoint on purpose. At morph 0.5 the virtual-analog
    // oscillator's pulse width cancels exactly, so Timbre -- which drives it --
    // does nothing at all, and the first knob a new owner turns should not be a
    // dead one.
    p.morph = 0.35f;
    return p;
}

/** The name an unsaved slot answers to. It says what the slot IS: nothing has
 *  been put here yet, even though turning to it makes a sound. */
constexpr char kEmptySlotName[] = "-- empty --";

/** Fill `b` with the factory bank: sixteen sounds, and the rest holding Init
 *  under an empty name, for you to save over. */
inline void BuildFactoryBank(PresetBank& b)
{
    // Every byte set, including the header's own: the bank is compared byte for
    // byte against what is in flash to decide whether a write is needed.
    b.magic   = PresetBank::kMagic;
    b.version = PresetBank::kVersion;
    b.slots   = PresetBank::kNumSlots;

    // Empty everywhere first, and empty means Init: an unused slot holds a
    // patch you can play, so a corrupt `used` byte cannot produce a nonsense
    // sound -- and, more to the point, so cycling onto one is a sound rather
    // than a silence.
    for(int i = 0; i < PresetBank::kNumSlots; i++)
    {
        PatchToPreset(InitPatch(), kEmptySlotName, b.slot[i]);
        b.slot[i].used = 0;
    }

    int n = 0;
    // Taken by value so the fall time can be normalised: the envelope is ADS
    // (Derek, 2026-09-24), so decay IS the release, and a preset must not carry
    // a release the panel cannot express.
    auto add = [&](const char* name, Patch p) {
        p.release = p.decay;
        if(n < PresetBank::kNumSlots)
            PatchToPreset(p, name, b.slot[n++]);
    };

    using namespace fx;

    // 1. Somewhere to start from, and what every empty slot plays. Not really a
    //    sound: the zero point, one detent away whatever you are doing.
    add("Init", InitPatch());

    // 2. Why the voices are panned at all: one chord, opened right out. Spread
    //    is doing two jobs here and they are the same job -- the voices sit
    //    across the field AND the sub is an octave under them.
    {
        Patch p;
        p.engine       = EngineId::kVirtualAnalog;
        p.morph        = 0.30f;
        p.timbre       = 0.55f;
        p.spread       = 0.85f;
        p.cutoff       = 0.62f;
        p.resonance    = 0.12f;
        p.attack       = 0.30f;
        p.decay        = 0.55f;
        p.sustain      = 0.85f;
        p.sub_level    = 0.35f;
        p.sub_interval = SubInterval::kMinus12;
        p.sub_wave     = SubWave::kSine;
        p.lfo_dest     = LfoDest::kMorph;
        p.lfo_rate     = 0.12f;
        p.lfo_depth    = 0.20f;
        p.mod_dest     = ModDest::kCutoff;
        p.type[TextureChain::kMovement]      = Movement::kChorus;
        p.amount[TextureChain::kMovement]    = 0.40f;
        p.secondary[TextureChain::kMovement] = 0.22f;
        p.type[TextureChain::kSpace]         = Space::kWash;
        p.amount[TextureChain::kSpace]       = 0.45f;
        p.space_time                         = 0.60f;
        p.space_feedback                     = 0.50f;
        p.space_grain                        = 0.55f;
        p.tilt                               = 0.55f;
        add("Wide Open", p);
    }

    // 3. What the sub is for. Mono, square an octave down at nearly full level,
    //    filter low enough that the sub is most of what you hear.
    {
        Patch p;
        p.engine       = EngineId::kVariableSaw;
        p.voice_mode   = VoiceMode::kMono;
        p.timbre       = 0.40f;
        p.morph        = 0.25f;
        p.cutoff       = 0.32f;
        p.resonance    = 0.30f;
        p.attack       = 0.00f;
        p.decay        = 0.18f;
        p.sustain      = 0.75f;
        p.filter_env   = 0.68f;
        p.vel_to_filter = 0.45f;
        p.glide        = 0.06f;
        p.sub_level    = 0.85f;
        p.sub_interval = SubInterval::kMinus12;
        p.sub_wave     = SubWave::kSquare;
        p.mod_dest     = ModDest::kCutoff;
        p.type[TextureChain::kCharacter]   = Character::kDrive;
        p.amount[TextureChain::kCharacter] = 0.35f;
        p.option[TextureChain::kCharacter] = Character::kCut100; // keep the sub clean
        p.bypass[TextureChain::kMovement]  = true;
        p.bypass[TextureChain::kTexture]   = true;
        p.type[TextureChain::kSpace]       = Space::kEcho;
        p.amount[TextureChain::kSpace]     = 0.12f;
        p.space_time                       = 0.18f;
        p.space_feedback                   = 0.20f;
        add("Sub Bass", p);
    }

    // 4. Unison as it should have sounded all along: eight voices at +/-25
    //    cents, spread across the field rather than stacked on one point.
    {
        Patch p;
        p.engine     = EngineId::kVariableSaw;
        p.voice_mode = VoiceMode::kUnison;
        p.spread     = 0.55f;
        p.timbre     = 0.60f;
        p.morph      = 0.45f;
        p.cutoff     = 0.58f;
        p.resonance  = 0.18f;
        p.attack     = 0.08f;
        p.decay      = 0.30f;
        p.sustain    = 0.80f;
        p.filter_env = 0.66f;
        p.keytrack   = 0.62f;
        p.mod_dest   = ModDest::kCutoff;
        p.type[TextureChain::kCharacter]   = Character::kWarm;
        p.amount[TextureChain::kCharacter] = 0.40f;
        p.type[TextureChain::kSpace]       = Space::kReverb;
        p.amount[TextureChain::kSpace]     = 0.28f;
        p.space_time                       = 0.45f;
        p.space_feedback                   = 0.40f;
        add("Unison Brass", p);
    }

    // 5. FM tines, with the sub an octave UP -- the interval knob going the
    //    other way is where the bell comes from.
    {
        Patch p;
        p.engine       = EngineId::kFm2;
        p.harmonics    = 0.55f; // ratio
        p.timbre       = 0.42f; // index
        p.spread       = 0.45f;
        p.cutoff       = 0.86f;
        p.resonance    = 0.08f;
        p.attack       = 0.00f;
        p.decay        = 0.38f;
        p.sustain      = 0.25f;
        p.env_to_timbre = 0.72f; // index falls with the envelope: FM's whole character
        p.sub_level    = 0.22f;
        p.sub_interval = SubInterval::kPlus12;
        p.sub_wave     = SubWave::kSine;
        p.vel_to_filter = 0.35f;
        p.type[TextureChain::kTexture]   = Texture::kTape;
        p.amount[TextureChain::kTexture] = 0.22f;
        p.type[TextureChain::kSpace]     = Space::kEcho;
        p.amount[TextureChain::kSpace]   = 0.32f;
        p.space_time                     = 0.26f;
        p.space_feedback                 = 0.38f;
        add("Glass Tines", p);
    }

    // 6. Vowels, wide and slow. Vosim caps at seven voices, which a pad never
    //    notices.
    {
        Patch p;
        p.engine    = EngineId::kVosim;
        p.harmonics = 0.45f;
        p.timbre    = 0.55f;
        p.morph     = 0.40f;
        p.spread    = 0.90f;
        p.cutoff    = 0.70f;
        p.resonance = 0.14f;
        p.attack    = 0.42f;
        p.decay     = 0.65f;
        p.sustain   = 0.85f;
        p.lfo_dest  = LfoDest::kTimbre;
        p.lfo_rate  = 0.10f;
        p.lfo_depth = 0.30f;
        p.lfo_shape = Lfo::kTriangle;
        p.mod_dest  = ModDest::kTimbre;
        p.type[TextureChain::kMovement]      = Movement::kChorus;
        p.amount[TextureChain::kMovement]    = 0.35f;
        p.secondary[TextureChain::kMovement] = 0.15f;
        p.type[TextureChain::kSpace]         = Space::kWash;
        p.amount[TextureChain::kSpace]       = 0.55f;
        p.space_time                         = 0.70f;
        p.space_feedback                     = 0.62f;
        p.space_grain                        = 0.60f;
        p.tilt                               = 0.60f;
        add("Vowel Choir", p);
    }

    // 7. Sixteen partials left to wander: no LFO, nothing moving on purpose.
    //    What you hear drifting is the voices' own tuning error.
    {
        Patch p;
        p.engine    = EngineId::kHarmonic;
        p.harmonics = 0.38f;
        p.timbre    = 0.50f;
        p.morph     = 0.55f;
        p.spread    = 1.00f;
        p.cutoff    = 0.52f;
        p.resonance = 0.10f;
        p.attack    = 0.60f;
        p.decay     = 0.80f;
        p.sustain   = 0.90f;
        p.sub_level = 0.18f;
        p.sub_wave  = SubWave::kTriangle;
        p.mod_dest  = ModDest::kSpaceMix;
        p.bypass[TextureChain::kMovement] = true;
        p.type[TextureChain::kSpace]      = Space::kReverb;
        p.amount[TextureChain::kSpace]    = 0.60f;
        p.space_time                      = 0.80f;
        p.space_feedback                  = 0.70f;
        p.tilt                            = 0.62f;
        add("Slow Drift", p);
    }

    // 8. Z-osc is the dearest engine left, so this is Mono on purpose: all of
    //    the budget in one voice, and the filter envelope doing the work.
    {
        Patch p;
        p.engine       = EngineId::kZOsc;
        p.voice_mode   = VoiceMode::kMono;
        p.harmonics    = 0.60f;
        p.timbre       = 0.55f;
        p.morph        = 0.50f;
        p.cutoff       = 0.30f;
        p.resonance    = 0.52f;
        p.attack       = 0.01f;
        p.decay        = 0.22f;
        p.sustain      = 0.55f;
        p.filter_env   = 0.80f;
        p.glide        = 0.08f;
        p.bend_range   = 12;
        p.env_to_morph = 0.62f;
        p.mod_dest     = ModDest::kCutoff;
        p.type[TextureChain::kCharacter]   = Character::kFold;
        p.amount[TextureChain::kCharacter] = 0.30f;
        p.type[TextureChain::kSpace]       = Space::kEcho;
        p.amount[TextureChain::kSpace]     = 0.22f;
        p.space_time                       = 0.22f;
        p.space_feedback                   = 0.45f;
        add("Z Growl", p);
    }

    // 9. Formants under a phaser: two kinds of movement that do not fight.
    {
        Patch p;
        p.engine    = EngineId::kFormant;
        p.harmonics = 0.35f;
        p.timbre    = 0.60f;
        p.morph     = 0.45f;
        p.spread    = 0.70f;
        p.cutoff    = 0.75f;
        p.resonance = 0.16f;
        p.attack    = 0.25f;
        p.decay     = 0.50f;
        p.sustain   = 0.80f;
        p.lfo_dest  = LfoDest::kHarmonics;
        p.lfo_rate  = 0.18f;
        p.lfo_depth = 0.26f;
        p.type[TextureChain::kMovement]      = Movement::kPhaser;
        p.amount[TextureChain::kMovement]    = 0.50f;
        p.secondary[TextureChain::kMovement] = 0.18f;
        p.type[TextureChain::kSpace]         = Space::kWash;
        p.amount[TextureChain::kSpace]       = 0.40f;
        p.space_time                         = 0.55f;
        p.space_feedback                     = 0.48f;
        add("Formant Pad", p);
    }

    // 10. The sub at a fifth, which stops being a sub and becomes a chord.
    {
        Patch p;
        p.engine       = EngineId::kVirtualAnalog;
        p.morph        = 0.42f;
        p.timbre       = 0.35f;
        p.spread       = 0.60f;
        p.cutoff       = 0.66f;
        p.resonance    = 0.20f;
        p.attack       = 0.02f;
        p.decay        = 0.34f;
        p.sustain      = 0.70f;
        p.sub_level    = 0.55f;
        p.sub_interval = SubInterval::kPlus7;
        p.sub_wave     = SubWave::kSaw;
        p.filter_env   = 0.62f;
        p.type[TextureChain::kCharacter]   = Character::kWarm;
        p.amount[TextureChain::kCharacter] = 0.28f;
        p.type[TextureChain::kSpace]       = Space::kEcho;
        p.amount[TextureChain::kSpace]     = 0.30f;
        p.space_time                       = 0.30f;
        p.space_feedback                   = 0.42f;
        add("Fifth Stack", p);
    }

    // 11. Struck, then played backwards.
    {
        Patch p;
        p.engine        = EngineId::kFm2;
        p.harmonics     = 0.70f;
        p.timbre        = 0.30f;
        p.spread        = 0.50f;
        p.cutoff        = 0.80f;
        p.resonance     = 0.12f;
        p.attack        = 0.00f;
        p.decay         = 0.22f;
        p.sustain       = 0.00f; // struck: no sustain at all
        p.env_to_timbre = 0.68f;
        p.sub_level     = 0.20f;
        p.sub_interval  = SubInterval::kPlus12;
        p.vel_to_filter = 0.40f;
        p.type[TextureChain::kSpace]   = Space::kReverse;
        p.amount[TextureChain::kSpace] = 0.55f;
        p.space_time                   = 0.42f;
        p.space_feedback               = 0.30f;
        add("Reverse Bell", p);
    }

    // 12. Bit-crushed and short. The Texture slot doing the harm, Character
    //     staying out of it.
    {
        Patch p;
        p.engine        = EngineId::kVirtualAnalog;
        p.morph         = 0.20f;
        p.timbre        = 0.70f;
        p.spread        = 0.45f;
        p.cutoff        = 0.72f;
        p.resonance     = 0.35f;
        p.attack        = 0.00f;
        p.decay         = 0.14f;
        p.sustain       = 0.10f;
        p.filter_env    = 0.72f;
        p.vel_to_filter = 0.50f;
        p.type[TextureChain::kTexture]      = Texture::kCrush;
        p.amount[TextureChain::kTexture]    = 0.45f;
        p.secondary[TextureChain::kTexture] = 0.60f;
        p.bypass[TextureChain::kCharacter]  = true;
        p.type[TextureChain::kSpace]        = Space::kEcho;
        p.amount[TextureChain::kSpace]      = 0.35f;
        p.space_time                        = 0.20f;
        p.space_feedback                    = 0.50f;
        add("Crush Pluck", p);
    }

    // 13. Nothing but air: the Space slot frozen, so it holds whatever was in
    //     it when you pressed. Freeze belongs to Space, which is why it is
    //     shown on that cell.
    {
        Patch p;
        p.engine    = EngineId::kHarmonic;
        p.harmonics = 0.55f;
        p.timbre    = 0.40f;
        p.morph     = 0.60f;
        p.spread    = 0.95f;
        p.cutoff    = 0.60f;
        p.resonance = 0.10f;
        p.attack    = 0.50f;
        p.decay     = 0.70f;
        p.sustain   = 0.85f;
        p.freeze    = true;
        p.lfo_dest  = LfoDest::kSpaceMix;
        p.lfo_rate  = 0.08f;
        p.lfo_depth = 0.35f;
        p.type[TextureChain::kSpace]   = Space::kCloud;
        p.amount[TextureChain::kSpace] = 0.70f;
        p.space_time                   = 0.65f;
        p.space_feedback               = 0.60f;
        p.space_grain                  = 0.70f;
        p.grain_pitch                  = 0.62f;
        p.grain_spread                 = 0.55f;
        p.option[TextureChain::kSpace] = Space::kOctaves;
        add("Freeze Cloud", p);
    }

    // 14. A lead that means the mod wheel: it opens the filter, and the bend
    //     range is an octave so the wheel and the stick both do something big.
    {
        Patch p;
        p.engine       = EngineId::kVirtualAnalog;
        p.voice_mode   = VoiceMode::kMono;
        p.morph        = 0.55f;
        p.timbre       = 0.50f;
        p.cutoff       = 0.45f;
        p.resonance    = 0.42f;
        p.attack       = 0.01f;
        p.decay        = 0.26f;
        p.sustain      = 0.85f;
        p.glide        = 0.12f;
        p.bend_range   = 12;
        p.keytrack     = 0.60f;
        p.sub_level    = 0.30f;
        p.sub_interval = SubInterval::kPlus12;
        p.sub_wave     = SubWave::kSquare;
        p.mod_dest     = ModDest::kCutoff;
        p.type[TextureChain::kCharacter]   = Character::kDrive;
        p.amount[TextureChain::kCharacter] = 0.40f;
        p.type[TextureChain::kSpace]       = Space::kEcho;
        p.amount[TextureChain::kSpace]     = 0.28f;
        p.space_time                       = 0.24f;
        p.space_feedback                   = 0.52f;
        add("Octave Lead", p);
    }

    // 15. High-pass, so what is left is the attack and none of the body.
    {
        Patch p;
        p.engine        = EngineId::kVariableSaw;
        p.timbre        = 0.65f;
        p.morph         = 0.30f;
        p.spread        = 0.65f;
        p.filter        = FilterType::kHighPass;
        p.cutoff        = 0.30f;
        p.resonance     = 0.30f;
        p.attack        = 0.00f;
        p.decay         = 0.20f;
        p.sustain       = 0.15f;
        p.vel_to_filter = 0.55f;
        p.type[TextureChain::kTexture]   = Texture::kDust;
        p.amount[TextureChain::kTexture] = 0.30f;
        p.type[TextureChain::kSpace]     = Space::kReverb;
        p.amount[TextureChain::kSpace]   = 0.42f;
        p.space_time                     = 0.50f;
        p.space_feedback                 = 0.45f;
        p.tilt                           = 0.60f;
        add("Dust Tick", p);
    }

    // 16. A drone to leave running: square LFO on the amplitude, so it chops
    //     itself, and a band-pass so it sits in the middle of the room.
    {
        Patch p;
        p.engine    = EngineId::kVosim;
        p.harmonics = 0.60f;
        p.timbre    = 0.45f;
        p.morph     = 0.55f;
        p.spread    = 0.80f;
        p.filter    = FilterType::kBandPass;
        p.cutoff    = 0.50f;
        p.resonance = 0.45f;
        p.attack    = 0.35f;
        p.decay     = 0.85f;
        p.sustain   = 1.00f;
        p.lfo_dest  = LfoDest::kAmp;
        p.lfo_rate  = 0.42f;
        p.lfo_depth = 0.55f;
        p.lfo_shape = Lfo::kSquare;
        p.sub_level = 0.25f;
        p.sub_wave  = SubWave::kTriangle;
        p.mod_dest  = ModDest::kLfoDepth;
        p.type[TextureChain::kMovement]      = Movement::kTremolo;
        p.amount[TextureChain::kMovement]    = 0.25f;
        p.secondary[TextureChain::kMovement] = 0.30f;
        p.type[TextureChain::kSpace]         = Space::kWash;
        p.amount[TextureChain::kSpace]       = 0.58f;
        p.space_time                         = 0.72f;
        p.space_feedback                     = 0.66f;
        p.space_grain                        = 0.55f;
        add("Pulse Drone", p);
    }

}

} // namespace rodeo002
