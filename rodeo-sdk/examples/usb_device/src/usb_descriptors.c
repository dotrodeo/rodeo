// The USB device test's normal-mode configuration: stereo UAC1 audio + CDC
// serial + MIDI. Its own layout (RODEO_USB_CUSTOM), so only this is here; the
// device and string descriptors, and disk mode's configuration (CDC serial + a
// RAM disk), are the SDK's (common/usb_desc.c). The IDs are in tusb_config.h.
#include "tusb.h"
#include "usb_desc.h"
#include "usb_descriptors.h"

// Endpoints, every address distinct
#define EPNUM_AUDIO_OUT 0x01
#define EPNUM_AUDIO_IN  0x81
#define EPNUM_AUDIO_FB  0x82
#define EPNUM_CDC_NOTIF 0x83
#define EPNUM_CDC_OUT   0x04
#define EPNUM_CDC_IN    0x84
#define EPNUM_MIDI_OUT  0x05
#define EPNUM_MIDI_IN   0x85

#define CONFIG_TOTAL_LEN \
    (TUD_CONFIG_DESC_LEN + RODEO_UAC1_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    RODEO_UAC1_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL,
                          4, // the strings are usb_desc.h's: 4 audio, 5 serial, 6 MIDI
                          AUDIO_BYTES_PER_SAMPLE,
                          AUDIO_BITS_PER_SAMPLE,
                          EPNUM_AUDIO_OUT,
                          EPNUM_AUDIO_FB,
                          EPNUM_AUDIO_IN,
                          AUDIO_EP_SZ,
                          AUDIO_SAMPLE_RATE),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 6, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
};

TU_VERIFY_STATIC(sizeof(desc_configuration) == CONFIG_TOTAL_LEN, "configuration length");

uint8_t const* rodeo_usb_custom_configuration(void)
{
    return desc_configuration;
}
