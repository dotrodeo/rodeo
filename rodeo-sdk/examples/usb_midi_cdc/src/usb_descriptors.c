// USB descriptors for the USB step-1 test: one composite device, CDC serial + MIDI.
#include "tusb.h"
#include "stm32h7xx.h"

// pid.codes test VID/PID, for development only. A product needs its own PID.
#define USB_VID 0x1209
#define USB_PID 0x0001

static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    // IAD: the CDC function spans two interfaces, so the device class is "misc"
    .bDeviceClass       = TUSB_CLASS_MISC,
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
    return (uint8_t const*)&desc_device;
}

enum
{
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_MIDI,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL
};

// Every endpoint address distinct: a shared address is the classic composite-MIDI
// failure (tinyusb discussion #2162).
#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT   0x02
#define EPNUM_CDC_IN    0x82
#define EPNUM_MIDI_OUT  0x03
#define EPNUM_MIDI_IN   0x83

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 5, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
};

uint8_t const* tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

static char const* const string_desc[] = {
    NULL,                 // 0: language, below
    "DOT RODEO",          // 1: manufacturer
    "Rodeo USB test",     // 2: product
    NULL,                 // 3: serial, from the chip's unique ID
    "Rodeo serial",       // 4: CDC interface
    "Rodeo MIDI",         // 5: MIDI interface
};

static uint16_t desc_str[33];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    size_t n;
    if(index == 0)
    {
        desc_str[1] = 0x0409; // English (US)
        n           = 1;
    }
    else if(index == 3)
    {
        // 96-bit unique device ID as 24 hex digits
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
        char const* s = string_desc[index];
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
