// Every Rodeo firmware's USB descriptors, from the layout and identity its
// tusb_config.h sets (usb_desc.h). The layouts' interfaces, endpoints and
// string numbers are the ones the SDK's USB examples used before this file
// existed, so a firmware moved onto it looks the same to a computer.
#include "tusb.h"
#include "stm32h7xx.h"
#include "usb_desc.h"

// ------------------------------------------------------------------ settings

#ifndef RODEO_USB_LAYOUT
#error "tusb_config.h: set RODEO_USB_LAYOUT (usb_desc.h)"
#endif
#if !defined(RODEO_USB_VID) || !defined(RODEO_USB_PID)
#error "tusb_config.h: set RODEO_USB_VID and RODEO_USB_PID: the product's IDs (usb_desc.h)"
#endif
#if !defined(RODEO_USB_MANUFACTURER) || !defined(RODEO_USB_PRODUCT)
#error "tusb_config.h: set RODEO_USB_MANUFACTURER and RODEO_USB_PRODUCT (usb_desc.h)"
#endif
#if !CFG_TUD_MIDI
#error "every layout has MIDI, which the host protocol rides on: set CFG_TUD_MIDI 1"
#endif
#if RODEO_USB_LAYOUT != RODEO_USB_MIDI && !CFG_TUD_CDC
#error "this layout has serial: set CFG_TUD_CDC 1"
#endif
#if RODEO_USB_LAYOUT == RODEO_USB_AUDIO_CDC_MIDI && !CFG_TUD_AUDIO
#error "this layout has audio: set CFG_TUD_AUDIO 1"
#endif

#ifdef RODEO_USB_PID_DISK
#ifndef RODEO_USB_PRODUCT_DISK
#error "disk mode: set RODEO_USB_PRODUCT_DISK too"
#endif
#if RODEO_USB_LAYOUT == RODEO_USB_MIDI
#error "disk mode is serial and USB storage: use a layout with serial"
#endif
#if !CFG_TUD_MSC
#error "disk mode: set CFG_TUD_MSC 1"
#endif
extern volatile bool usb_disk_mode; // the firmware's
#define DISK_MODE usb_disk_mode
#else
#define DISK_MODE false
#endif

#ifndef RODEO_USB_BCD_DEVICE
#define RODEO_USB_BCD_DEVICE 0x0001
#endif
#ifndef RODEO_USB_NAME_AUDIO
#define RODEO_USB_NAME_AUDIO "Rodeo audio"
#endif
#ifndef RODEO_USB_NAME_SERIAL
#define RODEO_USB_NAME_SERIAL "Rodeo serial"
#endif
#ifndef RODEO_USB_NAME_MIDI
#define RODEO_USB_NAME_MIDI "Rodeo MIDI"
#endif
#ifndef RODEO_USB_NAME_DISK
#define RODEO_USB_NAME_DISK "Rodeo SD card"
#endif

// ------------------------------------------------------- layout: interfaces

// The string numbers after 1 (manufacturer), 2 (product) and 3 (serial
// number): each layout's interfaces in order, then disk mode's storage.
#if RODEO_USB_LAYOUT == RODEO_USB_MIDI
#define STR_MIDI 4
static char const* const names[] = {RODEO_USB_NAME_MIDI};

#elif RODEO_USB_LAYOUT == RODEO_USB_CDC_MIDI
#define STR_SERIAL 4
#define STR_MIDI   5
#define STR_DISK   6
static char const* const names[] = {RODEO_USB_NAME_SERIAL, RODEO_USB_NAME_MIDI,
#ifdef RODEO_USB_PID_DISK
                                    RODEO_USB_NAME_DISK
#endif
};

#else // AUDIO_CDC_MIDI, CUSTOM
#define STR_AUDIO  4
#define STR_SERIAL 5
#define STR_MIDI   6
#define STR_DISK   7
static char const* const names[] = {RODEO_USB_NAME_AUDIO, RODEO_USB_NAME_SERIAL,
                                    RODEO_USB_NAME_MIDI,
#ifdef RODEO_USB_PID_DISK
                                    RODEO_USB_NAME_DISK
#endif
};
#endif

#if RODEO_USB_LAYOUT == RODEO_USB_MIDI
// One USB-MIDI function, interfaces 0-1
#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)
static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, CONFIG_LEN, 0x00, 100),
    TUD_MIDI_DESCRIPTOR(0, STR_MIDI, 0x01, 0x81, 64),
};

