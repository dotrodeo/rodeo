// The 4x4 audio function of RODEO_USB_AUDIO_CDC_MIDI (usb_desc.h): that
// layout's interface numbers, and the UAC1 descriptor usb_desc.c builds it from.
// Include it where the firmware handles audio requests (alt settings, volume),
// which ask by interface number. The rates and sizes (AUDIO_SAMPLE_RATE,
// AUDIO_N_CHANNELS = 4, AUDIO_BITS_PER_SAMPLE, AUDIO_EP_SZ) are the firmware's,
// in its tusb_config.h, with the audio class settings they size; see
// examples/usb_audio.
#pragma once

#include <stdbool.h>

enum
{
    ITF_NUM_AUDIO_CONTROL = 0,
    ITF_NUM_AUDIO_SPK, // computer -> Rodeo
    ITF_NUM_AUDIO_MIC, // Rodeo -> computer
    ITF_NUM_CDC,
    ITF_NUM_CDC_DATA,
    ITF_NUM_MIDI,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL
};

// UAC1 entity IDs
#define UAC1_SPK_INPUT_TERMINAL  0x01
#define UAC1_SPK_FEATURE_UNIT    0x02
#define UAC1_SPK_OUTPUT_TERMINAL 0x03
#define UAC1_MIC_INPUT_TERMINAL  0x11
#define UAC1_MIC_OUTPUT_TERMINAL 0x13

// The Audio Control interface's class-specific part: the units and terminals
#define UAC1_AC_ENTITIES_LEN                                                   \
    (TUD_AUDIO10_DESC_INPUT_TERM_LEN + TUD_AUDIO10_DESC_FEATURE_UNIT_LEN(4)    \
     + TUD_AUDIO10_DESC_OUTPUT_TERM_LEN + TUD_AUDIO10_DESC_INPUT_TERM_LEN      \
     + TUD_AUDIO10_DESC_OUTPUT_TERM_LEN)

#define TUD_IAD_LEN 8

// Four channels out with an asynchronous feedback endpoint, four in, one sample
// rate: usb_device's stereo descriptor with four channels each way. The channels
// are "non-predefined" (no speaker positions), so a computer shows them as 1-4
// rather than guessing a surround layout. The feature unit has master mute and
// volume only.
#define RODEO_UAC1_DESC_LEN                                                    \
    (TUD_IAD_LEN + TUD_AUDIO10_DESC_STD_AC_LEN + TUD_AUDIO10_DESC_CS_AC_LEN(2) \
     + UAC1_AC_ENTITIES_LEN /* speaker AS: alt 0, alt 1 */                     \
     + TUD_AUDIO10_DESC_STD_AS_LEN + TUD_AUDIO10_DESC_STD_AS_LEN               \
     + TUD_AUDIO10_DESC_CS_AS_INT_LEN + TUD_AUDIO10_DESC_TYPE_I_FORMAT_LEN(1)  \
     + TUD_AUDIO10_DESC_STD_AS_ISO_EP_LEN + TUD_AUDIO10_DESC_CS_AS_ISO_EP_LEN  \
     + TUD_AUDIO10_DESC_STD_AS_ISO_SYNC_EP_LEN /* microphone AS */             \
     + TUD_AUDIO10_DESC_STD_AS_LEN + TUD_AUDIO10_DESC_STD_AS_LEN               \
     + TUD_AUDIO10_DESC_CS_AS_INT_LEN + TUD_AUDIO10_DESC_TYPE_I_FORMAT_LEN(1)  \
     + TUD_AUDIO10_DESC_STD_AS_ISO_EP_LEN + TUD_AUDIO10_DESC_CS_AS_ISO_EP_LEN)

