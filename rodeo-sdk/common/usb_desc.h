#pragma once
//
// What a firmware tells the computer about itself over USB: its identity (the
// vendor and product IDs, its name) and which functions it has. Every Rodeo
// firmware's descriptors come from usb_desc.c; the firmware only says which
// layout, and who it is, in its tusb_config.h:
//
//   #include "usb_desc.h"
//   #define RODEO_USB_LAYOUT  RODEO_USB_CDC_MIDI
//   #define RODEO_USB_VID     0x1209       // pid.codes, for development
//   #define RODEO_USB_PID     0x0001       // see "Product IDs" below
//   #define RODEO_USB_MANUFACTURER "DOT RODEO"
//   #define RODEO_USB_PRODUCT "Rodeo"      // the name the computer shows
//
// and its Makefile includes common/tinyusb.mk, which builds usb_desc.c.
//
// Layouts. Every one has MIDI, so the host protocol (host.h) can reach every
// firmware, and tools/rodeo-console with it:
//
//   RODEO_USB_MIDI            MIDI only
//   RODEO_USB_CDC_MIDI        serial (CDC) and MIDI
//   RODEO_USB_AUDIO_CDC_MIDI  4x4 audio (usb_uac1.h), serial and MIDI
//   RODEO_USB_CUSTOM          the firmware's own configuration descriptor, from
//                             rodeo_usb_custom_configuration(); the device and
//                             string descriptors are still these. The strings
//                             are numbered as in AUDIO_CDC_MIDI.
//
// Disk mode: a firmware that hands its SD card to the computer defines
// RODEO_USB_PID_DISK and RODEO_USB_PRODUCT_DISK, and a `volatile bool
// usb_disk_mode` of its own. While it is set the device is serial and USB mass
// storage only (no MIDI, so no host protocol) under that PID; the firmware
// re-enumerates to switch (see examples/usb_storage).
//
// Optional: RODEO_USB_BCD_DEVICE (the device's version, default 0x0001), and
// the interface names RODEO_USB_NAME_AUDIO, _SERIAL, _MIDI and _DISK (defaults
// "Rodeo audio", "Rodeo serial", "Rodeo MIDI", "Rodeo SD card").
// tools/rodeo-console finds a Rodeo by "Rodeo" in its MIDI port's name.
//
// Product IDs. 0x1209 is pid.codes' vendor ID for open-source hardware, and
// these products are its test range: for development, not for a product
// released to the public. A host caches a device's layout by its IDs, so each
// distinct layout has its own PID and firmwares with the same layout share one:
//
//   0x0001  CDC + MIDI                usb_midi_cdc, usb_storage
//   0x0002  stereo audio + CDC + MIDI usb_device (custom)
//   0x0003  disk mode                 usb_device, usb_storage, rodeo-demo
//   0x0004  4x4 audio + CDC + MIDI    usb_audio
//   0x0005  4x4 audio + CDC + MIDI    rodeo-demo (its own, for its name)
//   0x0006  MIDI                      host
//
// Before you ship a product, give it IDs of its own: a pid.codes PID if it is
// open source (https://pid.codes/howto/), or the USB-IF's vendor ID otherwise.
// The IDs belong to the product, not to Rodeo.
//
// Only macros and declarations here: tusb_config.h includes it from C.

#define RODEO_USB_MIDI           1
#define RODEO_USB_CDC_MIDI       2
#define RODEO_USB_AUDIO_CDC_MIDI 3
#define RODEO_USB_CUSTOM         4

#ifndef __ASSEMBLER__
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** RODEO_USB_CUSTOM only: the firmware's configuration descriptor, outside
 *  disk mode. Interface strings: 4 audio, 5 serial, 6 MIDI, 7 disk. */
uint8_t const* rodeo_usb_custom_configuration(void);

#ifdef __cplusplus
}
#endif
#endif
