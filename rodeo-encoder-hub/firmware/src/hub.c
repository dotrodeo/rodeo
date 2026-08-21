/*
 * hub.c — register-shadow model. See hub.h and RODEO_HUB_I2C_CONTRACT.md.
 */
#include "hub.h"
#include "stm32c0xx.h"   /* NVIC_*, I2C1_IRQn, __DMB */

/* The RAM shadow. Single instance, updated by the poll, served by the ISR. */
static struct {
  uint8_t  address;                        /* live 7-bit I2C address        */
  uint8_t  capabilities;                   /* reg 0x05                      */
  uint8_t  config;                         /* reg 0x30                      */
  uint8_t  overflow;                       /* sticky, mirrors STATUS bit2   */
  int8_t   enc_delta[RODEO_HUB_ENC_COUNT]; /* read-clears                   */
  uint16_t sw_state;                       /* bit i = encoder i pressed     */
  uint16_t sw_pressed;                     /* latched press edges, r/clr    */
  volatile uint8_t soft_reset;             /* set by CMD_SOFT_RESET         */
} s;

/* Mask only the I2C1 interrupt while we mutate the shadow, so a byte read in
 * the ISR can't observe a half-applied update. Cheap on M0+. */
static inline void i2c_irq_lock(void)   { NVIC_DisableIRQ(I2C1_IRQn); __DMB(); }
static inline void i2c_irq_unlock(void) { __DMB(); NVIC_EnableIRQ(I2C1_IRQn); }

void hub_init(uint8_t i2c_addr7)
{
  for (unsigned i = 0; i < RODEO_HUB_ENC_COUNT; i++)
    s.enc_delta[i] = 0;
  s.address      = i2c_addr7;
  s.capabilities = RODEO_HUB_CAP_ENCODERS | RODEO_HUB_CAP_SWITCHES; /* LED off */
  s.config       = 0;
  s.overflow     = 0;
  s.sw_state     = 0;
  s.sw_pressed   = 0;
  s.soft_reset   = 0;
}

int hub_take_soft_reset(void)
{
  if (!s.soft_reset) return 0;
  s.soft_reset = 0;
  return 1;
}

uint8_t hub_reg_read(uint8_t reg)
{
  /* Encoder deltas 0x10..0x18 — read-clears. */
  if (reg >= RODEO_HUB_REG_ENC_DELTA_BASE &&
      reg <  RODEO_HUB_REG_ENC_DELTA_BASE + RODEO_HUB_ENC_COUNT) {
    unsigned i = reg - RODEO_HUB_REG_ENC_DELTA_BASE;
    int8_t v = s.enc_delta[i];
    s.enc_delta[i] = 0;
    return (uint8_t)v;
  }

  switch (reg) {
  case RODEO_HUB_REG_WHO_AM_I:     return RODEO_HUB_WHO_AM_I;
  case RODEO_HUB_REG_FW_VERSION:   return HUB_FW_VERSION;
  case RODEO_HUB_REG_MAP_VERSION:  return RODEO_HUB_MAP_VERSION;
  case RODEO_HUB_REG_HUB_ADDRESS:  return s.address;
  case RODEO_HUB_REG_ENC_COUNT:    return (uint8_t)RODEO_HUB_ENC_COUNT;
  case RODEO_HUB_REG_CAPABILITIES: return s.capabilities;

  case RODEO_HUB_REG_STATUS: {
    uint8_t st = 0;
    for (unsigned i = 0; i < RODEO_HUB_ENC_COUNT; i++)
      if (s.enc_delta[i]) { st |= RODEO_HUB_STAT_ENC_ACTIVITY; break; }
    if (s.sw_pressed) st |= RODEO_HUB_STAT_SW_EVENT;
    if (s.overflow)   st |= RODEO_HUB_STAT_OVERFLOW;
    s.overflow = 0;                 /* OVERFLOW is read-to-clear */
    return st;
  }

  case RODEO_HUB_REG_SW_STATE_LO:  return (uint8_t)(s.sw_state & 0xFFu);
  case RODEO_HUB_REG_SW_STATE_HI:  return (uint8_t)((s.sw_state >> 8) & 0x01u);

  case RODEO_HUB_REG_SW_PRESSED_LO: {
    uint8_t v = (uint8_t)(s.sw_pressed & 0xFFu);
    s.sw_pressed &= (uint16_t)~0xFFu;
    return v;
  }
  case RODEO_HUB_REG_SW_PRESSED_HI: {
    uint8_t v = (uint8_t)((s.sw_pressed >> 8) & 0x01u);
    s.sw_pressed &= 0x00FFu;
    return v;
  }

  case RODEO_HUB_REG_CONFIG:       return s.config;
  default:                         return 0x00u;   /* reserved reads 0 */
  }
}

void hub_reg_write(uint8_t reg, uint8_t val)
{
  switch (reg) {
  case RODEO_HUB_REG_CMD:
    switch (val) {
    case RODEO_HUB_CMD_CLEAR_DELTAS:
      for (unsigned i = 0; i < RODEO_HUB_ENC_COUNT; i++) s.enc_delta[i] = 0;
      break;
    case RODEO_HUB_CMD_CLEAR_SW_LATCH:
      s.sw_pressed = 0;
      break;
    case RODEO_HUB_CMD_CLEAR_ALL:
      for (unsigned i = 0; i < RODEO_HUB_ENC_COUNT; i++) s.enc_delta[i] = 0;
      s.sw_pressed = 0;
      s.overflow   = 0;
      break;
    case RODEO_HUB_CMD_SOFT_RESET:
      s.soft_reset = 1;
      break;
    default: break;   /* unknown command: ignore */
    }
    break;

  case RODEO_HUB_REG_CONFIG:
    s.config = val & (RODEO_HUB_CFG_ENC_DIR_INVERT | RODEO_HUB_CFG_SW_EVENT_ON_RELEASE);
    break;

  default:
    /* LED block (0x40..0x5F) and all reserved/read-only regs: ACK + ignore.
     * LED is stubbed until CAP_LED; nothing to store. */
    break;
  }
}

void hub_apply_inputs(const int8_t detents[RODEO_HUB_ENC_COUNT],
                      const uint8_t pressed_now[RODEO_HUB_ENC_COUNT])
{
  const int invert = (s.config & RODEO_HUB_CFG_ENC_DIR_INVERT) ? 1 : 0;
  const int latch_release = (s.config & RODEO_HUB_CFG_SW_EVENT_ON_RELEASE) ? 1 : 0;

  i2c_irq_lock();
  for (unsigned i = 0; i < RODEO_HUB_ENC_COUNT; i++) {
    int d = detents[i];
    if (invert) d = -d;

    int sum = (int)s.enc_delta[i] + d;
    if (sum > 127)  { sum = 127;  s.overflow = 1; }
    if (sum < -128) { sum = -128; s.overflow = 1; }
    s.enc_delta[i] = (int8_t)sum;

    uint16_t bit  = (uint16_t)(1u << i);
    int prev = (s.sw_state & bit) ? 1 : 0;
    int cur  = pressed_now[i] ? 1 : 0;

    if (cur) s.sw_state |= bit; else s.sw_state &= (uint16_t)~bit;

    if (cur && !prev)                    s.sw_pressed |= bit; /* press edge  */
    else if (latch_release && !cur && prev) s.sw_pressed |= bit; /* release  */
  }
  i2c_irq_unlock();
}
