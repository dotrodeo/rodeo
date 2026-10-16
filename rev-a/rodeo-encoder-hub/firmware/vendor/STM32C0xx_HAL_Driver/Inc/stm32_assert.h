/* Minimal stm32_assert.h — LL drivers include this when USE_FULL_LL_DRIVER is
 * defined. We build with asserts disabled (no USE_FULL_ASSERT), so assert_param
 * compiles to nothing. */
#ifndef STM32_ASSERT_H
#define STM32_ASSERT_H

#ifdef USE_FULL_ASSERT
#include <stdint.h>
void assert_failed(uint8_t *file, uint32_t line);
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
#else
#define assert_param(expr) ((void)0U)
#endif

#endif /* STM32_ASSERT_H */
