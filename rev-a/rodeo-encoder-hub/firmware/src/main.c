/*
 * main.c — rodeo-encoder-hub firmware entry.
 *
 * Boot: 48 MHz clock → read the 2 address jumpers → init hub shadow, encoder
 * poll, and I2C1 target → 1 kHz SysTick drives the encoder poll → idle in WFI.
 * See RODEO_HUB_I2C_CONTRACT.md.
 */
#include <stdint.h>
#include "stm32c0xx.h"
#include "stm32c0xx_ll_rcc.h"
#include "stm32c0xx_ll_system.h"
#include "stm32c0xx_ll_gpio.h"

#include "board.h"
#include "hub.h"
#include "encoder.h"
#include "i2c_target.h"
#include "rodeo_hub_regs.h"

extern void SystemCoreClockUpdate(void);

/* Raise the core to 48 MHz: HSISYS = HSI48 / 1, 1 flash wait state. */
static void clock_init_48mhz(void)
{
  LL_FLASH_SetLatency(LL_FLASH_LATENCY_1);   /* required >24 MHz */
  LL_RCC_SetHSIDiv(LL_RCC_HSI_DIV_1);        /* HSISYS = 48 MHz  */
  SystemCoreClockUpdate();
}

static void crude_delay(volatile uint32_t n) { while (n--) { __NOP(); } }

/* Read the 2 cuttable jumpers → 7-bit address 0x30..0x33.
 * External 10k pull-up to +3V3, jumper to GND: bridged reads LOW (0),
 * cut reads HIGH (1). Enable the internal pull-up too for a defined level. */
static uint8_t read_jumper_address(void)
{
  LL_GPIO_SetPinMode(BOARD_JP_A0_PORT, BOARD_JP_A0_PIN, LL_GPIO_MODE_INPUT);
  LL_GPIO_SetPinPull(BOARD_JP_A0_PORT, BOARD_JP_A0_PIN, LL_GPIO_PULL_UP);
  LL_GPIO_SetPinMode(BOARD_JP_A1_PORT, BOARD_JP_A1_PIN, LL_GPIO_MODE_INPUT);
  LL_GPIO_SetPinPull(BOARD_JP_A1_PORT, BOARD_JP_A1_PIN, LL_GPIO_PULL_UP);
  crude_delay(1000);   /* let the pin settle */

  uint8_t a0 = LL_GPIO_IsInputPinSet(BOARD_JP_A0_PORT, BOARD_JP_A0_PIN) ? 1u : 0u;
  uint8_t a1 = LL_GPIO_IsInputPinSet(BOARD_JP_A1_PORT, BOARD_JP_A1_PIN) ? 1u : 0u;
  return RODEO_HUB_ADDR(a1, a0);
}

int main(void)
{
  clock_init_48mhz();
  board_gpio_clocks_enable();

  uint8_t addr = read_jumper_address();
  hub_init(addr);
  encoder_init();
  i2c_target_init(addr);

  /* 1 kHz encoder poll. */
  SysTick_Config(SystemCoreClock / 1000u);

  for (;;) {
    if (hub_take_soft_reset()) {
      /* Re-read jumpers and re-arm the target at the (possibly new) address. */
      addr = read_jumper_address();
      hub_init(addr);
      i2c_target_init(addr);
    }
    __WFI();
  }
}

void SysTick_Handler(void)
{
  encoder_poll();
}
