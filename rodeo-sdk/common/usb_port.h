#pragma once
//
// The USB port TinyUSB drives, chosen at build time (common/tinyusb.mk):
//
//   RODEO_USB=hs  Rev B's port: the STM32H750's OTG_HS core on its internal
//                 full-speed PHY, Seed pins 36/37 (board::pin::kUsbDm/kUsbDp),
//                 to the power board's USB-C. TinyUSB port 1.
//   RODEO_USB=fs  the Seed's own micro-USB: the OTG_FS core. TinyUSB port 0.
//
// Both run at full speed (12 Mbit/s), the only speed either has here. In an
// app's tusb_config.h:
//
//   #include "usb_port.h"
//   #define BOARD_TUD_RHPORT RODEO_USB_RHPORT
//
// and in main(), after hw.Init() (or System::Init()):
//
//   rodeo::UsbPortInit();
//   tusb_rhport_init_t dev_init = {TUSB_ROLE_DEVICE, TUSB_SPEED_AUTO};
//   tusb_init(BOARD_TUD_RHPORT, &dev_init);
//
// libDaisy defines both cores' interrupt handlers (OTG_HS's in system.cpp,
// which every app links, OTG_FS's with DaisySeed's USB), so TinyUSB can't
// define its own. UsbPortInit() copies the vector table to RAM and points the
// port's entry at TinyUSB instead, and sets its priority below the audio DMA's
// so audio always preempts USB.
//
// The STM32's ROM DFU bootloader (System::ResetToBootloader(STM)) only answers
// on OTG_FS: on an hs build, reflash through the Seed's micro-USB.
//
#ifdef RODEO_USB_HS
#define RODEO_USB_RHPORT 1
#define RODEO_USB_PORT_NAME "OTG_HS (Rev B USB-C)"
#else
#define RODEO_USB_RHPORT 0
#define RODEO_USB_PORT_NAME "OTG_FS (Seed micro-USB)"
#endif

#ifdef __cplusplus
// C++ linkage even when tusb_config.h pulls this in from inside TinyUSB's
// extern "C" block.
extern "C++"
{
    namespace rodeo
    {
    /** Pins, clocks, VBUS sensing off, and the interrupt routed to TinyUSB, for
     *  the port this build chose. Call once, before tusb_init(). */
    void UsbPortInit();
    } // namespace rodeo
}
#endif
