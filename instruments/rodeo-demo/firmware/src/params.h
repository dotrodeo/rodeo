// The parameter table: every synth::Params field the outside world can set, by a
// number (the id) and the preset files' own key, in whole-number units (presets.h).
// The SDK's host protocol (rodeo-sdk/common/host.h, through remote.h) sends this
// table to the computer and sets parameters through it; panel::Set() is the one
// way in. See planning/RODEO_DEMO_MIDI_PROTOCOL.md.
#pragma once
#include <stddef.h>
#include "host.h"
#include "synth.h"

namespace params
{
// A parameter is a rodeo::HostParam (host.h): id, key, min, max, default, unit,
// names, pot. The pot is 1..8 (panel.h).
using Info = rodeo::HostParam;

/** Fills in each parameter's default from a default synth::Params, so the
 *  table can't disagree with synth.h. Call once, before anything else here. */
void Init();

/** The table, in id order. */
int         Count();
const Info& At(int i);
const Info* Table();
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
