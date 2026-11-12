// TinyUSB configuration for the USB audio test: UAC1 audio (stereo out + stereo in,
// 48 kHz, 24-bit, asynchronous with feedback) + MIDI + CDC serial, on the Daisy Seed's
// micro-USB (OTG_FS, TinyUSB port 0), or Rev B's USB-C with RODEO_USB=hs (port 1).
// See common/usb_port.h and planning/REV_B_PLAN.md, "USB".
#pragma once

#include "usb_port.h"
#include "usb_desc.h"

// What the computer sees (common/usb_desc.h). pid.codes test IDs: for
// development only; a product needs IDs of its own.
#define RODEO_USB_LAYOUT       RODEO_USB_CUSTOM
#define RODEO_USB_VID          0x1209
#define RODEO_USB_PID          0x0002
#define RODEO_USB_MANUFACTURER "DOT RODEO"
#define RODEO_USB_PRODUCT      "Rodeo" // what the computer's sound settings show
#define RODEO_USB_PID_DISK     0x0003 // disk mode: serial + the SD card
#define RODEO_USB_PRODUCT_DISK "Rodeo (disk mode)"
#define RODEO_USB_NAME_DISK    "Rodeo disk"

#define CFG_TUSB_MCU OPT_MCU_STM32H7
#define CFG_TUSB_OS  OPT_OS_NONE

#define BOARD_TUD_RHPORT  RODEO_USB_RHPORT
#define CFG_TUD_ENABLED   1
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED

// DWC2 in slave mode: the CPU moves FIFO data, no DMA, so no cache maintenance.
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_AUDIO  1
#define CFG_TUD_CDC    1
#define CFG_TUD_MIDI   1
#define CFG_TUD_MSC    1 // disk mode only
#define CFG_TUD_HID    0
#define CFG_TUD_VENDOR 0

//------------- Audio -------------//
// UAC1, not UAC2: at full speed UAC1's feedback is 10.14, which Windows, macOS and
// Linux all accept. TinyUSB sends UAC2 feedback as 16.16, which macOS rejects at
// full speed (and Rev B's port is full speed only).
#define AUDIO_SAMPLE_RATE      48000
#define AUDIO_N_CHANNELS       2
#define AUDIO_BYTES_PER_SAMPLE 3 // 24-bit, packed
#define AUDIO_BITS_PER_SAMPLE  24
#define AUDIO_EP_SZ \
    TUD_AUDIO_EP_SIZE(false, AUDIO_SAMPLE_RATE, AUDIO_BYTES_PER_SAMPLE, AUDIO_N_CHANNELS)

#define CFG_TUD_AUDIO_CTRL_BUF_SZ 64

#define CFG_TUD_AUDIO_ENABLE_EP_OUT       1 // computer -> Seed ("speaker")
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP  1 // asynchronous: the codec clock leads
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX AUDIO_EP_SZ
// FIFO-count feedback needs at least 4 packets of buffer
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ (4 * AUDIO_EP_SZ)

#define CFG_TUD_AUDIO_ENABLE_EP_IN        1 // Seed -> computer ("microphone")
#define CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL  1 // packet size follows the FIFO level
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX AUDIO_EP_SZ
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ (4 * AUDIO_EP_SZ)

//------------- MSC (disk mode) -------------//
// Larger than the 64-byte bulk packet so a 10-sector transfer moves in fewer callbacks
#define CFG_TUD_MSC_EP_BUFSIZE 4096

//------------- CDC, MIDI -------------//
#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 512
#define CFG_TUD_CDC_EP_BUFSIZE 64

#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 64
