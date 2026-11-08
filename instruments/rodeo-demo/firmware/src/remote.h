// rodeo-demo over MIDI, for the phone app and any controller:
// planning/RODEO_DEMO_MIDI_PROTOCOL.md.
//
//   CCs         the panel's parameters, from USB and the TRS jack (Cc())
//   SysEx       F0 7D 52 44 01 <cmd> ... F7, from USB only: hello and the parameter
//               table, the state (once, or streamed while the app keeps asking),
//               14-bit parameter sets, the preset and sound lists
//
// Everything Rodeo sends goes through a queue of whole USB-MIDI packets, drained
// as the host takes them. A message goes in whole or not at all, so the receiver
// never sees half of one, and the main loop never waits on a host that isn't
// reading.
#pragma once
#include <stdint.h>
#include "synth.h"

namespace remote
{
/** say: where the descriptions of what changed go (the serial port, and back to
 *  Said()). */
void Init(void (*say)(const char*));

/** A USB-MIDI packet that may be part of a SysEx message (CIN 0x4 to 0x7). Returns
 *  false for anything else, which the caller handles. */
bool Packet(const uint8_t packet[4], synth::Params& p);

/** A CC the protocol maps (not 64, 120, 123: those are the caller's). Returns
 *  false if the number isn't one of ours. */
bool Cc(int cc, int value, synth::Params& p);

/** From the main loop: sends the queue and, while the app asks for it, the state. */
void Task(const synth::Params& p, int cpu_percent);

/** A description of what changed, to the app while it's streaming. */
void Said(const char* s);
} // namespace remote
