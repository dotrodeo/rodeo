// Host stand-in for the one thing usb_desc.c reads from the STM32 header: the
// 96-bit unique ID its serial number is made from. A fixed ID, so the serial
// number string in the golden files is too.
#pragma once
#include <stdint.h>
static uint32_t const fake_uid[3] = {0x00410023u, 0x31385104u, 0x36363932u};
#define UID_BASE ((uintptr_t)fake_uid)
