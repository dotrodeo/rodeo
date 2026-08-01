// Rodeo 001 — M2: 12-voice polyphony.
//
// MIDI IN (USART1) -> note-event queue -> 12-voice VoiceManager (FIFO alloc +
// FIFO stealing). Each voice is Osc1+Osc2 -> per-voice filter -> amp, with its
// own amp + filter envelopes, so filter sweeps track per note. Parameters are
// fixed placeholder defaults until the external-ADC control surface (M4).
//
//   control plane (main loop): MidiEngine parses UART; prints CPU/voice telemetry
//   audio plane   (callback):  Synth drains the queue and renders all voices
//
// CPU load + active-voice count are logged over the Daisy's onboard USB serial
// (open a serial monitor after flashing) to verify the "no glitches" budget.

#include "daisy_seed.h"
#include "platform/board.h"
#include "platform/system.h"
#include "engine/synth.h"
#include "midi/midi_engine.h"

using namespace daisy;

namespace
{
DaisySeed    hw;
Synth        synth;
MidiEngine   midi;
CpuLoadMeter cpu_meter;
GPIO         status_led; // carrier LED D5 (Daisy pin 28 / seed::D21)

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    (void)in;
    cpu_meter.OnBlockStart();
    synth.Process(out, size);
    cpu_meter.OnBlockEnd();
}
} // namespace

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(board::kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    platform::EnableFlushToZero();
    status_led.Init(seed::D21, GPIO::Mode::OUTPUT);

    const float sr = hw.AudioSampleRate();
    synth.Init(sr);
    midi.Init();
    cpu_meter.Init(sr, board::kBlockSize);

    hw.StartAudio(AudioCallback);
    hw.StartLog(false); // non-blocking; telemetry appears once a monitor connects

    uint32_t last_toggle = System::GetNow();
    bool     led_state   = false;
    for(;;)
    {
        midi.Process(synth); // parse MIDI -> note-event queue (never blocks audio)

        const uint32_t now = System::GetNow();
        if(now - last_toggle >= 500u)
        {
            last_toggle = now;
            led_state   = !led_state;
            hw.SetLed(led_state);
            status_led.Write(led_state);

            hw.PrintLine("voices=%d  cpu avg=%d%% max=%d%%",
                         synth.ActiveVoices(),
                         static_cast<int>(cpu_meter.GetAvgCpuLoad() * 100.f),
                         static_cast<int>(cpu_meter.GetMaxCpuLoad() * 100.f));
        }
    }
}
