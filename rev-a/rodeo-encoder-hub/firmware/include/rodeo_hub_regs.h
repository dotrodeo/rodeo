/*
 * rodeo_hub_regs.h — Rodeo Encoder-Hub I2C register map (contract v1)
 *
 * SHARED HEADER: the single C encoding of RODEO_HUB_I2C_CONTRACT.md. Both sides
 * include this so the byte layout can never drift between them:
 *   - hub target firmware (STM32C031C6) — serves this map from its RAM shadow
 *   - motherboard master firmware (Daisy Seed) — drives reads/writes against it
 *
 * Source of truth: rev-a/rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md (silicon-verified
 * Bumble 2026-08-20). If you change a value here, bump RODEO_HUB_MAP_VERSION and
 * the doc's MAP_VERSION together — they are one contract.
 *
 * Toolchain-independent: plain C, no vendor/MCU dependencies. Safe to include on
 * either target.
 */
#ifndef RODEO_HUB_REGS_H
#define RODEO_HUB_REGS_H

#include <stdint.h>

/* ---- Contract identity -------------------------------------------------- */
#define RODEO_HUB_WHO_AM_I      0x9Eu  /* 0x00 WHO_AM_I constant             */
#define RODEO_HUB_MAP_VERSION   0x01u  /* 0x02 MAP_VERSION (this layout)     */
#define RODEO_HUB_ENC_COUNT     9u     /* 0x04 encoder slots per hub         */

/* ---- I2C address plan (7-bit) ------------------------------------------- */
/* addr = RODEO_HUB_ADDR_BASE | (PA15<<1) | PD0   (JP2=A1/bit1, JP1=A0/bit0) */
#define RODEO_HUB_ADDR_BASE     0x30u  /* both jumpers bridged = default     */
#define RODEO_HUB_ADDR_MIN      0x30u
#define RODEO_HUB_ADDR_MAX      0x33u
/* Compose an address from the two jumper bits (1 = jumper cut). */
#define RODEO_HUB_ADDR(a1, a0)  ((uint8_t)(RODEO_HUB_ADDR_BASE | (((a1) & 1u) << 1) | ((a0) & 1u)))

/* ---- Register addresses ------------------------------------------------- */
/* Info block 0x00–0x0F */
#define RODEO_HUB_REG_WHO_AM_I      0x00u  /* R  const 0x9E                  */
#define RODEO_HUB_REG_FW_VERSION    0x01u  /* R  hub build version           */
#define RODEO_HUB_REG_MAP_VERSION   0x02u  /* R  contract version = 0x01     */
#define RODEO_HUB_REG_HUB_ADDRESS   0x03u  /* R  live 7-bit addr             */
#define RODEO_HUB_REG_ENC_COUNT     0x04u  /* R  = 0x09                      */
#define RODEO_HUB_REG_CAPABILITIES  0x05u  /* R  bitfield, see CAP_* below   */
#define RODEO_HUB_REG_STATUS        0x06u  /* R  bitfield, see STAT_* below  */
#define RODEO_HUB_REG_CMD           0x07u  /* W  command strobe, see CMD_*   */

/* Telemetry block 0x10–0x1F (contiguous 16-byte hot read) */
#define RODEO_HUB_REG_ENC_DELTA_BASE 0x10u /* R/clr  int8 x9 @ 0x10..0x18    */
#define RODEO_HUB_REG_SW_STATE_LO    0x19u /* R  live level, enc1..8         */
#define RODEO_HUB_REG_SW_STATE_HI    0x1Au /* R  bit0 = enc9                 */
#define RODEO_HUB_TELEMETRY_LEN      16u   /* burst length from 0x10         */

/* Switch press-event latches 0x20–0x21 (read-clears) */
#define RODEO_HUB_REG_SW_PRESSED_LO  0x20u /* R/clr press edges, enc1..8     */
#define RODEO_HUB_REG_SW_PRESSED_HI  0x21u /* R/clr bit0 = enc9              */

/* Config 0x30 */
#define RODEO_HUB_REG_CONFIG         0x30u /* R/W see CFG_* below            */

/* LED block 0x40–0x5F — RESERVED, no-op until CAP_LED set (future ring) */
#define RODEO_HUB_REG_LED_BASE       0x40u /* W  3 bytes/enc G,R,B @0x40..5A */
#define RODEO_HUB_REG_LED_BRIGHTNESS 0x5Bu /* W  global 0..255               */
#define RODEO_HUB_LED_BYTES_PER_ENC  3u    /* WS2812 G,R,B order             */

/* ---- Bitfields ---------------------------------------------------------- */
/* CAPABILITIES (0x05) */
#define RODEO_HUB_CAP_ENCODERS  (1u << 0)
#define RODEO_HUB_CAP_SWITCHES  (1u << 1)
#define RODEO_HUB_CAP_LED       (1u << 2)  /* 0 until ring board populated   */

/* STATUS (0x06) — bit2 read-to-clear */
#define RODEO_HUB_STAT_ENC_ACTIVITY (1u << 0)  /* a delta is pending         */
#define RODEO_HUB_STAT_SW_EVENT     (1u << 1)  /* a press latch is pending   */
#define RODEO_HUB_STAT_OVERFLOW     (1u << 2)  /* a delta saturated (r/clr)  */

/* CONFIG (0x30) */
#define RODEO_HUB_CFG_ENC_DIR_INVERT     (1u << 0)  /* flip CW/CCW, all enc  */
#define RODEO_HUB_CFG_SW_EVENT_ON_RELEASE (1u << 1) /* latch release edges   */

/* CMD (0x07) write-strobe values */
#define RODEO_HUB_CMD_CLEAR_DELTAS   0x01u
#define RODEO_HUB_CMD_CLEAR_SW_LATCH 0x02u
#define RODEO_HUB_CMD_CLEAR_ALL      0x03u
#define RODEO_HUB_CMD_SOFT_RESET     0xA5u

/* Switch bit position for a 0-based encoder index (0..8) within the LO/HI
 * byte pair: enc 0..7 -> LO bit0..7, enc 8 -> HI bit0. */
#define RODEO_HUB_SW_IS_LO(enc)   ((enc) < 8u)
#define RODEO_HUB_SW_BIT(enc)     (RODEO_HUB_SW_IS_LO(enc) ? (1u << (enc)) : (1u << ((enc) - 8u)))

#endif /* RODEO_HUB_REGS_H */
