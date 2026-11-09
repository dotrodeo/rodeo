// rodeo-demo over MIDI, for the console, the phone app and any controller:
// planning/RODEO_DEMO_MIDI_PROTOCOL.md.
//
//   CCs     the panel's parameters, from USB and the TRS jack (Cc())
//   SysEx   the SDK's host protocol (rodeo-sdk/common/host.h), from USB only:
//           hello and the parameter table, the state, the chain, sets, dfu;
//           and rodeo-demo's own commands: the preset and sound lists, and
//           loading a preset
#pragma once
#include <stdint.h>
#include "sat_chain.h"
#include "synth.h"

namespace remote
{
/** say: where the descriptions of what changed go (the serial port, and back to
 *  Said()). p: the parameters MIDI sets. */
void Init(void (*say)(const char*), synth::Params& p, const rodeo::SatChain& chain);

/** A USB-MIDI packet. True if it was SysEx, which is the protocol's; false for
 *  anything else, which the caller handles. */
bool Packet(const uint8_t packet[4]);

/** A CC the protocol maps (not 64, 120, 123: those are the caller's). Returns
 *  false if the number isn't one of ours. */
bool Cc(int cc, int value);

/** From the main loop: sends what's queued and, while the computer asks, the
 *  state and the chain. */
void Task(int cpu_percent);

/** A description of what changed, to the computer while it's streaming. */
void Said(const char* s);

/** True once the computer has asked for the bootloader and been answered. */
bool DfuRequested();
} // namespace remote