#elif RODEO_USB_LAYOUT == RODEO_USB_CDC_MIDI
// Serial on interfaces 0-1, MIDI on 2-3
#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)
static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 4, 0, CONFIG_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(0, STR_SERIAL, 0x81, 8, 0x02, 0x82, 64),
    TUD_MIDI_DESCRIPTOR(2, STR_MIDI, 0x03, 0x83, 64),
};

#elif RODEO_USB_LAYOUT == RODEO_USB_AUDIO_CDC_MIDI
// Audio on interfaces 0-2, serial on 3-4, MIDI on 5-6 (usb_uac1.h's ITF_NUM_*)
#include "usb_uac1.h"
#define CONFIG_LEN \
    (TUD_CONFIG_DESC_LEN + RODEO_UAC1_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)
static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_LEN, 0x00, 100),
    RODEO_UAC1_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, STR_AUDIO, AUDIO_BYTES_PER_SAMPLE,
                          AUDIO_BITS_PER_SAMPLE, /*out*/ 0x01, /*feedback*/ 0x82,
                          /*in*/ 0x81, AUDIO_EP_SZ, AUDIO_SAMPLE_RATE),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STR_SERIAL, 0x83, 8, 0x04, 0x84, 64),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, STR_MIDI, 0x05, 0x85, 64),
};
#endif

#ifdef CONFIG_LEN
TU_VERIFY_STATIC(sizeof(desc_configuration) == CONFIG_LEN, "configuration length");
#endif

#ifdef RODEO_USB_PID_DISK
// Disk mode: serial on interfaces 0-1, USB mass storage on 2
#define CONFIG_DISK_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)
static uint8_t const desc_configuration_disk[] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, CONFIG_DISK_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(0, STR_SERIAL, 0x81, 8, 0x02, 0x82, 64),
    TUD_MSC_DESCRIPTOR(2, STR_DISK, 0x03, 0x83, 64),
};
TU_VERIFY_STATIC(sizeof(desc_configuration_disk) == CONFIG_DISK_LEN, "disk configuration length");
#endif

// ----------------------------------------------------------------- callbacks

static tusb_desc_device_t desc_device = {
    .bLength         = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB          = 0x0200,
#if RODEO_USB_LAYOUT == RODEO_USB_MIDI
    .bDeviceClass    = 0x00, // each interface says its own
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
#else
    .bDeviceClass    = TUSB_CLASS_MISC, // IADs: audio and serial span interfaces
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
#endif
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = RODEO_USB_VID,
    .idProduct          = RODEO_USB_PID,
    .bcdDevice          = RODEO_USB_BCD_DEVICE,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const* tud_descriptor_device_cb(void)
{
#ifdef RODEO_USB_PID_DISK
    desc_device.idProduct = DISK_MODE ? RODEO_USB_PID_DISK : RODEO_USB_PID;
#endif
    return (uint8_t const*)&desc_device;
}

uint8_t const* tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
#ifdef RODEO_USB_PID_DISK
    if(DISK_MODE)
        return desc_configuration_disk;
#endif
#if RODEO_USB_LAYOUT == RODEO_USB_CUSTOM
    return rodeo_usb_custom_configuration();
#else
    return desc_configuration;
#endif
}

static uint16_t desc_str[33];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    size_t      n = 0;
    char const* s = NULL;
    if(index == 0)
    {
        desc_str[1] = 0x0409; // English (US)
        n           = 1;
    }
    else if(index == 3)
    {
        // The serial number: the STM32's 96-bit unique ID as 24 hex digits
        uint32_t const uid[3] = {*(uint32_t*)UID_BASE, *(uint32_t*)(UID_BASE + 4),
                                 *(uint32_t*)(UID_BASE + 8)};
        for(int w = 0; w < 3; w++)
            for(int b = 28; b >= 0; b -= 4)
                desc_str[1 + n++] = "0123456789ABCDEF"[(uid[w] >> b) & 0xF];
    }
    else
    {
        if(index == 1)
            s = RODEO_USB_MANUFACTURER;
        else if(index == 2)
#ifdef RODEO_USB_PID_DISK
            s = DISK_MODE ? RODEO_USB_PRODUCT_DISK : RODEO_USB_PRODUCT;
#else
            s = RODEO_USB_PRODUCT;
#endif
        else if(index - 4u < sizeof names / sizeof names[0])
            s = names[index - 4];
        else
            return NULL;
        while(s[n] && n < 32)
        {
            desc_str[1 + n] = (uint8_t)s[n];
            n++;
        }
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return desc_str;
}
