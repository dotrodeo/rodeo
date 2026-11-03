// The `#S` state line's chain half: the satellites in ring order with every
// slot's value, as JSON, for tools/rodeo-console
// (planning/RODEO_CONSOLE_STATE_LINE.md). main.cpp writes the rest of the line.
//
// Encoders are reported as running detent totals kept here, from each slot's raw
// counter. SatChain::EncoderSteps() would hand the steps to the line instead of
// the panel, and the encoders would stop working. Pots move in the line once
// they have moved 3 in 1000, so one resting between two values doesn't flicker.
#pragma once
#include <stddef.h>
#include "sat_chain.h"

namespace state_line
{
/** From the main loop, after the chain port: follows the encoders and pots.
 *  The totals start again at 0 when the chain's layout changes. */
void Track(const rodeo::SatChain& chain);

/** Writes `"chain":{"state":..,"sats":..,"broken":..,"panel":..,"s":[..]` into
 *  buf, left open for the caller's scan counts and closing brace. Returns the
 *  length, or 0 if it didn't fit. */
size_t Chain(const rodeo::SatChain& chain, bool panel, char* buf, size_t n);
} // namespace state_line
