// USB descriptors for the USB device test. Normal mode: UAC1 audio + CDC serial + MIDI.
// Disk mode: CDC serial + mass storage (a RAM disk). See main.cpp.
#include "tusb.h"
#include "usb_descriptors.h"
#include "stm32h7xx.h"

// pid.codes test VID/PID, for development only. A product needs its own PIDs.
// Each interface set gets its own PID so a host never reuses a cached configuration
// for a different one: 0x0001 the MIDI + CDC test, 0x0002 normal mode, 0x0003 disk mode.
#define USB_VID      0x1209
#define USB_PID      0x0002
#define USB_PID_DISK 0x0003

static tusb_desc_device_t const desc_device = {
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

static tusb_desc_device_t desc_device_disk;

uint8_t const* tud_descriptor_device_cb(void)
{
    if(!usb_disk_mode)
        return (uint8_t const*)&desc_device;
    desc_device_disk           = desc_device;
    desc_device_disk.idProduct = USB_PID_DISK;
    return (uint8_t const*)&desc_device_disk;
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

// Disk mode: serial (to report and to leave the mode) + USB storage
#define EPNUM_DISK_CDC_NOTIF 0x81
#define EPNUM_DISK_CDC_OUT   0x02
#define EPNUM_DISK_CDC_IN    0x82
#define EPNUM_DISK_MSC_OUT   0x03
#define EPNUM_DISK_MSC_IN    0x83

#define CONFIG_DISK_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)

static uint8_t const desc_configuration_disk[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_DISK_TOTAL, 0, CONFIG_DISK_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_DISK_CDC, 5, EPNUM_DISK_CDC_NOTIF, 8, EPNUM_DISK_CDC_OUT, EPNUM_DISK_CDC_IN, 64),
    TUD_MSC_DESCRIPTOR(ITF_DISK_MSC, 7, EPNUM_DISK_MSC_OUT, EPNUM_DISK_MSC_IN, 64),
};

TU_VERIFY_STATIC(sizeof(desc_configuration_disk) == CONFIG_DISK_TOTAL_LEN, "disk configuration length");

uint8_t const* tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return usb_disk_mode ? desc_configuration_disk : desc_configuration;
}

static char const* const string_desc[] = {
    NULL,               // 0: language
    "DOT RODEO",        // 1: manufacturer
    "Rodeo",            // 2: product (what the computer's sound settings show)
    NULL,               // 3: serial, from the chip's unique ID
    "Rodeo audio",      // 4: audio function
    "Rodeo serial",     // 5: CDC
    "Rodeo MIDI",       // 6: MIDI
    "Rodeo disk",       // 7: mass storage
    "Rodeo (disk mode)" // 8: product, in disk mode
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
        if(index >= sizeof(string_desc) / sizeof(string_desc[0]))
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
