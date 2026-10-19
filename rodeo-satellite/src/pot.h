#pragma once
//
// A pot reading, from the ADC to the POT slot's 0..65535.
//
// The satellite smooths lightly; the Seed does any further smoothing and
// hysteresis (contract §3.1). The reading is ratiometric: the pot and the ADC's
// reference are the same 3V3, so supply drop along the chain cancels out.
//
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A 10-bit reading stretched to 16 bits, so 0 -> 0 and 1023 -> 65535. */
static inline uint16_t pot_scale10(uint16_t x)
{
    return (uint16_t)((x << 6) | (x >> 4));
}

/** One-pole smoothing in 16.8 fixed point; shift 2 weights each new reading 1/4. */
typedef struct
{
    uint32_t y; // value << 8
} sat_pot_filter;

static inline uint16_t pot_filter(sat_pot_filter* f, uint16_t x, uint8_t shift)
{
    uint32_t in = (uint32_t)x << 8;
    if(f->y == 0 && x)
        f->y = in; // first reading: start there rather than climbing from 0
    else
        f->y = in > f->y ? f->y + ((in - f->y) >> shift) : f->y - ((f->y - in) >> shift);
    return (uint16_t)((f->y + 128) >> 8);
}

#ifdef __cplusplus
}
#endif
