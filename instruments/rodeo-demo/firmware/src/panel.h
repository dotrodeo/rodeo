// The panel: a pot bank and an encoder bank on the satellite chain, in any order
// and anywhere on it, and what each control does. The fixed map from the concept
// (../docs/RODEO_DEMO_CONCEPT.md), with no pages:
//
//   Pot 1  cutoff              Pot 3  filter envelope amount
//   Pot 2  resonance           Pot 4  release
//
//   E1 turn  the synth's shape, or (sampler) which sound;  tap  synth / sampler / both
//   E2 turn  detune; held, octave                          tap  detune and octave back
//   E3 turn  attack                                        tap  LFO on / off
//   E4 turn  effects mix                                   tap  chorus / reverb / both
//
// A tap is a press let go without turning. Holding E1 and E4 together for a
// second asks for disk mode (and taps neither).
//
// The encoder bank's LED shows the layer (steady: synth, slow blink: sampler,
// fast blink: both); the pot bank's lights while notes sound, blinks fast while
// samples are still loading, and slowly in disk mode.
#pragma once
#include <stdint.h>
#include "sat_chain.h"
#include "synth.h"

namespace panel
{
enum class Status
{
    Playing,
    Loading, // samples still coming off the card
    Disk,    // the computer has the card
};

/** Read the controls into p, and set the LEDs. From the main loop. Returns a
 *  description of what changed, for the serial port, or null. */
const char* Update(rodeo::SatChain& chain, synth::Params& p, Status status);

/** E1 and E4 held together for a second, since the last call. */
bool DiskRequested();

/** Whether a pot bank and an encoder bank are both on the chain. */
bool Found();
} // namespace panel
