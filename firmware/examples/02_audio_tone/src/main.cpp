// Rodeo bring-up 02 — audio tone.
//
// Proves the codec / SAI path and both output jacks.
//   PASS: a steady 440 Hz sine on OUT L and OUT R at moderate level.
//
// Flash: put the Daisy in DFU (hold BOOT, tap RESET), then `make program-dfu`.

#include "daisy_seed.h"
#include "daisysp.h"
#include "board.h"

using namespace daisy;
using namespace daisysp;

DaisySeed  hw;
Oscillator osc;

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    (void)in;
    for(size_t i = 0; i < size; i++)
    {
        float s   = osc.Process();
        out[0][i] = s;
        out[1][i] = s;
    }
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(board::kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    osc.Init(hw.AudioSampleRate());
    osc.SetWaveform(Oscillator::WAVE_SIN);
    osc.SetFreq(440.0f);
    osc.SetAmp(0.25f);

    hw.StartAudio(AudioCallback);
    for(;;) {}
}
