// All eight audio channels on the Rev B motherboard: both codecs in one
// callback, each output sending its own tone, each input measured.
//
// Outputs 1-4 send 250, 500, 1000 and 2000 Hz sines at half scale. Patch each
// output to the input with the same number on the jack board, and every half
// second each input prints its peak and frequency over the Seed's USB serial:
//
//   in1  peak  500/1000  250 Hz  ok
//   in2  peak  498/1000  500 Hz  ok
//   in3  peak  502/1000  250 Hz  from out1
//   in4  peak    0/1000    0 Hz  silent
//
// "ok" is the input hearing its own output, at close to the level sent (the
// jack board aims every channel at the same level, so loopback is near unity).
// "from outN" is another channel's tone: a crossed cable, or channels swapped
// in the firmware or the wiring. 1/2 are the Seed's codec, 3/4 the CS4272, so
// a pair that is silent or wrong together points at one codec.
//
// 'B' over the serial reboots into DFU for the next flash.
#include <math.h>

#include "daisy_seed.h"
#include "audio_4x4.h"
#include "rodeo_dsp.h"
#include "dfu.h"

using namespace daisy;

constexpr int   kChannels = 4;
constexpr float kToneHz[kChannels] = {250.f, 500.f, 1000.f, 2000.f};
constexpr float kLevel             = 0.5f;

DaisySeed            hw;
static float         phase[kChannels];
static rodeo::ToneMeter meter[kChannels];
static volatile float   peak[kChannels], hz[kChannels];
static volatile bool    report_ready = false;
static volatile bool    to_bootloader = false;

static void UsbRx(uint8_t* buf, uint32_t* len)
{
    for(uint32_t i = 0; i < *len; i++)
        if(buf[i] == 'B')
            to_bootloader = true;
}

static void Audio(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    const float sr = hw.AudioSampleRate();
    for(size_t i = 0; i < size; i++)
    {
        bool done = false;
        for(int c = 0; c < kChannels; c++)
        {
            out[c][i] = kLevel * sinf(rodeo::kTwoPi * phase[c]);
            phase[c] += kToneHz[c] / sr;
            if(phase[c] >= 1.f)
                phase[c] -= 1.f;
            done = meter[c].Process(in[c][i]);
        }
        if(done) // the meters run in step, so they finish together
        {
            for(int c = 0; c < kChannels; c++)
            {
                peak[c] = meter[c].Peak();
                hz[c]   = meter[c].Hz();
            }
            report_ready = true;
        }
    }
}

// The output whose tone this is, or -1.
static int WhichTone(float f)
{
    for(int c = 0; c < kChannels; c++)
        if(fabsf(f - kToneHz[c]) < kToneHz[c] * 0.03f)
            return c;
    return -1;
}

static void Report()
{
    for(int c = 0; c < kChannels; c++)
    {
        const float p    = peak[c];
        const float f    = hz[c];
        const int   from = WhichTone(f);
        const char* verdict;
        char        buf[16];
        if(p < 0.02f)
            verdict = "silent";
        else if(from == c && fabsf(p - kLevel) < kLevel * 0.2f)
            verdict = "ok";
        else if(from == c)
            verdict = "level off";
        else if(from >= 0)
        {
            snprintf(buf, sizeof(buf), "from out%d", from + 1);
            verdict = buf;
        }
        else
            verdict = "no tone";
        hw.PrintLine("in%d  peak %4d/1000  %4d Hz  %s", c + 1,
                     static_cast<int>(lroundf(p * 1000.f)),
                     static_cast<int>(lroundf(f)), verdict);
    }
    hw.PrintLine("");
}

int main(void)
{
    hw.Init();
    hw.StartLog(false);
    hw.usb_handle.SetReceiveCallback(UsbRx, UsbHandle::FS_INTERNAL);

    rodeo::InitAudio4x4(hw);
    for(int c = 0; c < kChannels; c++)
        meter[c].Init(hw.AudioSampleRate(), 0.5f);
    rodeo::StartAudio4x4(hw, Audio);

    hw.PrintLine("audio_4x4: out1-4 send 250/500/1000/2000 Hz; patch outN to inN");
    for(;;)
    {
        if(report_ready)
        {
            report_ready = false;
            Report();
        }
        if(to_bootloader)
        {
            rodeo::StopAudio4x4(hw);
            ResetToDfu();
        }
    }
}
