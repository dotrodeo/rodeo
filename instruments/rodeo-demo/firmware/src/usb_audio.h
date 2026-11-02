// USB audio for rodeo-demo: Rodeo as a 4-in, 4-out interface on the computer.
//
// The TinyUSB side is the usb_audio example's (rodeo-sdk/examples/usb_audio),
// proven on Rev B: UAC1 at 48 kHz, asynchronous, the codec clock leading and a
// feedback endpoint keeping the computer in step. Here it is wrapped in two calls
// the audio callback makes once a block, so main.cpp only sees floats.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace usb_audio
{
constexpr int kChannels = 4;

/** What the computer is playing to channels 1-4, for this block. Silence while
 *  it isn't playing, or while the buffer refills after running dry. */
void Receive(float* const* from_computer, size_t frames);

/** What the computer records on channels 1-4, for this block. Dropped while it
 *  isn't recording. */
void Send(const float* const* to_computer, size_t frames);

/** Forget the streams, as the device leaves the bus (disk mode's re-enumeration
 *  closes them without telling the audio function). */
void Reset();

struct Stats
{
    bool     playing, recording; // the computer is streaming to / from Rodeo
    uint32_t underruns;          // to Rodeo: the buffer ran dry
    uint32_t overruns;           // from Rodeo: the buffer was full, frames dropped
};
Stats GetStats();
} // namespace usb_audio
