// TinyUSB configuration for the SD-as-USB-drive test: serial + MIDI normally,
// serial + mass storage in disk mode, on the port the build chose (Rev B's USB-C
// by default). See common/usb_port.h and planning/REV_B_PLAN.md, "USB".
#pragma once

#include "usb_port.h"

#define CFG_TUSB_MCU OPT_MCU_STM32H7
#define CFG_TUSB_OS  OPT_OS_NONE

#define BOARD_TUD_RHPORT  RODEO_USB_RHPORT
#define CFG_TUD_ENABLED   1
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED

// DWC2 in slave mode: the CPU moves FIFO data, no DMA, so no cache maintenance.
// (The SD card's own DMA goes through a staging buffer in main.cpp.)
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_CDC    1
#define CFG_TUD_MIDI   1 // normal mode
#define CFG_TUD_MSC    1 // disk mode only
#define CFG_TUD_AUDIO  0
#define CFG_TUD_HID    0
#define CFG_TUD_VENDOR 0

// Eight sectors a callback: fewer, larger SD transfers
#define CFG_TUD_MSC_EP_BUFSIZE 4096

#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 512
#define CFG_TUD_CDC_EP_BUFSIZE 64

#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 64
