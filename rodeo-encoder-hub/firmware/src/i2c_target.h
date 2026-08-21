/*
 * i2c_target.h — I2C1 target-mode plumbing for the hub.
 *
 * Configures I2C1 (PB6/PB7) as a 7-bit target at `addr7`, serves the register
 * map from hub.c via an interrupt-driven register-pointer state machine
 * (auto-increment), and starts at 100 kHz. All bus data flows through
 * hub_reg_read()/hub_reg_write(); this module knows nothing about semantics.
 */
#ifndef I2C_TARGET_H
#define I2C_TARGET_H

#include <stdint.h>

void i2c_target_init(uint8_t addr7);   /* GPIO + I2C1 + NVIC, target mode */

#endif /* I2C_TARGET_H */
