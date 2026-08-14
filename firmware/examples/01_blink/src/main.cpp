// Rodeo bring-up 01 — blink.
//
// Smoke test: proves the board boots and both LEDs are wired.
//   PASS: the Daisy onboard LED and the Rodeo carrier LED (D5) blink together
//         at ~2 Hz.
//
// Flash: put the Daisy in DFU (hold BOOT, tap RESET), then `make program-dfu`.

#include "daisy_seed.h"
#include "board.h"

using namespace daisy;

DaisySeed hw;

int main(void)
{
    hw.Init();

    GPIO status_led;
    status_led.Init(seed::D21, GPIO::Mode::OUTPUT); // Rodeo carrier LED (D5)

    bool on = false;
    for(;;)
    {
        on = !on;
        hw.SetLed(on);        // Daisy onboard LED
        status_led.Write(on); // Rodeo carrier LED
        System::Delay(250);
    }
}
