// rodeo-002 — the LFO destinations that live in the CHAIN.
//
// Most destinations are inside a voice, where VoiceManager applies them. Two
// are not: they modulate a texture-chain slot's amount, and the chain is shared
// by every voice.
//
// This is a header rather than a few lines in the app because a destination
// that quietly does nothing is a real failure mode and an easy one: "Space mix"
// was on the list from the start, was offered on the panel and in presets, and
// was never wired to anything -- the LFO value had a public accessor that no
// caller ever called (found 2026-09-24). Here it can be tested.

#pragma once

#include "dsp_util.h"
#include "patch.h"
#include "texture_chain.h"

namespace rodeo002
{
/** True if this destination is applied to the chain rather than to a voice. */
inline bool LfoDestIsChain(LfoDest d)
{
    return d == LfoDest::kSpaceMix || d == LfoDest::kCharacterAmt;
}

/** Apply the LFO to whichever slot amount it targets, leaving the patch alone.
 *  Call once per block: the slot's own smoothers turn the steps into a ramp,
 *  and the control rate the app applies patches at (~200 Hz) would not be fast
 *  enough for an LFO that reaches 40 Hz. */
inline void ApplyLfoToChain(fx::TextureChain& chain, const Patch& p, float lfo)
{
    const int slot = p.lfo_dest == LfoDest::kSpaceMix       ? fx::TextureChain::kSpace
                     : p.lfo_dest == LfoDest::kCharacterAmt ? fx::TextureChain::kCharacter
                                                            : -1;
    if(slot < 0)
        return;
    chain.SetAmount(slot, fx::Clamp(p.amount[slot] + lfo * 0.5f, 0.f, 1.f));
}

/** The mod wheel's chain destinations, by the same rule and for the same
 *  reason: applied once per block, because the patch rate is not fast enough to
 *  follow a hand. Unipolar, so it only ever adds. */
inline void ApplyModWheelToChain(fx::TextureChain& chain, const Patch& p, float wheel)
{
    const int slot = p.mod_dest == ModDest::kSpaceMix       ? fx::TextureChain::kSpace
                     : p.mod_dest == ModDest::kCharacterAmt ? fx::TextureChain::kCharacter
                                                            : -1;
    if(slot < 0)
        return;
    chain.SetAmount(slot, fx::Clamp(p.amount[slot] + wheel * 0.5f, 0.f, 1.f));
}

} // namespace rodeo002
