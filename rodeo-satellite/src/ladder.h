#pragma once
//
// The encoder bank's four push switches on one ADC pin.
//
// SW_SENSE is pulled up by 10 k (R6), and each pressed switch adds its own
// resistor to ground: 10 k, 20 k, 40.2 k, 80.6 k for switches 1-4 (R7, R11,
// R14, R17). Every one of the 16 combinations gives a different voltage; the
// decoder picks the nearest and accepts a combination once it has read the same
// one three times running.
//
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LADDER_PULL_UP 10000u
#define LADDER_STABLE 3u
/** A switch the board doesn't have (the single encoder has only switch 1). Its
 *  conductance comes out as 0, so every combination with it reads as without it
 *  and the decoder, which takes the lowest of equal levels, never reports it. */
#define LADDER_ABSENT 0xFFFFFFFFu

typedef struct
{
    uint16_t level[16]; // expected reading for each mask (bit i: switch i+1)
    uint8_t  pressed;   // the debounced mask
    uint8_t  candidate;
    uint8_t  seen;
    uint8_t  presses[4]; // per switch, +1 on each debounced press, wraps
} sat_ladder;

/** Work out the 16 levels for the given resistors and ADC full scale. */
void ladder_init(sat_ladder* l, const uint32_t r_switch[4], uint32_t r_pull, uint16_t full_scale);

/** Nearest combination to one reading, without debouncing. */
uint8_t ladder_decode(const sat_ladder* l, uint16_t reading);

/** Feed a reading; updates `pressed` and `presses` once it is stable. */
void ladder_update(sat_ladder* l, uint16_t reading);

#ifdef __cplusplus
}
#endif
