#pragma once
//
// A quadrature encoder, counted in detents.
//
// Bourns PEC11R-4215F-S0024: 24 pulses and 24 detents a turn, so one detent is
// one full quadrature cycle, and at rest both lines are high (open contacts,
// pulled up). Every edge moves a quarter-step count; the detent counter moves
// only when the encoder comes back to rest having gone at least half a cycle one
// way. A bounce that returns where it started counts nothing.
//
// Which way is "up" is fixed on the bench: swap `a` and `b` if it reads backwards.
//
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t prev;    // last (a << 1) | b
    int8_t  quarter; // quarter steps since the last rest
    uint8_t detents; // wraps at 256, as the ENCODER slot carries it
} sat_quad;

void quad_init(sat_quad* q, uint8_t a, uint8_t b);

/** Feed the current line levels (0 or 1). Call on every edge, or poll fast
 *  enough to see each one (~1 kHz is plenty by hand). */
void quad_update(sat_quad* q, uint8_t a, uint8_t b);

#ifdef __cplusplus
}
#endif
