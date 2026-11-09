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
// Before rebooting for the bootloader, HostUsbFinish() lets the dfu reply
// leave: once it's in TinyUSB's buffer the Host counts it as sent, and TinyUSB
// can't say when its buffer is empty.
//
#include "host.h"
#include "sys/system.h"
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

/** Runs USB for a while longer, so what TinyUSB holds reaches the computer. */
inline void HostUsbFinish(uint32_t ms = 50)
{
    uint32_t start = daisy::System::GetNow();
    while(daisy::System::GetNow() - start < ms)
        tud_task();
}
} // namespace rodeo
