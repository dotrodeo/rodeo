// The Rev B motherboard's first test: the Seed boots on it, and the MAX17048
// fuel gauge reports the cell.
//
// Fit a power board (USB-C or barrel) and a cell, switch on, and the Seed's LED
// blinks once a second: the switch, the buck-boost and the 3.3 V rails got the
// Seed running. Every two seconds the Seed's USB serial prints:
//
//   cell 3.912 V   71.4 %   +18.3 %/hr  charging
//
// What firmware can't see, check by eye: the power board's green LED is the
// charger's power-good and the red one is charging. Both come from the charger
// and light only with power plugged in; neither reaches the Seed. So:
//
//   * plug power in: green on, red on unless the cell is full, and within a
//     minute or two the rate turns positive and the line says "charging";
//   * unplug it: both LEDs off, the Seed keeps running from the cell, and the
//     rate goes negative ("discharging");
//   * the power switch off turns everything off, plugged in or not.
//
// If the gauge doesn't answer, the line says so and the LED blinks fast: check
// the cell is connected (the gauge is powered from it) and the I2C pull-ups.
// The gauge's power-up flag is printed once at start: set means the cell was
// connected since the gauge last saw this firmware.
//
// 'B' over the serial reboots into DFU.
#include <math.h>

#include "daisy_seed.h"
#include "battery.h"
#include "board.h"
#include "version.h"
#include "dfu.h"

using namespace daisy;
namespace gauge = rodeo::max17048;

DaisySeed            hw;
rodeo::Battery       battery;
static volatile bool to_bootloader = false;

static void UsbRx(uint8_t* buf, uint32_t* len)
{
    for(uint32_t i = 0; i < *len; i++)
        if(buf[i] == 'B')
            to_bootloader = true;
}

static const char* SeedName(DaisySeed::BoardVersion v)
{
    switch(v)
    {
        case DaisySeed::BoardVersion::DAISY_SEED_1_1: return "Seed 1.1 (WM8731)";
        case DaisySeed::BoardVersion::DAISY_SEED_2_DFM: return "Seed 2 DFM (PCM3060)";
        default: return "Seed (AK4556)";
    }
}

static int Milli(float v) { return static_cast<int>(lroundf(v * 1000.0f)); }

int main(void)
{
    hw.Init();
    hw.StartLog(false);
    hw.usb_handle.SetReceiveCallback(UsbRx, UsbHandle::FS_INTERNAL);

    bool gauge_ok = battery.Init();
    hw.PrintLine("blink_power: Rodeo SDK %s on a %s", rodeo::kSdkVersionString,
                 SeedName(hw.CheckBoardVersion()));
    if(gauge_ok)
    {
        uint16_t status = 0;
        battery.Status(&status);
        hw.PrintLine("gauge: MAX17048 version 0x%04x, power-up flag %s", battery.Version(),
                     (status & gauge::kStatusResetIndicator) ? "set" : "clear");
    }
    else
        hw.PrintLine("gauge: no MAX17048 at 0x%02x (version read 0x%04x)",
                     board::i2c::kBatteryGaugeAddr, battery.Version());

    uint32_t last_print = 0, last_blink = 0;
    bool     led = false;
    for(;;)
    {
        if(to_bootloader)
            ResetToDfu();

        uint32_t now = System::GetNow();
        if(now - last_blink >= (gauge_ok ? 500u : 100u))
        {
            last_blink = now;
            led        = !led;
            hw.SetLed(led);
        }
        if(now - last_print < 2000)
            continue;
        last_print = now;

        if(!gauge_ok)
        {
            // Keep trying: the cell may go in after power-up.
            if((gauge_ok = battery.Init()))
                hw.PrintLine("gauge: found it, version 0x%04x", battery.Version());
            else
                hw.PrintLine("gauge: not answering");
            continue;
        }
        gauge::Reading r;
        if(!battery.Read(&r))
        {
            hw.PrintLine("gauge: read failed");
            continue;
        }
        const int rate = Milli(r.percent_per_hr) / 100; // tenths
        hw.PrintLine("cell %d.%03d V  %3d.%d %%  %c%d.%d %%/hr  %s", Milli(r.volts) / 1000,
                     Milli(r.volts) % 1000, static_cast<int>(r.percent),
                     static_cast<int>(r.percent * 10) % 10, rate < 0 ? '-' : '+',
                     abs(rate) / 10, abs(rate) % 10, gauge::TrendName(r.trend));
    }
}
