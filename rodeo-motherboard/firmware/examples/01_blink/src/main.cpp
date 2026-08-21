// Rodeo bring-up 01 — blink.
//
// Smoke test: proves the board boots and both LEDs are wired.
//   PASS: the Daisy onboard LED and the Rodeo carrier status LED (Seed pin 28)
//         blink together at ~2 Hz, and the USB serial log ticks once per toggle.
//
// Flash: put the Daisy in DFU (hold BOOT, tap RESET), then `make program-dfu`.

#include "daisy_seed.h"
#include "board.h"
#include "status_led.h"

using namespace daisy;

DaisySeed        hw;
rodeo::StatusLed status;

int main(void)
{
    hw.Init();
    hw.StartLog(false); // non-blocking: runs with or without a serial monitor
    status.Init();

    hw.PrintLine("=== Rodeo 01_blink ===");
    hw.PrintLine("onboard LED + carrier status LED (Seed pin 28) @ ~2 Hz");

    uint32_t ticks = 0;
    bool     on    = false;
    for(;;)
    {
        on = !on;
        hw.SetLed(on);   // Daisy onboard LED
        status.Set(on);  // Rodeo carrier status LED
        hw.PrintLine("tick %lu  LED %s", ticks++, on ? "ON" : "off");
        System::Delay(250);
    }
}
