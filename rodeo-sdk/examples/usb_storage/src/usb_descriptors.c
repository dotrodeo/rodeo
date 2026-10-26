// USB descriptors for the SD-as-USB-drive test. Normal mode: CDC serial + MIDI.
// Disk mode: CDC serial + mass storage (the SD card). See main.cpp.
//
// Each mode reuses the interfaces and endpoints of a set an earlier test already
// gave a pid.codes test PID, so no new PID is needed and a host never applies a
// cached configuration to a different interface set: normal mode is
// usb_midi_cdc's (0x0001), disk mode is usb_device's disk mode (0x0003).
#include "tusb.h"
#include "stm32h7xx.h"

extern volatile bool usb_disk_mode; // main.cpp

// pid.codes test VID/PIDs, for development only. A product needs its own PIDs.
#define USB_VID      0x1209
#define USB_PID      0x0001
#define USB_PID_DISK 0x0003

static tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC, // IAD: CDC spans two interfaces
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

// Normal mode: usb_midi_cdc's interfaces and endpoints
#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT   0x02
#define EPNUM_CDC_IN    0x82
#define EPNUM_MIDI_OUT  0x03
#define EPNUM_MIDI_IN   0x83

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 4, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(0, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_MIDI_DESCRIPTOR(2, 5, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
};

TU_VERIFY_STATIC(sizeof(desc_configuration) == CONFIG_TOTAL_LEN, "configuration length");

// Disk mode: usb_device's disk-mode interfaces and endpoints
#define EPNUM_DISK_CDC_NOTIF 0x81
#define EPNUM_DISK_CDC_OUT   0x02
#define EPNUM_DISK_CDC_IN    0x82
#define EPNUM_DISK_MSC_OUT   0x03
#define EPNUM_DISK_MSC_IN    0x83

#define CONFIG_DISK_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)

static uint8_t const desc_configuration_disk[] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, CONFIG_DISK_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(0, 4, EPNUM_DISK_CDC_NOTIF, 8, EPNUM_DISK_CDC_OUT, EPNUM_DISK_CDC_IN, 64),
    TUD_MSC_DESCRIPTOR(2, 6, EPNUM_DISK_MSC_OUT, EPNUM_DISK_MSC_IN, 64),
};

TU_VERIFY_STATIC(sizeof(desc_configuration_disk) == CONFIG_DISK_TOTAL_LEN,
                 "disk configuration length");

uint8_t const* tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return usb_disk_mode ? desc_configuration_disk : desc_configuration;
}

static char const* const string_desc[] = {
    NULL,               // 0: language
    "DOT RODEO",        // 1: manufacturer
    "Rodeo",            // 2: product
    NULL,               // 3: serial, from the chip's unique ID
    "Rodeo serial",     // 4: CDC
    "Rodeo MIDI",       // 5: MIDI
    "Rodeo SD card",    // 6: mass storage
    "Rodeo (disk mode)" // 7: product, in disk mode
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
        char const* s = (index == 2 && usb_disk_mode) ? string_desc[7] : string_desc[index];
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
