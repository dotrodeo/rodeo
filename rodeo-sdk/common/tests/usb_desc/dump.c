// Asks a firmware's USB descriptors for everything a computer asks for at
// enumeration, through the same TinyUSB callbacks, and prints every byte: the
// device descriptor, the configuration, and strings 0-11, in normal mode and,
// for a firmware with one, disk mode. check.sh compares the output with
// golden/<firmware>.txt. Built on the host against the firmware's own
// tusb_config.h, the SDK's usb_desc.c and, for a custom layout, the firmware's
// usb_descriptors.c.
#include <stdbool.h>
#include <stdio.h>
#include "tusb.h"

#ifdef HAS_DISK_MODE
volatile bool usb_disk_mode; // the firmware's, in the real build
#endif

static void hex(const char* what, const uint8_t* p, unsigned n)
{
    printf("%s (%u):", what, n);
    for(unsigned i = 0; i < n; i++)
        printf(" %02x", p[i]);
    printf("\n");
}

static void dump(const char* mode)
{
    printf("== %s\n", mode);
    const uint8_t* d = tud_descriptor_device_cb();
    hex("device", d, d[0]);
    const uint8_t* c = tud_descriptor_configuration_cb(0);
    hex("configuration", c, (unsigned)(c[2] | c[3] << 8)); // wTotalLength
    for(int i = 0; i < 12; i++)
    {
        const uint16_t* s = tud_descriptor_string_cb((uint8_t)i, 0x0409);
        char            what[16];
        snprintf(what, sizeof what, "string %d", i);
        if(!s)
            printf("%s: none\n", what);
        else
            hex(what, (const uint8_t*)s, (unsigned)(s[0] & 0xff)); // bLength
    }
}

int main(void)
{
    dump("normal");
#ifdef HAS_DISK_MODE
    usb_disk_mode = true;
    dump("disk");
#endif
    return 0;
}
