// TinyUSB configuration for the USB step-1 test: MIDI + CDC serial on the Daisy
// Seed's micro-USB (the STM32H750's OTG_FS core, TinyUSB root-hub port 0).
// See planning/REV_B_PLAN.md, "USB: explore first".
#pragma once

#define CFG_TUSB_MCU OPT_MCU_STM32H7
#define CFG_TUSB_OS  OPT_OS_NONE

// Port 0 = OTG_FS (the Seed's micro-USB, PA11/PA12). Rev B's external pins are the
// OTG_HS core on its internal full-speed PHY: port 1.
#define BOARD_TUD_RHPORT  0
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