#define RODEO_UAC1_DESCRIPTOR(_itf, _stridx, _nbytes, _nbits, _epout, _epfb, _epin, _epsize, _rate) \
    /* Interface Association: groups the three audio interfaces as one function */                \
    TUD_IAD_LEN, TUSB_DESC_INTERFACE_ASSOCIATION, (_itf), 3, TUSB_CLASS_AUDIO,                       \
        AUDIO_SUBCLASS_CONTROL, 0x00, (_stridx),                                                   \
    TUD_AUDIO10_DESC_STD_AC(/*_itfnum*/ (_itf), /*_nEPs*/ 0x00, /*_stridx*/ (_stridx)),           \
    TUD_AUDIO10_DESC_CS_AC(/*_bcdADC*/ 0x0100, /*_totallen*/ UAC1_AC_ENTITIES_LEN,               \
                           /*_itf*/ (uint8_t)((_itf) + 1), (uint8_t)((_itf) + 2)),               \
    /* computer -> Rodeo: USB streaming in, feature unit (mute, volume), line out */               \
    TUD_AUDIO10_DESC_INPUT_TERM(UAC1_SPK_INPUT_TERMINAL, AUDIO_TERM_TYPE_USB_STREAMING,          \
                                UAC1_MIC_OUTPUT_TERMINAL, AUDIO_N_CHANNELS,                       \
                                AUDIO10_CHANNEL_CONFIG_NON_PREDEFINED,                            \
                                0x00, 0x00),                                                      \
    TUD_AUDIO10_DESC_FEATURE_UNIT(UAC1_SPK_FEATURE_UNIT, UAC1_SPK_INPUT_TERMINAL, 0x00,          \
                                  (AUDIO10_FU_CONTROL_BM_MUTE | AUDIO10_FU_CONTROL_BM_VOLUME),    \
                                  0x00, 0x00, 0x00, 0x00),                                                    \
    TUD_AUDIO10_DESC_OUTPUT_TERM(UAC1_SPK_OUTPUT_TERMINAL, AUDIO_TERM_TYPE_OUT_GENERIC_SPEAKER,  \
                                 0x00, UAC1_SPK_FEATURE_UNIT, 0x00),                              \
    /* Rodeo -> computer: line in, USB streaming out */                                            \
    TUD_AUDIO10_DESC_INPUT_TERM(UAC1_MIC_INPUT_TERMINAL, AUDIO_TERM_TYPE_IN_GENERIC_MIC, 0x00,   \
                                AUDIO_N_CHANNELS,                                                 \
                                AUDIO10_CHANNEL_CONFIG_NON_PREDEFINED,                            \
                                0x00, 0x00),                                                      \
    TUD_AUDIO10_DESC_OUTPUT_TERM(UAC1_MIC_OUTPUT_TERMINAL, AUDIO_TERM_TYPE_USB_STREAMING,        \
                                 UAC1_SPK_INPUT_TERMINAL, UAC1_MIC_INPUT_TERMINAL, 0x00),         \
    /* Speaker streaming interface: alt 0 idle, alt 1 streaming (data OUT + feedback IN) */       \
    TUD_AUDIO10_DESC_STD_AS_INT((uint8_t)((_itf) + 1), 0x00, 0x00, 0x00),                         \
    TUD_AUDIO10_DESC_STD_AS_INT((uint8_t)((_itf) + 1), 0x01, 0x02, 0x00),                         \
    TUD_AUDIO10_DESC_CS_AS_INT(UAC1_SPK_INPUT_TERMINAL, 0x01, AUDIO10_DATA_FORMAT_TYPE_I_PCM),   \
    TUD_AUDIO10_DESC_TYPE_I_FORMAT(AUDIO_N_CHANNELS, (_nbytes), (_nbits), (_rate)),                           \
    TUD_AUDIO10_DESC_STD_AS_ISO_EP((_epout),                                                      \
                                   (uint8_t)((uint8_t)TUSB_XFER_ISOCHRONOUS                       \
                                             | (uint8_t)TUSB_ISO_EP_ATT_ASYNCHRONOUS),            \
                                   (_epsize), 0x01, (_epfb)),                                     \
    TUD_AUDIO10_DESC_CS_AS_ISO_EP(AUDIO10_CS_AS_ISO_DATA_EP_ATT_SAMPLING_FRQ,                     \
                                  AUDIO10_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0x0000),   \
    /* bRefresh 0: feedback every 1 ms on every host, which the DWC2 core needs (see */           \
    /* TinyUSB's uac2_speaker_fb, "UAC1 Isochronous Synch Endpoint bRefresh Workaround") */      \
    TUD_AUDIO10_DESC_STD_AS_ISO_SYNC_EP((_epfb), 0),                                              \
    /* Microphone streaming interface: alt 0 idle, alt 1 streaming (data IN) */                   \
    TUD_AUDIO10_DESC_STD_AS_INT((uint8_t)((_itf) + 2), 0x00, 0x00, 0x00),                         \
    TUD_AUDIO10_DESC_STD_AS_INT((uint8_t)((_itf) + 2), 0x01, 0x01, 0x00),                         \
    TUD_AUDIO10_DESC_CS_AS_INT(UAC1_MIC_OUTPUT_TERMINAL, 0x01, AUDIO10_DATA_FORMAT_TYPE_I_PCM),  \
    TUD_AUDIO10_DESC_TYPE_I_FORMAT(AUDIO_N_CHANNELS, (_nbytes), (_nbits), (_rate)),                           \
    TUD_AUDIO10_DESC_STD_AS_ISO_EP((_epin),                                                       \
                                   (uint8_t)((uint8_t)TUSB_XFER_ISOCHRONOUS                       \
                                             | (uint8_t)TUSB_ISO_EP_ATT_ASYNCHRONOUS),            \
                                   (_epsize), 0x01, 0x00),                                        \
    TUD_AUDIO10_DESC_CS_AS_ISO_EP(AUDIO10_CS_AS_ISO_DATA_EP_ATT_SAMPLING_FRQ,                     \
                                  AUDIO10_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0x0000)
