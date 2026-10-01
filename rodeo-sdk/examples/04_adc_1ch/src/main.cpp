// Rodeo bring-up 04 — external ADC single-channel read (ADS114S08, U3).
//
// Reads ONE channel (AIN0 vs AINCOM) on U3 and streams it over USB serial.
// Now built on the Layer 1 `rodeo::Ads114s08` driver (common/ads114s08.*) — the
// SPI/command/register plumbing and the board register defaults live there; this
// example is just the driver's proof harness.
//
//   PASS: open a serial monitor after flashing (the Daisy's onboard USB). A
//         value on AIN0 (a pot wiper between AGND and +3V3) prints ~10x/sec and
//         tracks the input. Jumper AIN0->AGND => ~0; ->3V3 => full-scale.
//
// Reference is external 3.3V (REFP0/REFN0), confirmed w/ Bumble 2026-08-14. The
// external ADCs are the one subsystem still pending full bench verification; the
// SPI path (MOSI/MISO orientation) is bench-verified, absolute mV via pot sweep
// is the remaining check. DRDY# is unwired, so we poll RDATA on a timed cadence.

#include "daisy_seed.h"
#include "board.h"
#include "ads114s08.h"
#include "status_led.h"

using namespace daisy;

DaisySeed        hw;
SpiHandle        spi;
rodeo::Ads114s08 adc; // U3
rodeo::StatusLed status;

int main(void)
{
    hw.Init();
    hw.StartLog(false); // telemetry over onboard USB once a monitor connects
    status.Init();

    // One shared SPI1 handle (Layer 2 will share it across both chips); here we
    // bring up just U3 (chip-select seed::D25).
    spi.Init(rodeo::Ads114s08::DefaultSpiConfig());
    bool present = adc.Init(&spi, seed::D25);

    hw.PrintLine("=== Rodeo 04_adc_1ch (U3 AIN0) ===");
    hw.PrintLine("U3 presence: %s", present ? "OK" : "FAILED (dead / mis-wired?)");

    adc.SelectChannel(0); // AIN0 vs AINCOM

    for(;;)
    {
        // Unipolar single-ended: clamp the negative offset/noise tail to 0 so a
        // fully-off pot reads ~0, not full-scale (see design doc value pipeline).
        int32_t code = adc.ReadData();
        if(code < 0)
            code = 0;
        float volts = (static_cast<float>(code) / 32767.0f) * rodeo::Ads114s08::kVref;

        // Daisy's logger has no %f; split millivolts into whole.fraction.
        int mv = static_cast<int>(volts * 1000.0f);
        hw.PrintLine("U3 AIN0: code=%5d  volts=%d.%03d V",
                     static_cast<int>(code),
                     mv / 1000,
                     mv % 1000);

        status.Heartbeat(200); // visible "sampling" blink
        System::Delay(100);
    }
}
