// Rodeo bench tool 10 — audio levels.
//
// Measures the Seed codec's full-scale levels on Rev A, to set the Rev B
// front-end gains (hardware/rodeo-motherboard/docs/rev-b.md, "Audio interface and jack board").
//
//   OUT L/R: a 1 kHz sine that steps every 5 s through 1.0, 0.5, 0.25 and
//            0.125 of full scale, then repeats. Measure Vpp with a scope.
//   IN  L/R: the peak |sample| of each input over the last 0.5 s, printed over
//            USB serial. 1.000 means the input is at (or past) full scale.
//   Also prints which Seed hardware libDaisy detected.
//
// Flash: put the Daisy in DFU (hold BOOT, tap RESET), then `make program-dfu`.
// Watch the serial output with e.g. `screen /dev/tty.usbmodem* 115200`.

#include <cmath>
#include "daisy_seed.h"
#include "daisysp.h"
#include "board.h"

using namespace daisy;
using namespace daisysp;

DaisySeed  hw;
Oscillator osc;

static constexpr float    kToneHz   = 1000.0f;
static constexpr uint32_t kStepMs   = 5000;
static constexpr uint32_t kReportMs = 500;

static volatile float peak_l = 0.0f;
static volatile float peak_r = 0.0f;

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    float pl = peak_l, pr = peak_r;
    for(size_t i = 0; i < size; i++)
    {
        float s   = osc.Process();
        out[0][i] = s;
        out[1][i] = s;
        pl        = fmaxf(pl, fabsf(in[0][i]));
        pr        = fmaxf(pr, fabsf(in[1][i]));
    }
    peak_l = pl;
    peak_r = pr;
}

static const char* BoardName(DaisySeed::BoardVersion v)
{
    switch(v)
    {
        case DaisySeed::BoardVersion::DAISY_SEED: return "DAISY_SEED (Rev4, AK4556)";
        case DaisySeed::BoardVersion::DAISY_SEED_1_1:
            return "DAISY_SEED_1_1 (Rev5, WM8731)";
        case DaisySeed::BoardVersion::DAISY_SEED_2_DFM:
            return "DAISY_SEED_2_DFM (PCM3060)";
    }
    return "unknown";
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(board::kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    osc.Init(hw.AudioSampleRate());
    osc.SetWaveform(Oscillator::WAVE_SIN);
    osc.SetFreq(kToneHz);
    osc.SetAmp(1.0f);

    hw.StartLog(true); // wait for the serial monitor so nothing is missed
    hw.PrintLine("=== Rodeo 10_audio_levels ===");
    hw.PrintLine("Seed detected as: %s", BoardName(hw.CheckBoardVersion()));
    hw.PrintLine("OUT: 1 kHz sine, amplitude steps 1.0 / 0.5 / 0.25 / 0.125 every 5 s");

    hw.StartAudio(AudioCallback);

    static constexpr float kAmps[] = {1.0f, 0.5f, 0.25f, 0.125f};
    size_t   step      = 0;
    float    amp       = kAmps[0];
    uint32_t last_step = System::GetNow();
    for(;;)
    {
        System::Delay(kReportMs);

        if(System::GetNow() - last_step >= kStepMs)
        {
            step      = (step + 1) % (sizeof(kAmps) / sizeof(kAmps[0]));
            amp       = kAmps[step];
            last_step = System::GetNow();
            osc.SetAmp(amp);
        }

        float pl = peak_l, pr = peak_r;
        peak_l = 0.0f;
        peak_r = 0.0f;
        hw.PrintLine("OUT amp " FLT_FMT3 " | IN peak L " FLT_FMT3
                     " R " FLT_FMT3 "%s",
                     FLT_VAR3(amp),
                     FLT_VAR3(pl),
                     FLT_VAR3(pr),
                     (pl >= 0.999f || pr >= 0.999f) ? "  CLIP" : "");
    }
}
