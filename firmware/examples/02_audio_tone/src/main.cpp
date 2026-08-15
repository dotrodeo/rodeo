// Rodeo bring-up 02 — audio tone.
//
// Proves the codec / SAI path and both output jacks.
//   PASS: a steady 440 Hz sine on OUT L and OUT R at moderate level.
//
// Flash: put the Daisy in DFU (hold BOOT, tap RESET), then `make program-dfu`.

#include "daisy_seed.h"
#include "daisysp.h"
#include "board.h"
#include "status_led.h"

using namespace daisy;
using namespace daisysp;

DaisySeed        hw;
Oscillator       osc;
rodeo::StatusLed status;

static constexpr float kToneHz = 440.0f;
static constexpr float kAmp    = 0.25f;

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
    osc.SetFreq(kToneHz);
    osc.SetAmp(kAmp);

    hw.StartLog(false);
    status.Init();

    hw.StartAudio(AudioCallback);

    hw.PrintLine("=== Rodeo 02_audio_tone ===");
    hw.PrintLine("%d Hz sine on OUT L/R, amp %d%% — audio running",
                 static_cast<int>(kToneHz),
                 static_cast<int>(kAmp * 100.0f));

    // Audio runs in the callback; the main loop just heartbeats the status LED
    // (proof the firmware is alive) and prints an uptime tick.
    uint32_t secs = 0;
    for(;;)
    {
        status.Heartbeat(500); // ~1 Hz blink
        if(System::GetNow() - secs * 1000 >= 1000)
        {
            secs++;
            hw.PrintLine("audio alive — uptime %lus", secs);
        }
    }
}
