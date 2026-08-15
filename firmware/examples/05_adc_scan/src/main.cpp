// Rodeo bring-up 05 — full control-surface scan (both ADS114S08 chips).
//
// Brings up the Layer 2 rodeo::RodeoAdc scanner: inits U3+U4, pipelines a
// non-blocking round-robin across all populated channels, and prints the 24
// cached codes ~5x/sec over USB serial.
//
//   PASS: sweep a pot on any channel -> that column tracks 0..32767; grounded
//         inputs read ~0; a missing chip's 12 columns stay 0 and its
//         "not responding" line prints once at boot.
//
// Bench harness for the multi-channel + revisit-rate validation (needs the JST
// harness). See docs/RODEO_ADC_LIB_DESIGN.md.

#include "daisy_seed.h"
#include "board.h"
#include "rodeo_adc.h"

using namespace daisy;

DaisySeed       hw;
rodeo::RodeoAdc adc;

int main(void)
{
    hw.Init();
    hw.StartLog(false);

    rodeo::RodeoAdc::Config cfg; // all 24 channels, 1000 SPS low-latency
    adc.Init(cfg);
    if(!adc.ChipPresent(0))
        hw.PrintLine("U3 (chip A) presence check FAILED");
    if(!adc.ChipPresent(1))
        hw.PrintLine("U4 (chip B) presence check FAILED");

    uint32_t last_print = System::GetNow();
    for(;;)
    {
        adc.Update(); // pump the scan as fast as the main loop turns

        if(System::GetNow() - last_print >= 200) // ~5 Hz console refresh
        {
            last_print = System::GetNow();
            for(uint8_t ch = 0; ch < rodeo::RodeoAdc::kNumChannels; ++ch)
                hw.Print("%d ", static_cast<int>(adc.GetCode(ch)));
            hw.PrintLine("");
        }
    }
}
