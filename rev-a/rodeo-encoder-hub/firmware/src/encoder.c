/*
 * encoder.c — see encoder.h.
 */
#include <stdint.h>
#include "board.h"
#include "hub.h"
#include "stm32c0xx_ll_gpio.h"

#define ENC_STEPS_PER_DETENT  4   /* Alps EC11: 4 quarter-steps per detent */
#define SW_DEBOUNCE_MAX       8   /* integrator ceiling (~8 ms @ 1 kHz)   */

typedef struct { GPIO_TypeDef *port; uint32_t pin; } pinref_t;

/* Build the A/B/S pin tables from the board X-macro. */
#define X_A(idx, ap, apin, bp, bpin, sp, spin)  { ap, apin },
#define X_B(idx, ap, apin, bp, bpin, sp, spin)  { bp, bpin },
#define X_S(idx, ap, apin, bp, bpin, sp, spin)  { sp, spin },
static const pinref_t enc_a[BOARD_ENC_COUNT] = { BOARD_ENC_TABLE(X_A) };
static const pinref_t enc_b[BOARD_ENC_COUNT] = { BOARD_ENC_TABLE(X_B) };
static const pinref_t enc_s[BOARD_ENC_COUNT] = { BOARD_ENC_TABLE(X_S) };
#undef X_A
#undef X_B
#undef X_S

/* Quadrature transition table: index = (prev_ab << 2) | cur_ab. */
static const int8_t qtable[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

static uint8_t ab_prev[BOARD_ENC_COUNT];
static int8_t  substep[BOARD_ENC_COUNT];
static uint8_t sw_cnt[BOARD_ENC_COUNT];
static uint8_t sw_state[BOARD_ENC_COUNT];   /* debounced level, 1 = pressed */

static void cfg_input_pullup(const pinref_t *p)
{
  LL_GPIO_SetPinMode(p->port, p->pin, LL_GPIO_MODE_INPUT);
  LL_GPIO_SetPinPull(p->port, p->pin, LL_GPIO_PULL_UP);
}

void encoder_init(void)
{
  for (unsigned i = 0; i < BOARD_ENC_COUNT; i++) {
    cfg_input_pullup(&enc_a[i]);
    cfg_input_pullup(&enc_b[i]);
    cfg_input_pullup(&enc_s[i]);
    /* Seed prev state so the first poll doesn't emit a phantom step. */
    uint8_t a = LL_GPIO_IsInputPinSet(enc_a[i].port, enc_a[i].pin) ? 1u : 0u;
    uint8_t b = LL_GPIO_IsInputPinSet(enc_b[i].port, enc_b[i].pin) ? 1u : 0u;
    ab_prev[i] = (uint8_t)((a << 1) | b);
    substep[i] = 0;
    sw_cnt[i]  = 0;
    sw_state[i] = 0;
  }
}

void encoder_poll(void)
{
  int8_t  detents[BOARD_ENC_COUNT];
  uint8_t pressed[BOARD_ENC_COUNT];

  for (unsigned i = 0; i < BOARD_ENC_COUNT; i++) {
    /* --- quadrature --- */
    uint8_t a = LL_GPIO_IsInputPinSet(enc_a[i].port, enc_a[i].pin) ? 1u : 0u;
    uint8_t b = LL_GPIO_IsInputPinSet(enc_b[i].port, enc_b[i].pin) ? 1u : 0u;
    uint8_t cur = (uint8_t)((a << 1) | b);
    int8_t step = qtable[(ab_prev[i] << 2) | cur];
    ab_prev[i] = cur;

    int acc = substep[i] + step;
    int8_t det = 0;
    while (acc >= ENC_STEPS_PER_DETENT)  { acc -= ENC_STEPS_PER_DETENT; det++; }
    while (acc <= -ENC_STEPS_PER_DETENT) { acc += ENC_STEPS_PER_DETENT; det--; }
    substep[i] = (int8_t)acc;
    detents[i] = det;

    /* --- switch: S is active-low (pressed = pin reads 0) --- */
    uint8_t raw_pressed = LL_GPIO_IsInputPinSet(enc_s[i].port, enc_s[i].pin) ? 0u : 1u;
    if (raw_pressed) { if (sw_cnt[i] < SW_DEBOUNCE_MAX) sw_cnt[i]++; }
    else             { if (sw_cnt[i] > 0)               sw_cnt[i]--; }
    if (sw_cnt[i] >= SW_DEBOUNCE_MAX) sw_state[i] = 1;
    else if (sw_cnt[i] == 0)          sw_state[i] = 0;   /* hysteresis */
    pressed[i] = sw_state[i];
  }

  hub_apply_inputs(detents, pressed);
}
