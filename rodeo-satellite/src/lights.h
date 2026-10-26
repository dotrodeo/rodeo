#pragma once
//
// The button bank's four WS2812B-2020 LEDs: what each output slot asks for,
// turned into the 24-bit words the LEDs take.
//
// Slot i drives LED i. An RGB slot sets a steady colour; an LED slot sets white
// at its brightness, with its blink mode; NONE keeps what was there. IDENTIFY
// blinks all four white.
//
// Every channel draws 12 mA at full scale, so all four LEDs at full white take
// 144 mA. On the chain's 3V3 that's far over the contract's budget (§1.4), so
// the four are scaled together until their total fits a cap: LIGHTS_CAP_CHAIN_MA
// on chain power, LIGHTS_CAP_AUX_MA (no limit) with the aux 5 V plugged in.
//
#include <stdbool.h>
#include <stdint.h>

#include "sat_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIGHTS_N 4u
#define LIGHTS_MA_PER_CHANNEL 12u
#define LIGHTS_CAP_CHAIN_MA 24u  // ≈1/6 of full scale, spread over the four
#define LIGHTS_CAP_AUX_MA 144u   // all four at full white

typedef struct
{
    uint8_t r[LIGHTS_N], g[LIGHTS_N], b[LIGHTS_N];
    uint8_t mode[LIGHTS_N]; // a SAT_LED_ mode
} sat_lights;

void lights_init(sat_lights* l);

/** Apply a SCAN's output slots, slot i to LED i. Slots with a bad CRC, or of a
 *  kind the lights don't know, change nothing. */
void lights_take(sat_lights* l, const sat_slot* taken, uint8_t n);

/** The four LEDs' words for the driver, 0xGGRRBB (the WS2812's order), at this
 *  moment of their blink and within cap_ma in total. */
void lights_render(const sat_lights* l, uint32_t now_ms, bool identify, uint16_t cap_ma,
                   uint32_t grb[LIGHTS_N]);

/** Whether the aux 5 V is plugged in, from AUX_SENSE: half the aux voltage,
 *  ≈2.5 V with it and 0 V without, against the ADC's full scale (VDD). */
static inline bool lights_aux_present(uint16_t reading, uint16_t full_scale)
{
    return reading > full_scale / 3u;
}

#ifdef __cplusplus
}
#endif
