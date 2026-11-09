// TinyUSB configuration for rodeo-demo, the usb_audio example's: UAC1 audio (4 out + 4 in,
// 48 kHz, 16- or 24-bit, asynchronous with feedback) + MIDI + CDC serial, on the port the
// build chose (Rev B's USB-C by default). See common/usb_port.h and
// planning/REV_B_PLAN.md, "USB".
#pragma once

#include "usb_port.h"

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
// Eight sectors a callback: fewer, larger SD transfers
#define CFG_TUD_MSC_EP_BUFSIZE 4096
#define CFG_TUD_HID    0
#define CFG_TUD_VENDOR 0

//------------- Audio -------------//
// UAC1, as in usb_device: its full-speed feedback (10.14) is the one every OS takes.
// Four channels each way, (48 + 1) frames a packet. 16-bit by default: 392-byte
// packets, which fit behind a USB 2.0 hub (a Mac mini's front ports are one).
// `make AUDIO_BITS=24` makes them 588 bytes, which a direct port takes but macOS
// gives up on behind a hub: a hub's full-speed budget is tighter than the plan's
// sum ("Bandwidth fits" in planning/REV_B_PLAN.md, measured 2026-10-31).
#ifndef AUDIO_BITS
#define AUDIO_BITS 16
#endif
#define AUDIO_SAMPLE_RATE      48000
#define AUDIO_N_CHANNELS       4
#define AUDIO_BITS_PER_SAMPLE  AUDIO_BITS
#define AUDIO_BYTES_PER_SAMPLE (AUDIO_BITS / 8) // packed
#define AUDIO_EP_SZ \
    TUD_AUDIO_EP_SIZE(false, AUDIO_SAMPLE_RATE, AUDIO_BYTES_PER_SAMPLE, AUDIO_N_CHANNELS)

#define CFG_TUD_AUDIO_CTRL_BUF_SZ 64

#define CFG_TUD_AUDIO_ENABLE_EP_OUT       1 // computer -> Rodeo
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP  1 // asynchronous: the codec clock leads
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX AUDIO_EP_SZ
// FIFO-count feedback needs at least 4 packets of buffer
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ (4 * AUDIO_EP_SZ)

#define CFG_TUD_AUDIO_ENABLE_EP_IN        1 // Rodeo -> computer
#define CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL  1 // packet size follows the FIFO level
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX AUDIO_EP_SZ
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ (4 * AUDIO_EP_SZ)

//------------- CDC, MIDI -------------//
#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 512
#define CFG_TUD_CDC_EP_BUFSIZE 64

#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 256 // the Host (remote.cpp) queues whole SysEx messages ahead of this
