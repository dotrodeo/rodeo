// Rodeo motherboard — app template.
//
// Minimal skeleton: boots the Daisy Seed and blinks the onboard + Rodeo carrier
// LED. Copy this folder to start a new app; board.h has the full Rodeo pin map.

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
