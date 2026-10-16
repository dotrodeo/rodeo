/*
 * board.h — rodeo-encoder-hub (STM32C031C6) pin map.
 *
 * Source of truth: RODEO_HUB_I2C_CONTRACT.md §10 (verified Bumble 2026-08-20)
 * and RODEO_CONTROL_MAP.md. A/B/S are uniform across all 9 slots:
 * A = connector pin1, B = pin2, S = pin3. Slot N = connector J(N+2).
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32c0xx_ll_gpio.h"
#include "stm32c0xx_ll_bus.h"

/* ---- System clock: HSISYS = HSI48 / 1 = 48 MHz ------------------------- */
#define BOARD_SYSCLK_HZ   48000000UL

/* ---- Encoder count ----------------------------------------------------- */
#define BOARD_ENC_COUNT   9u

/*
 * Encoder pin table as an X-macro. Each row:
 *   X(idx, A_port, A_pin, B_port, B_pin, S_port, S_pin)
 * Expanded by encoder.c into the poll table.
 */
#define BOARD_ENC_TABLE(X)                                                        \
  X(0, GPIOA, LL_GPIO_PIN_0,  GPIOA, LL_GPIO_PIN_1,  GPIOA, LL_GPIO_PIN_2)  /*E1 J3 */ \
  X(1, GPIOA, LL_GPIO_PIN_3,  GPIOA, LL_GPIO_PIN_4,  GPIOA, LL_GPIO_PIN_5)  /*E2 J4 */ \
  X(2, GPIOA, LL_GPIO_PIN_6,  GPIOA, LL_GPIO_PIN_7,  GPIOB, LL_GPIO_PIN_0)  /*E3 J5 */ \
  X(3, GPIOB, LL_GPIO_PIN_1,  GPIOB, LL_GPIO_PIN_2,  GPIOB, LL_GPIO_PIN_10) /*E4 J6 */ \
  X(4, GPIOB, LL_GPIO_PIN_11, GPIOB, LL_GPIO_PIN_12, GPIOB, LL_GPIO_PIN_13) /*E5 J7 */ \
  X(5, GPIOB, LL_GPIO_PIN_14, GPIOB, LL_GPIO_PIN_15, GPIOA, LL_GPIO_PIN_8)  /*E6 J8 */ \
  X(6, GPIOA, LL_GPIO_PIN_9,  GPIOC, LL_GPIO_PIN_6,  GPIOC, LL_GPIO_PIN_7)  /*E7 J9 */ \
  X(7, GPIOA, LL_GPIO_PIN_10, GPIOA, LL_GPIO_PIN_11, GPIOA, LL_GPIO_PIN_12) /*E8 J10*/ \
  X(8, GPIOB, LL_GPIO_PIN_3,  GPIOB, LL_GPIO_PIN_4,  GPIOB, LL_GPIO_PIN_5)  /*E9 J11*/

/* ---- I2C1 target: PB6 = SCL, PB7 = SDA (AF6) --------------------------- */
#define BOARD_I2C1_PORT       GPIOB
#define BOARD_I2C1_SCL_PIN    LL_GPIO_PIN_6
#define BOARD_I2C1_SDA_PIN    LL_GPIO_PIN_7
#define BOARD_I2C1_AF         LL_GPIO_AF_6

/* ---- Address jumpers: JP1 = PD0 (A0/bit0), JP2 = PA15 (A1/bit1) --------
 * 10k pull-up to +3V3, cuttable to GND. Bridged (default) reads 0.
 * We enable internal pull-ups too and read: bit = (pin reads HIGH ? cut : 0).
 * A cut jumper leaves the pin pulled HIGH = a 1 bit. */
#define BOARD_JP_A0_PORT      GPIOD
#define BOARD_JP_A0_PIN       LL_GPIO_PIN_0
#define BOARD_JP_A1_PORT      GPIOA
#define BOARD_JP_A1_PIN       LL_GPIO_PIN_15

/* ---- LED data (reserved, stubbed): PB8 = TIM16_CH1 (AF2) --------------- */
#define BOARD_LED_PORT        GPIOB
#define BOARD_LED_PIN         LL_GPIO_PIN_8
#define BOARD_LED_AF          LL_GPIO_AF_2

/* Enable clocks for every GPIO port the board uses. */
static inline void board_gpio_clocks_enable(void)
{
  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA |
                          LL_IOP_GRP1_PERIPH_GPIOB |
                          LL_IOP_GRP1_PERIPH_GPIOC |
                          LL_IOP_GRP1_PERIPH_GPIOD);
}

#endif /* BOARD_H */
