// Rodeo 001 — M1: one MIDI voice.
//
// MIDI IN (USART1) -> note-event queue -> a single Osc1+Osc2 -> filter -> amp
// voice with per-voice amp + filter envelopes, played monophonically
// (last-note priority). Parameters are fixed placeholder defaults until the
// external-ADC control surface arrives in M4.
//
//   control plane (main loop): MidiEngine parses UART, pushes note events
//   audio plane   (callback):  Synth drains the queue and renders the voice

#include "daisy_seed.h"
#include "platform/board.h"
#include "platform/system.h"
#include "engine/synth.h"
#include "midi/midi_engine.h"

using namespace daisy;

namespace
{
DaisySeed  hw;
Synth      synth;
MidiEngine midi;
GPIO       status_led; // carrier LED D5 (Daisy pin 28 / seed::D21)

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    (void)in;
    synth.Process(out, size);
}
} // namespace

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(board::kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    platform::EnableFlushToZero();
    status_led.Init(seed::D21, GPIO::Mode::OUTPUT);

    synth.Init(hw.AudioSampleRate());
    midi.Init();

    hw.StartAudio(AudioCallback);

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
        }
    }
}
