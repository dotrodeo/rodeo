// Rodeo 001 — M0 bring-up: boots and produces audio.
//
// Scope (per the firmware plan, milestone M0):
//   * init the Daisy Seed, 48 kHz, block size 32
//   * enable FPU flush-to-zero
//   * emit a fixed sine tone to both output channels (J5)
//   * blink the onboard + carrier status LEDs to prove the main loop is alive
//
// No MIDI, no ADC/SPI, no control surface yet — those arrive in M1+.

#include "daisy_seed.h"
#include "daisysp.h"
#include "platform/board.h"
#include "platform/system.h"

using namespace daisy;
using namespace daisysp;

namespace
{
DaisySeed  hw;
Oscillator osc;
GPIO       status_led; // carrier LED D5 (Daisy pin 28 / seed::D21)

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    (void)in; // no input processing in M0
    for(size_t i = 0; i < size; ++i)
    {
        const float sig = osc.Process();
        out[0][i]       = sig; // left  -> J5 tip
        out[1][i]       = sig; // right -> J5 ring
    }
}
} // namespace

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(board::kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    platform::EnableFlushToZero();

    status_led.Init(seed::D21, GPIO::Mode::OUTPUT);

    const float sample_rate = hw.AudioSampleRate();
    osc.Init(sample_rate);
    osc.SetWaveform(Oscillator::WAVE_SIN);
    osc.SetFreq(220.0f);
    osc.SetAmp(0.2f); // conservative level; leave headroom

    hw.StartAudio(AudioCallback);

    uint32_t last_toggle = System::GetNow();
    bool     led_state   = false;
    for(;;)
    {
        const uint32_t now = System::GetNow();
        if(now - last_toggle >= 500u)
        {
            last_toggle = now;
            led_state   = !led_state;
            hw.SetLed(led_state);        // Daisy onboard user LED
            status_led.Write(led_state); // carrier status LED (D5)
        }
    }
}
