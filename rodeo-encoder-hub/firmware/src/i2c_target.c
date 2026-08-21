/*
 * i2c_target.c — see i2c_target.h.
 *
 * Register-pointer target protocol (matches RODEO_HUB_I2C_CONTRACT.md §2):
 *   write:  [addr|W] REG [DATA...]        first RX byte = pointer, rest = data
 *   read:   [addr|W] REG Sr [addr|R] ...  pointer set by the write phase,
 *                                          then TX auto-increments from it
 */
#include "i2c_target.h"
#include "board.h"
#include "stm32c0xx.h"
#include "stm32c0xx_ll_gpio.h"
#include "stm32c0xx_ll_bus.h"
#include "stm32c0xx_ll_i2c.h"
#include "hub.h"

/*
 * TIMINGR for 100 kHz (standard mode) at a 48 MHz I2C kernel clock.
 * PROVISIONAL — derived from the ST timing tool for the G0/C0 I2C v2 cell;
 * validate SDADEL/SCLDEL on a logic analyzer at bring-up and regenerate for
 * 400 kHz once the populated bus is confirmed. (Contract: boot 100 k → 400 k.)
 */
#define I2C_TIMINGR_100K_48M  0x10707DBCu

static volatile uint8_t cur_reg;      /* auto-incrementing register pointer */
static volatile uint8_t expect_ptr;   /* next RX byte is the pointer         */

static void gpio_init(void)
{
  /* PB6=SCL, PB7=SDA — alternate function, open-drain, no pull (external). */
  LL_GPIO_InitTypeDef g = {0};
  g.Mode       = LL_GPIO_MODE_ALTERNATE;
  g.Speed      = LL_GPIO_SPEED_FREQ_HIGH;
  g.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
  g.Pull       = LL_GPIO_PULL_NO;
  g.Alternate  = BOARD_I2C1_AF;
  g.Pin        = BOARD_I2C1_SCL_PIN | BOARD_I2C1_SDA_PIN;
  LL_GPIO_Init(BOARD_I2C1_PORT, &g);
}

void i2c_target_init(uint8_t addr7)
{
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C1);
  gpio_init();

  LL_I2C_Disable(I2C1);
  LL_I2C_SetTiming(I2C1, I2C_TIMINGR_100K_48M);

  /* 7-bit own address (LL wants it left-aligned: bits [7:1]). */
  LL_I2C_SetOwnAddress1(I2C1, (uint32_t)addr7 << 1, LL_I2C_OWNADDRESS1_7BIT);
  LL_I2C_EnableOwnAddress1(I2C1);

  /* Target: let the peripheral ACK; we serve bytes on TXIS/RXNE. */
  LL_I2C_DisableOwnAddress2(I2C1);
  LL_I2C_DisableGeneralCall(I2C1);
  LL_I2C_EnableClockStretching(I2C1);   /* allowed; shadow keeps it ~zero */

  LL_I2C_Enable(I2C1);

  /* Interrupts: address match, RX, TX, STOP, NACK. */
  LL_I2C_EnableIT_ADDR(I2C1);
  LL_I2C_EnableIT_RX(I2C1);
  LL_I2C_EnableIT_TX(I2C1);
  LL_I2C_EnableIT_STOP(I2C1);
  LL_I2C_EnableIT_NACK(I2C1);

  expect_ptr = 1;
  cur_reg    = 0;

  NVIC_SetPriority(I2C1_IRQn, 1);
  NVIC_EnableIRQ(I2C1_IRQn);
}

void I2C1_IRQHandler(void)
{
  /* Address matched — a new transaction phase begins. */
  if (LL_I2C_IsActiveFlag_ADDR(I2C1)) {
    if (LL_I2C_GetTransferDirection(I2C1) == LL_I2C_DIRECTION_WRITE) {
      /* Master will send the register pointer next. */
      expect_ptr = 1;
      LL_I2C_ClearFlag_ADDR(I2C1);
    } else {
      /* Repeated-start read: keep cur_reg from the write phase. Flush any
       * stale TXDR so the first TXIS loads fresh data. */
      LL_I2C_ClearFlag_ADDR(I2C1);
      if (LL_I2C_IsActiveFlag_TXE(I2C1)) { /* TXDR empty, ready to load */ }
    }
  }

  /* Master read: load the next byte. */
  if (LL_I2C_IsActiveFlag_TXIS(I2C1)) {
    LL_I2C_TransmitData8(I2C1, hub_reg_read(cur_reg));
    cur_reg++;
  }

  /* Master write: consume a byte. */
  if (LL_I2C_IsActiveFlag_RXNE(I2C1)) {
    uint8_t b = LL_I2C_ReceiveData8(I2C1);
    if (expect_ptr) { cur_reg = b; expect_ptr = 0; }
    else            { hub_reg_write(cur_reg, b); cur_reg++; }
  }

  /* Master NACK (normal end of a read burst). */
  if (LL_I2C_IsActiveFlag_NACK(I2C1)) {
    LL_I2C_ClearFlag_NACK(I2C1);
  }

  /* STOP — end of transaction, re-arm for the next pointer. */
  if (LL_I2C_IsActiveFlag_STOP(I2C1)) {
    LL_I2C_ClearFlag_STOP(I2C1);
    if (LL_I2C_IsActiveFlag_TXE(I2C1)) {
      LL_I2C_ClearFlag_TXE(I2C1);   /* flush leftover TX data */
    }
    expect_ptr = 1;
  }
}
