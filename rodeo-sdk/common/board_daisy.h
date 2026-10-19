#pragma once
//
// board.h's pins as libDaisy Pins. Target-only: board.h stays host-safe by
// keeping libDaisy out, and this is the one place that crosses over.
//
//   GPIO gate;
//   gate.Init(board::ToDaisy(board::kIo[board::kIoD1].pin), GPIO::Mode::INPUT);
//
#include "daisy_seed.h"
#include "board.h"

namespace board
{
/** A board.h pin as the libDaisy Pin for it. */
inline daisy::Pin ToDaisy(SeedPin p)
{
    return daisy::Pin(static_cast<daisy::GPIOPort>(daisy::PORTA + (p.port - 'A')), p.num);
}
} // namespace board
