#pragma once
//
// The Host (host.h) over TinyUSB's MIDI class, cable 0. Target only. Call from
// the main loop, after reading the MIDI packets and offering each to
// host.Receive():
//
//   rodeo::HostUsbTask(host, System::GetNow(), cpu_percent);
//
// It tells the Host whether a computer is there, lets it stream, and sends
// what it has queued as fast as the computer takes it, never waiting.
//
#include "host.h"
#include "tusb.h"

namespace rodeo
{
inline void HostUsbTask(Host& host, uint32_t now_ms, int cpu_percent)
{
    host.Connected(tud_midi_mounted());
    host.Task(now_ms, cpu_percent);
    uint8_t packet[4];
    while(host.Peek(packet) && tud_midi_packet_write(packet))
        host.Pop();
}
} // namespace rodeo
