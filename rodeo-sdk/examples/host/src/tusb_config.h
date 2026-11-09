// TinyUSB for the host example: USB-MIDI and nothing else, on the port the
// build chose (common/usb_port.h). The host protocol rides on the MIDI.
#pragma once

#include "usb_port.h"

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

#define CFG_TUD_CDC    0
#define CFG_TUD_MIDI   1
#define CFG_TUD_MSC    0
#define CFG_TUD_HID    0
#define CFG_TUD_VENDOR 0
#define CFG_TUD_AUDIO  0

// A hello is a couple of KB: a bigger TX buffer takes it in fewer main loops
#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 256
