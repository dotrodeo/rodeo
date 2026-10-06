// rodeo-002 — what everything costs, and how many voices that leaves.
//
// An overload on this board is an audible dropout, not a slowdown, and the
// player can reach the expensive corners freely: any engine, any Space type.
// So polyphony is not a constant -- it is computed from measured costs
// whenever the engine or a slot's type changes, and shown on the OLED.
//
//   load = kFixedLoad + voices x engine cost + the active slots' costs
//
// That model is not a guess: on hardware every engine's load is linear in voice
// count with an intercept under 0.3% (bench_voices, 2026-09-22), and the
// chain's worst case measured 35.4% against 35.3% summed from its parts.
//
// ALL FIGURES ARE MEASURED, not estimated:
//   * engines: least-squares fit over the voice sweep, chain bypassed
//   * effects: load added over an all-bypassed baseline, at their heaviest
//     settings (full amount; for Space, dense pitched grains with feedback)
//   * both under the Daisy bootloader (BOOT_SRAM), from raw timer ticks
//   * logs: rodeo-002/docs/bench_voices-2026-09-22-optimised.log
//           rodeo-002/docs/bench_space-2026-09-22.log
//
// Re-measure after any DSP change: these numbers are a snapshot of the code,
// and the cap is only as honest as they are.

#pragma once

#include "dsp_util.h"
#include "engines.h"
#include "texture_chain.h"

namespace rodeo002
{
class CostModel
{
  public:
    /** Audio budget. The rest is the UI's: pot scanning over SPI, encoder
     *  polling and OLED redraws over I2C, MIDI. Audio runs in interrupt
     *  context and the UI in what is left. */
    static constexpr float kBudget = 70.f;

    /** The audio callback's own overhead: voice summing, and the chain's
     *  output stage (tilt, volume, limiter) which runs even with every slot
     *  bypassed. */
    static constexpr float kFixedLoad = 1.3f;

    /** Design limit. No patch offers more, whatever the cost model allows. */
    static constexpr int kMaxPoly = 8;

    /** Cost of one voice, % CPU. */
    static float VoiceCost(EngineId id)
    {
        switch(id)
        {
            case EngineId::kVariableSaw: return 3.01f;
            case EngineId::kVirtualAnalog: return 3.35f;
            case EngineId::kFormant: return 3.95f;
            case EngineId::kFm2: return 6.15f;
            case EngineId::kHarmonic: return 6.82f;
            case EngineId::kVosim: return 8.78f;
            default: return 9.02f; // kZOsc, the dearest one left
        }
    }

    // IsMonoOnly lived here. String (16.43%) and Karplus (14.04%) were its
    // only members, and Modal (46% for ONE voice) before them. With those gone
    // the dearest engine left is ZOsc at 9.02%: seven voices on a bare chain,
    // not eight, but seven is polyphony. Every engine is playable in Poly now,
    // so there is nothing left to override and the cap is purely budget
    // arithmetic (2026-09-29).

    /** Cost of one effect type at its heaviest, % CPU. A bypassed slot is 0. */
    static float EffectCost(int slot, int type)
    {
        switch(slot)
        {
            case fx::TextureChain::kCharacter:
            {   // enum order: Warm, Drive, Fuzz, Fold, Rectify
                static const float k[] = {3.0f, 1.7f, 2.2f, 1.9f, 2.7f};
                return Pick(k, 5, type);
            }
            case fx::TextureChain::kMovement:
            {   // Chorus, Vibrato, Phaser, Flanger, Tremolo
                static const float k[] = {3.6f, 3.4f, 5.2f, 3.6f, 1.4f};
                return Pick(k, 5, type);
            }
            case fx::TextureChain::kTexture:
            {   // Crush, Downsample, Tape, Dust, Squash
                static const float k[] = {1.9f, 1.3f, 6.5f, 2.6f, 2.4f};
                return Pick(k, 5, type);
            }
            default:
            {   // Echo, Cloud, Reverse, Wash, Reverb
                static const float k[] = {7.8f, 11.0f, 5.3f, 11.7f, 20.6f};
                return Pick(k, 5, type);
            }
        }
    }

    /** What the chain as configured costs. */
    static float ChainCost(const fx::TextureChain& chain)
    {
        float sum = 0.f;
        for(int s = 0; s < fx::TextureChain::kNumSlots; s++)
            if(!chain.GetBypass(s))
                sum += EffectCost(s, chain.GetType(s));
        return sum;
    }

    /** What the sub-oscillator adds to a voice, % CPU, when it is switched on.
     *
     *  NOT MEASURED ON HARDWARE YET -- every other number in this file is, and
     *  this one is an estimate pending a run of bench_voices, which has a SUB
     *  pass that prints exactly this figure. It is deliberately generous: a
     *  polyBLEP oscillator should cost well under half the cheapest macro
     *  engine (variable saw, 3.01%), and over-charging costs a voice where
     *  under-charging costs an overrun. Correct it from the bench output. */
    static constexpr float kSubCost = 1.5f;

    /** Voices that fit alongside `chain_cost`, at least 1 and never more than
     *  kMaxPoly. Every engine is polyphonic now, so the cap is purely what the
     *  budget allows.
     *
     *  `sub_on` matters: the sub is charged for only when a patch actually uses
     *  it, which is what keeps a preset that does not use it at full polyphony.
     *  Z-osc is the case that needs it -- seven voices at 9.02% leaves 5.2% of
     *  the budget, and seven subs would not fit in it. */
    static int MaxVoices(EngineId id, float chain_cost, bool sub_on = false)
    {
        const float spare = kBudget - kFixedLoad - chain_cost;
        const float per   = VoiceCost(id) + (sub_on ? kSubCost : 0.f);
        const int   n     = static_cast<int>(spare / per);
        return n < 1 ? 1 : (n > kMaxPoly ? kMaxPoly : n);
    }

    /** Predicted load for a configuration -- for the OLED, and for tests that
     *  check the model against the hardware logs. */
    static float PredictLoad(EngineId id, int voices, float chain_cost, bool sub_on = false)
    {
        return kFixedLoad
               + static_cast<float>(voices) * (VoiceCost(id) + (sub_on ? kSubCost : 0.f))
               + chain_cost;
    }

    /** True when even one voice does not fit: the patch is beyond the board,
     *  and the firmware has to give something up rather than drop out. */
    static bool Overcommitted(EngineId id, float chain_cost)
    {
        return PredictLoad(id, 1, chain_cost) > kBudget;
    }

  private:
    static float Pick(const float* table, int n, int i)
    {
        return (i >= 0 && i < n) ? table[i] : table[0];
    }
};

} // namespace rodeo002
