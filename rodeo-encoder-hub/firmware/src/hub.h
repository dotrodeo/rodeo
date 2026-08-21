/*
 * hub.h — the register-shadow model for the Rodeo encoder hub.
 *
 * Owns the RAM shadow described by RODEO_HUB_I2C_CONTRACT.md and implements the
 * per-register read/write semantics (read-clear deltas, switch latches, CMD
 * strobes, LED no-op). The I2C target ISR calls hub_reg_read/write byte-by-byte;
 * the ~1 kHz input poll calls hub_apply_inputs. Concurrency: hub_apply_inputs
 * runs inside a critical section that masks the I2C IRQ, so shadow updates are
 * atomic w.r.t. ISR reads.
 */
#ifndef HUB_H
#define HUB_H

#include <stdint.h>
#include "rodeo_hub_regs.h"   /* RODEO_HUB_ENC_COUNT and the register map */

/* Firmware build version reported at reg 0x01. */
#define HUB_FW_VERSION  0x01u

void    hub_init(uint8_t i2c_addr7);

/* Called by the I2C target ISR (auto-increment handled by caller). */
uint8_t hub_reg_read(uint8_t reg);          /* applies read-clear side effects */
void    hub_reg_write(uint8_t reg, uint8_t val);

/*
 * Push one poll cycle's decoded inputs into the shadow (atomic).
 *   detents[i]     : signed detents observed this cycle for encoder i
 *   pressed_now[i] : debounced switch level (1 = pressed) for encoder i
 */
void    hub_apply_inputs(const int8_t detents[RODEO_HUB_ENC_COUNT],
                         const uint8_t pressed_now[RODEO_HUB_ENC_COUNT]);

/* True (and self-clears) if a SOFT_RESET command was received. */
int     hub_take_soft_reset(void);

#endif /* HUB_H */
