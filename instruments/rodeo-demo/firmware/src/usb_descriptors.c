// USB descriptors for rodeo-demo: UAC1 audio 4x4 + CDC serial + MIDI, as the
// usb_audio example (rodeo-sdk/examples/usb_audio), under its own PID and name;
// in disk mode, CDC serial + mass storage, as the usb_storage example's disk mode.
// See main.cpp, usb_audio.cpp and disk.h.
#include "tusb.h"
#include "usb_descriptors.h"
#include "stm32h7xx.h"

// pid.codes test VID/PID, for development only. A product needs its own PIDs.
// Its own PID, because a host caches a device's layout by PID: 0x0001 the MIDI +
// CDC test, 0x0002 usb_device's stereo audio, 0x0003 its disk mode, 0x0004 the
// usb_audio example, 0x0005 this. Disk mode's interfaces and endpoints are
// exactly usb_device's and usb_storage's disk mode, so it shares their 0x0003.
#define USB_VID      0x1209
#define USB_PID      0x0005
#define USB_PID_DISK 0x0003

extern volatile bool usb_disk_mode; // disk.cpp

static tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC, // IADs: audio and CDC are multi-interface functions
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0001,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const* tud_descriptor_device_cb(void)
{
    desc_device.idProduct = usb_disk_mode ? USB_PID_DISK : USB_PID;
    return (uint8_t const*)&desc_device;
}

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
                          4,
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

// Disk mode: usb_storage's interfaces and endpoints
#define EPNUM_DISK_CDC_NOTIF 0x81
#define EPNUM_DISK_CDC_OUT   0x02
#define EPNUM_DISK_CDC_IN    0x82
#define EPNUM_DISK_MSC_OUT   0x03
#define EPNUM_DISK_MSC_IN    0x83

#define CONFIG_DISK_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)

static uint8_t const desc_configuration_disk[] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, CONFIG_DISK_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(0, 5, EPNUM_DISK_CDC_NOTIF, 8, EPNUM_DISK_CDC_OUT, EPNUM_DISK_CDC_IN, 64),
    TUD_MSC_DESCRIPTOR(2, 7, EPNUM_DISK_MSC_OUT, EPNUM_DISK_MSC_IN, 64),
};

TU_VERIFY_STATIC(sizeof(desc_configuration_disk) == CONFIG_DISK_TOTAL_LEN,
                 "disk configuration length");

uint8_t const* tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return usb_disk_mode ? desc_configuration_disk : desc_configuration;
}

static char const* const string_desc[] = {
    NULL,           // 0: language
    "DOT RODEO",    // 1: manufacturer
    "Rodeo Demo",   // 2: product (what the computer's sound settings show)
    NULL,           // 3: serial, from the chip's unique ID
    "Rodeo audio",  // 4: audio function
    "Rodeo serial", // 5: CDC
    "Rodeo MIDI",   // 6: MIDI
    "Rodeo SD card", // 7: mass storage
    "Rodeo Demo (disk mode)", // 8: product, in disk mode
};

static uint16_t desc_str[33];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    size_t n;
    if(index == 0)
    {
        desc_str[1] = 0x0409;
        n           = 1;
    }
    else if(index == 3)
    {
        uint32_t const uid[3] = {*(uint32_t*)UID_BASE,
                                 *(uint32_t*)(UID_BASE + 4),
                                 *(uint32_t*)(UID_BASE + 8)};
        n = 0;
        for(int w = 0; w < 3; w++)
            for(int s = 28; s >= 0; s -= 4)
                desc_str[1 + n++] = "0123456789ABCDEF"[(uid[w] >> s) & 0xF];
    }
    else
    {
        if(index >= sizeof(string_desc) / sizeof(string_desc[0]) - 1)
            return NULL;
        char const* s = (index == 2 && usb_disk_mode) ? string_desc[8] : string_desc[index];
        n             = 0;
        while(s[n] && n < 32)
        {
            desc_str[1 + n] = s[n];
            n++;
        }
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return desc_str;
}
