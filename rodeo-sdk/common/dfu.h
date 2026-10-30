// dfu.h — reboot into whichever DFU bootloader can reflash this build.
#pragma once
#include "daisy_seed.h"

// Under the Daisy bootloader (APP_TYPE=BOOT_SRAM, the default) this waits on the
// USB-C for `make program-dfu`, however long it takes. A BOOT_NONE build has only
// the STM32's ROM, on the Seed's micro-USB: the Daisy request returns at once
// there, and the ROM takes over.
inline void ResetToDfu()
{
    daisy::System::ResetToBootloader(daisy::System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
    daisy::System::ResetToBootloader(daisy::System::BootloaderMode::STM);
}
