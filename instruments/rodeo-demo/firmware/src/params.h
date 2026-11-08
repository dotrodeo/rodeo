// The parameter table: every synth::Params field the outside world can set, by a
// number (the id) and the preset files' own key, in whole-number units (presets.h).
// The MIDI protocol (remote.h) sends this table to the app and sets parameters
// through it; panel::Set() is the one way in. See
// planning/RODEO_DEMO_MIDI_PROTOCOL.md.
#pragma once
#include <stddef.h>
#include "synth.h"

namespace params
{
struct Info
{
    int         id;
    const char* key;  // the preset file's key
    int         min, max;
    const char* unit; // "" for none; JSON-escaped (‰ for per mille)
    const char* const* names; // for a choice: max - min + 1 names, or null
    int         pot;  // 1..4 for a pot's parameter, else 0
};

/** The table, in id order. */
int         Count();
const Info& At(int i);
/** By id, or null. */
const Info* Find(int id);

/** The highest value id can take now: the sound's depends on the card. */
int Max(const Info& info);

/** p's value for the parameter, in its units. */
int Get(const Info& info, const synth::Params& p);
/** Put v (clamped) into p. Nothing else: panel::Set() does the rest. */
void Put(const Info& info, int v, synth::Params& p);

/** "cutoff: 41 %", "shape: saw + sub", into buf. */
void Describe(const Info& info, const synth::Params& p, char* buf, size_t n);
} // namespace params
