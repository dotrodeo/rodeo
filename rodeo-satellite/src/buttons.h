#pragma once
//
// Up to four push switches on their own pins (the button bank), sampled once a
// millisecond. A switch changes state once it has read the other way for
// BUTTONS_STABLE_MS in a row, and each debounced press bumps its counter.
//
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTONS_STABLE_MS 5u

typedef struct
{
    uint8_t pressed;    // debounced, bit i: switch i+1 held
    uint8_t run[4];     // ms each switch has read differently from `pressed`
    uint8_t presses[4]; // per switch, +1 on each debounced press, wraps
} sat_buttons;

void buttons_init(sat_buttons* b);

/** One millisecond's sample, bit i set while switch i+1 reads pressed. */
void buttons_update(sat_buttons* b, uint8_t raw);

#ifdef __cplusplus
}
#endif
