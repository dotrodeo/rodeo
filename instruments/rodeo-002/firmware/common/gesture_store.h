// rodeo-002 — a gesture as it sits in flash.
//
// A take is ~100 KB, which is three orders of magnitude bigger than a preset,
// so it does not live in the preset bank: each slot gets its own region, and
// whether a preset HAS a gesture is answered by that region's header rather
// than by anything in the bank. That is why adding gestures changed no stored
// format and cost no saved preset.
//
// Only the frames actually recorded are written. A four-second take is 25 KB,
// not 100, and the erase is sized to match -- which matters, because erasing
// 128 KB stalls the panel for the best part of a second.

#pragma once

#include <cstddef>
#include <cstdint>

#include "gesture.h"

namespace rodeo002
{
struct GestureHeader
{
    static constexpr uint32_t kMagic   = 0x52324753u; // "R2GS"
    static constexpr uint16_t kVersion = 1;

    uint32_t magic;
    uint16_t version;
    uint16_t lanes;
    uint32_t frames;    // 0 = this slot has no gesture
    uint32_t lane_mask; // which lanes the loop drives

    bool Valid() const
    {
        return magic == kMagic && version == kVersion
               && lanes == static_cast<uint16_t>(GestureRecorder::kLanes)
               && frames <= GestureRecorder::kMaxFrames;
    }
    bool HasGesture() const { return Valid() && frames > 0; }
};

/** Bytes a take occupies in flash, header included. */
constexpr size_t GestureBytes(size_t frames)
{
    return sizeof(GestureHeader)
           + frames * static_cast<size_t>(GestureRecorder::kLanes) * sizeof(uint16_t);
}

/** Where a slot's take lives, as an offset into QSPI.
 *
 *  0x100000 onward, 128 KB a slot: two 64 KB erase blocks, which is what a
 *  full-length 32-lane take needs. It sits in the free middle of the chip --
 *  the program ends below 0x070000 and the panel wiring map, preset bank and
 *  instrument state are all up at 0x780000 and above -- so nothing had to move. */
inline uint32_t GestureOffsetForSlot(int slot)
{
    return 0x100000u + static_cast<uint32_t>(slot) * 0x20000u;
}
inline uint32_t GestureSlotSize() { return 0x20000u; }

static_assert(GestureBytes(GestureRecorder::kMaxFrames) <= 0x20000u,
              "a full-length take must fit in its slot");

} // namespace rodeo002
