// The panel: two pot banks and two encoder banks on the satellite chain, anywhere
// on it, and what each control does. Of each kind, the first after the Seed is
// bank 1 and the next bank 2; swap the cables to swap them. Either second bank
// can be left off: its parameters then stay where a preset or MIDI puts them.
// The fixed map from the concept (../docs/RODEO_DEMO_CONCEPT.md), with no pages:
//
//   Pot bank 1: the filter              Pot bank 2: the amp envelope
//   Pot 1  cutoff                       Pot 5  attack   (1 ms .. 4 s)
//   Pot 2  resonance                    Pot 6  decay    (1 ms .. 10 s)
//   Pot 3  filter envelope amount       Pot 7  sustain
//   Pot 4  drive                        Pot 8  release  (5 ms .. 4 s)
//
//   Encoder bank 1
//   E1 turn  the synth's shape, or (sampler) which sound;  tap  synth / sampler / both
//      held, turn  the next or previous preset (presets.h)
//   E2 turn  detune; held, octave                          tap  detune and octave back
//   E3 turn  high-pass (off, 20 Hz .. 2 kHz)               tap  LFO on / off
//   E4 turn  effects mix                                   tap  chorus / reverb / both
//
//   Encoder bank 2: the filter envelope, and mono
//   E5 turn  filter attack (follows attack, 1 ms .. 4 s)   tap  back to following attack
//   E6 turn  filter decay (1 ms .. 10 s)                   tap  back to 400 ms
//   E7 turn  filter sustain                                tap  back to 25 %
//   E8 turn  glide (off, 5 ms .. 2 s)                      tap  mono on / off
//
// A preset puts its own values on the pots' parameters; each pot takes over
// again, from where it is, once it is moved.
//
// A tap is a press let go without turning. Holding E1 and E4 together for a
// second asks for disk mode (and taps neither).
//
// Encoder bank 1's LED shows the layer (steady: synth, slow blink: sampler,
// fast blink: both); encoder bank 2's is bright in mono. The pot banks' light
// while notes sound, blink fast while samples are still loading, and slowly in
// disk mode.
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

/** Load preset i into p, and hold the pots' parameters it sets until each pot
 *  moves. Returns a description for the serial port. */
const char* LoadPreset(int i, synth::Params& p);

/** Set parameter id (params.h) to v in its units, from MIDI: as if its control
 *  had moved there. A pot's parameter is held, as a preset holds it, until the pot
 *  moves. Returns a description for the serial port, or null for an unknown id. */
const char* Set(int id, int v, synth::Params& p);

/** The pots' parameters a preset or Set() holds, one bit a pot, pot 1 first
 *  (presets::PotBits): 8 bits. */
unsigned Held();

/** The preset loaded last, or -1. */
int Preset();

/** E1 and E4 held together for a second, since the last call. */
bool DiskRequested();

/** Whether a pot bank and an encoder bank are both on the chain: bank 1 of each. */
bool Found();

/** How many pot banks, and encoder banks, the panel uses: 0 to 2 each. */
int PotBanks();
int EncoderBanks();
} // namespace panel
