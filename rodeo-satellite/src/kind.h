#pragma once
//
// Which board this satellite is on, from its ID resistor (contract §1.6).
//
// The divider's top is fed two ways. On the pot bank a GPIO (PC2) drives it
// during the read; on the encoder bank it's tied to 3V3 and PC2 is an encoder
// line, which must never be driven high: a closed encoder contact would short
// it to ground. One firmware serves both, before it knows which it is on, so it
// reads twice if it must:
//
//   1. Read with the feed pin left as an input. A divider tied to 3V3 reads its
//      kind now, and the feed pin is never touched.
//   2. Only if that reads as kind 0, near ground, drive the feed pin high and
//      read again: a divider fed from the pin reads ~0 until it is driven.
//
// Hardware-free: the caller supplies the ADC read and the feed pin.
//
#include "sat_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint16_t (*read_id)(void* ctx);             // one ADC reading of the ID pin
    void (*drive_feed)(void* ctx, bool high);   // true: PC2 push-pull high; false: input
    void*    ctx;
    uint16_t full_scale;                        // 1023 for the CH32V003
} sat_kind_io;

/** The board kind, 0..15, or -1 if the reading sits between bands; then
 *  *detail holds it as kind x 10, for the FAULT slot (§7). */
int sat_detect_kind(const sat_kind_io* io, uint8_t* detail);

#ifdef __cplusplus
}
#endif
