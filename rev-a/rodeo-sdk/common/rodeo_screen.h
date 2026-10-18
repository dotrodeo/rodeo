// Rodeo screen primitives — title bars, list rows, scroll bars, bounded text.
//
// The platform's display is a 128x64 SSD130x (see oled_ssd130x_bulk.h), so the
// geometry here is the platform's, not any one instrument's: 21 characters of
// Font_6x8 across, five list rows under a title bar. An instrument supplies its
// own layout constants and its own Draw(); these are the pieces every list
// screen on this hardware ends up needing.
//
// TEMPLATED ON THE DISPLAY, which is what lets these render on a laptop as well
// as on the board: libDaisy's OledDisplay is a template over its driver and
// everything under it is plain C++, so a stub driver holding a framebuffer gives
// pixel-exact screens with nothing flashed. See firmware/tools/screens.
//
// Written for rodeo-002 and moved here on 2026-10-01.
//
// NOTHING WRAPS ON THIS DISPLAY: a string longer than its room draws straight
// over whatever is beside it. That is why AppendUpTo exists and why every field
// goes in through it.

#pragma once

#include <cstddef>
#include <cstdint>

#include "util/FixedCapStr.h"
#include "util/oled_fonts.h"

namespace rodeo
{
using daisy::FixedCapStr;

/** The list convention on a 128x64 panel: a 10-pixel title bar, then five rows
 *  of 6x8 on a 10-pixel pitch. An instrument may ignore these and lay out its
 *  own screen; a list screen almost never wants to. */
constexpr int kListTop  = 12;
constexpr int kRowPitch = 10;
constexpr int kListRows = 5;


/** Font_6x8 is the header's font, so a character is six pixels and the line
 *  holds twenty-one of them. Spelled once, because the header now packs two
 *  fields against each other and every number here is derived from it. */
constexpr int kChar = 6;

/** Append what fits and no more.
 *
 *  Nothing wraps on this display: a string longer than its room draws straight
 *  over whatever is to its right. Every header field is bounded by what the
 *  field beside it leaves, so every one of them goes in through here. */
template <size_t kCap>
static void AppendUpTo(FixedCapStr<kCap>& s, const char* text, int room)
{
    for(int i = 0; i < room && text[i]; i++)
        s.Append(text + i, 1);
}

/** A title bar: inverted, with an optional counter on the right. */
template <typename Display>
static void DrawTitle(Display& display, const char* text, const char* right)
{
    display.DrawRect(0, 0, 127, 9, true, true);
    display.SetCursor(2, 1);
    display.WriteString(text, Font_6x8, false);
    if(right)
    {
        int n = 0;
        while(right[n])
            n++;
        display.SetCursor(static_cast<uint8_t>(126 - 6 * n), 1);
        display.WriteString(right, Font_6x8, false);
    }
}

/** One list row. The selected row inverts rather than carrying a marker, which
 *  would eat a character of width on a 21-character line. */
template <typename Display>
static void DrawRow(Display& display, int r, const char* left, const char* right, bool selected)
{
    const uint8_t y = static_cast<uint8_t>(kListTop + r * kRowPitch);
    if(selected)
        display.DrawRect(0, static_cast<uint8_t>(y - 1), 127, static_cast<uint8_t>(y + 8),
                         true, true);
    display.SetCursor(2, y);
    display.WriteString(left, Font_6x8, !selected);
    if(right)
    {
        int n = 0;
        while(right[n])
            n++;
        display.SetCursor(static_cast<uint8_t>(126 - 6 * n), y);
        display.WriteString(right, Font_6x8, !selected);
    }
}

/** How far down a long list you are. Nothing is drawn when it all fits. */
template <typename Display>
static void DrawScrollBar(Display& display,
                          int       first,
                          int       visible,
                          int       total,
                          int       y0    = kListTop,
                          int       track = kListRows * kRowPitch - (kRowPitch - 8))
{
    if(total <= visible)
        return;
    int h = track * visible / total;
    if(h < 4)
        h = 4;
    const int top = y0 + track * first / total;
    display.DrawRect(126, static_cast<uint8_t>(top), 127,
                     static_cast<uint8_t>(top + h), true, true);
}
} // namespace rodeo
