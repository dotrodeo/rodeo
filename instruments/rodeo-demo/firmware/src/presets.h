// Presets: text files on the SD card, written on the computer (in disk mode, or
// with the card out) and only ever read by the Rodeo.
//
//   /presets/<name>.txt   one preset a file, in name order; the file's name,
//                         without .txt, is the preset's
//
// A file is `key = value` lines; `#` or `;` starts a comment. Every key is
// optional:
//
//   layer     = synth | sampler | both
//   shape     = saw | square | pulse | triangle | saw + sub | square + sub | fm
//   sound     = <name>   a sound on the card by name (a WAV's name without .wav,
//                        or a kit's folder), not by number, so adding files
//                        doesn't change what a preset plays
//   detune    = 0..50    cents
//   octave    = -2..2
//   attack    = ms
//   release   = ms       (pot 4)
//   cutoff    = 0..100   % of the pot's travel (pot 1)
//   resonance = 0..100   % (pot 2)
//   env       = 0..100   % (pot 3)
//   lfo       = on | off
//   fx        = chorus | reverb | both
//   mix       = 0..100   %
//
// and these, which no control on the panel reaches:
//
//   decay     = ms       amp envelope (200)
//   sustain   = 0..100   % (85)
//   fattack   = ms       filter envelope; without it, the same as attack
//   fdecay    = ms       (400)
//   fsustain  = 0..100   % (25)
//   filter    = svf | ladder | ladder12   12 dB state-variable (the default), a
//                        24 dB ladder, or the ladder's 12 dB tap
//   drive     = 0..100   % into the filter
//   hpf       = Hz       a high-pass before the filter; 0 for none
//   keytrack  = 0..100   % the cutoff follows the key, from middle C
//   mono      = on | off one voice, last key wins, legato (synth layer only)
//   glide     = ms       in mono
//
// What a preset leaves out takes the power-up default, except the four pots'
// parameters: a preset without cutoff leaves cutoff to pot 1. What it does set
// holds until that pot is moved (panel.h). Lines the parser can't read are
// skipped and reported on the serial port.
#pragma once
#include "synth.h"

namespace presets
{
/** Bit for each pot parameter a preset sets, pot 1 first. */
enum PotBits : unsigned
{
    kCutoff    = 1,
    kResonance = 2,
    kEnv       = 4,
    kRelease   = 8,
};

/** Read every preset off the card (mounted: after samples::Mount() or
 *  Remount()). Returns how many. */
int Load();

int         Count();
const char* Name(int i);

/** Preset i into p, and which of the pots' parameters it set. The sound is
 *  looked up by name in the sample library as it is now. */
unsigned Apply(int i, synth::Params& p);

/** The sound the last Apply() asked for and the card doesn't have, or null. */
const char* MissingSound();

/** What Load() couldn't read, for the serial port; empty if nothing. */
const char* Problems();
} // namespace presets
