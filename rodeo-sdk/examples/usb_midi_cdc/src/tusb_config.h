// TinyUSB configuration for the MIDI + CDC serial test, on the port the build
// chose: the Seed's micro-USB (OTG_FS, port 0) or Rev B's USB-C (OTG_HS on its
// full-speed PHY, port 1). See common/usb_port.h and planning/REV_B_PLAN.md, "USB".
#pragma once

#include "usb_port.h"
#include "usb_desc.h"

// What the computer sees (common/usb_desc.h). pid.codes test IDs: for
// development only; a product needs IDs of its own.
#define RODEO_USB_LAYOUT       RODEO_USB_CDC_MIDI
#define RODEO_USB_VID          0x1209
#define RODEO_USB_PID          0x0001
#define RODEO_USB_MANUFACTURER "DOT RODEO"
#define RODEO_USB_PRODUCT      "Rodeo USB test"

#define CFG_TUSB_MCU OPT_MCU_STM32H7
#define CFG_TUSB_OS  OPT_OS_NONE

#define BOARD_TUD_RHPORT  RODEO_USB_RHPORT
#define CFG_TUD_ENABLED   1
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED

// The DWC2 core runs in slave mode (FIFO by CPU, no DMA), so buffers can live in
// any RAM and the H7's data cache doesn't touch them.
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_CDC  1
#define CFG_TUD_MIDI 1
#define CFG_TUD_MSC  0
#define CFG_TUD_HID  0
#define CFG_TUD_VENDOR 0
#define CFG_TUD_AUDIO 0

#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 256
#define CFG_TUD_CDC_EP_BUFSIZE 64

#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 64
