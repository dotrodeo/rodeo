// Rodeo app template.
//
// Boots the Daisy Seed and blinks its own LED: the smallest program that proves
// the toolchain, the board and the flash path. Copy this folder to start an app.
// board.h has the Rev B pin map; board_daisy.h turns its pins into libDaisy's.

#include "daisy_seed.h"
#include "board.h"

using namespace daisy;

DaisySeed hw;

int main(void)
{
    hw.Init();

    bool on = false;
    for(;;)
    {
        on = !on;
        hw.SetLed(on);
        System::Delay(250);
    }
}
