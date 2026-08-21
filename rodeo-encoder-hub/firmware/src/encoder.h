/*
 * encoder.h — 9-channel EC11 quadrature + switch reader for the hub.
 *
 * encoder_poll() is called at ~1 kHz. It decodes each encoder with a Gray-code
 * state machine (4 quarter-steps per detent), debounces each push switch, and
 * pushes the result into the register shadow via hub_apply_inputs(). Polling is
 * mandatory here: the A/B lines collide on shared EXTI lines across ports, so
 * per-edge interrupts are impossible (see RODEO_HUB_I2C_CONTRACT.md §4/§10).
 */
#ifndef ENCODER_H
#define ENCODER_H

void encoder_init(void);   /* configure all 27 lines as input + pull-up */
void encoder_poll(void);    /* one ~1 kHz cycle: decode, debounce, apply */

#endif /* ENCODER_H */
