// rodeo-002 — what the screen looks like.
//
// Separated from the app so it can be RENDERED ON A HOST: libDaisy's
// OledDisplay is a template over its driver, and everything it needs --
// display.h, graphics_common.h, daisy_core.h and the real font tables -- is
// plain C++ with no STM32 in it. Only the hardware driver is device-specific,
// so a stub driver with a framebuffer gives pixel-exact screens on a laptop,
// with the same fonts and the same draw calls rather than an approximation.
// See `tools/screens`. That is worth more than any external mockup tool: a
// preview that is not the real drawing code is a preview that can lie.
//
// Templated on the display for the same reason, and taking the Scope by
// reference rather than reaching for a global.

#pragma once

// FixedCapStr wants <string_view>, so the host tool builds at C++17 while the
// firmware stays gnu++14 -- the only thing the two builds do not share.
#include "util/FixedCapStr.h"

#include "gesture.h"
#include "preset.h"
#include "scope.h"
#include "ui.h"

namespace rodeo002
{
namespace screen
{
using daisy::FixedCapStr;


// --- where everything sits, 128x64 ---------------------------------------
//
// Named and checked rather than scattered as literals: the screen is 64 rows
// and every one of them is spoken for, so an overlap is a drawing bug that only
// shows up on hardware. The static_asserts below are what make the compiler
// find it instead.
namespace layout
{
constexpr int kRows = 64;

constexpr int kHeaderTop = 0, kHeaderBot = 8;  // preset name, mode, voices
constexpr int kRuleY     = 9;                  // the line under it
constexpr int kSlotTop = 11, kSlotBot = 18;    // the four slot names
constexpr int kBodyTop = 20, kBodyBot = 62;    // scope, readouts, meter

// With a readout on screen the body splits three ways.
constexpr int kStripBot  = 46;                 // scope strip ends
constexpr int kValueY    = 48;                 // name and percentage (6x8)
constexpr int kMeterTop = 57, kMeterBot = kBodyBot;
constexpr int kChoiceY   = 51;                 // "Filter: Band pass" (6x8)
// The envelope keeps the whole body; the message box floats over the strip.
constexpr int kEnvTop = 30, kEnvBot = kBodyBot;
constexpr int kMsgTop = 32, kMsgBot = 45;

static_assert(kHeaderBot < kRuleY, "header runs into its rule");
static_assert(kRuleY < kSlotTop, "rule runs into the slot row");
static_assert(kSlotBot < kBodyTop, "slot row runs into the body");
static_assert(kStripBot < kValueY, "scope strip runs into the value line");
static_assert(kValueY + 8 <= kMeterTop, "value line runs into the meter");
static_assert(kChoiceY + 8 <= kRows, "the choice line falls off the screen");
static_assert(kMeterBot < kRows, "the meter falls off the screen");
static_assert(kEnvBot + 1 < kRows, "the envelope baseline falls off the screen");
static_assert(kMsgTop >= kBodyTop && kMsgBot <= kStripBot, "the message box is not over the strip");
} // namespace layout

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
    const uint8_t y = static_cast<uint8_t>(12 + r * 10);
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
static void DrawScrollBar(Display& display, int first, int visible, int total)
{
    if(total <= visible)
        return;
    const int track = 51;
    int       h     = track * visible / total;
    if(h < 4)
        h = 4;
    const int top = 12 + track * first / total;
    display.DrawRect(126, static_cast<uint8_t>(top), 127,
                     static_cast<uint8_t>(top + h), true, true);
}

/** The output as a triggered scope, in whatever space the readout leaves. */
template <typename Display>
static void DrawScope(Display& display, Scope& scope, int y0, int y1)
{
    uint8_t y[Scope::kWidth];
    scope.Trace(y0, y1, y);
    for(int x = 1; x < Scope::kWidth; x++)
        display.DrawLine(static_cast<uint8_t>(x - 1), y[x - 1], static_cast<uint8_t>(x), y[x], true);
}

/** The envelope the moving segment belongs to, with that segment picked out.
 *  Times are drawn to a fixed budget rather than to scale: the shape is what
 *  says what the sound will do, and 124 pixels cannot hold seconds anyway. */
template <typename Display>
static void DrawEnvelope(Display& display, const DisplayState& d)
{
    const int y_top = layout::kEnvTop, y_base = layout::kEnvBot;
    const int h     = y_base - y_top;
    // NO MINIMUM WIDTH. A segment's width is its time and nothing else, so a
    // time of zero draws a vertical line -- which is what zero means. Giving
    // each segment a few pixels of floor drew a slope at zero and implied an
    // attack that was not there (Derek, 2026-09-24).
    const int wa   = static_cast<int>(d.env[0] * 32.f);
    const int wd   = static_cast<int>(d.env[1] * 32.f);
    const int hold = 16;
    const int wr   = static_cast<int>(d.env[3] * 32.f);
    const int y_sus = y_base - static_cast<int>(d.env[2] * static_cast<float>(h));

    int x[5], y[5];
    x[0] = 2;            y[0] = y_base;
    x[1] = x[0] + wa;    y[1] = y_top;
    x[2] = x[1] + wd;    y[2] = y_sus;
    x[3] = x[2] + hold;  y[3] = y_sus;
    x[4] = x[3] + wr;    y[4] = y_base;

    for(int i = 0; i < 4; i++)
    {
        // The segment being moved is drawn twice, a pixel apart: on a 1-bit
        // display that is the only "highlight" available. Decay owns the
        // release segment as well -- this is an ADS envelope -- so both light
        // up, which is what that knob does to the sound.
        const bool moving = i == d.env_moving || (d.env_moving == 1 && i == 3);
        display.DrawLine(static_cast<uint8_t>(x[i]), static_cast<uint8_t>(y[i]),
                         static_cast<uint8_t>(x[i + 1]), static_cast<uint8_t>(y[i + 1]), true);
        if(moving)
            display.DrawLine(static_cast<uint8_t>(x[i]), static_cast<uint8_t>(y[i] - 1),
                             static_cast<uint8_t>(x[i + 1]), static_cast<uint8_t>(y[i + 1] - 1),
                             true);
    }
    display.DrawLine(0, static_cast<uint8_t>(y_base + 1), 127,
                     static_cast<uint8_t>(y_base + 1), true);
}

/** 128x64: header, slot row, body, and whatever modal layer is up. */
template <typename Display>
void Draw(Display& display, const DisplayState& d, Scope& scope)
{
    display.Fill(false);

    // Header: the preset you are in, whether you have changed it, the voice
    // mode in three letters, and the voices in use. The ENGINE is not here any
    // more -- it announces itself when you turn to it, and the preset's name is
    // the more useful thing to have in front of you.
    // SHIFT inverts the header rather than spending a line on the word. It is
    // a held state, so it wants to be unmissable and to cost nothing: the
    // bottom of the screen is worth more as waveform.
    const bool inv = d.shift;
    if(inv)
        display.DrawRect(0, layout::kHeaderTop, 127, layout::kHeaderBot, true, true);
    // While SHIFT is held the preset's name is the least useful thing on this
    // screen and the gesture nobody can guess is the most useful: holding MAIN
    // and turning it chooses the voice mode, which is shown to the right, so
    // the hint and its result are on the same line.
    FixedCapStr<22> line("");
    if(inv)
        line.Append("TURN: MODE");
    else if(d.notice[0])
        // What the instrument did on its own, where it cannot cover what you
        // did: the engine you just turned onto is in the middle of the screen.
        line.Append(d.notice);
    else
    {
        line.Append(d.preset_name);
        if(d.preset_dirty)
            line.Append("*");
    }
    display.SetCursor(0, 0);
    display.WriteString(line, Font_6x8, !inv);
    // What the recorder is doing displaces the voice mode while it is doing it:
    // knowing a take is running matters more than knowing you are in Poly, and
    // it has to be visible without opening anything.
    display.SetCursor(128 - 6 * 7, 0);
    display.WriteString(d.gesture[0] ? d.gesture : d.voice_short, Font_6x8, !inv);
    FixedCapStr<8> voices_str("");
    voices_str.AppendInt(d.voices_active);
    voices_str.Append("/");
    voices_str.AppendInt(d.voices_max);
    display.SetCursor(128 - 6 * 3, 0);
    display.WriteString(voices_str, Font_6x8, !inv);
    display.DrawLine(0, layout::kRuleY, 127, layout::kRuleY, true);

    // The chain: four slot names, bypassed ones struck through.
    for(int s = 0; s < fx::TextureChain::kNumSlots; s++)
    {
        const uint8_t x = static_cast<uint8_t>(s * 32);
        FixedCapStr<8> name("");
        for(int i = 0; i < 5 && d.slot_type[s][i]; i++)
            name.Append(d.slot_type[s] + i, 1);
        // FREEZE belongs to Space, so it is shown ON Space rather than as a
        // word somewhere else.
        const bool frozen = d.freeze && s == fx::TextureChain::kSpace;
        if(frozen)
            display.DrawRect(x, layout::kSlotTop, static_cast<uint8_t>(x + 31),
                             layout::kSlotBot, true, true);
        display.SetCursor(x + 1, 12);
        display.WriteString(name, Font_4x6, !frozen);
        if(d.slot_bypass[s])
            display.DrawLine(x + 1, 15, x + 29, 15, !frozen);
    }

    // The middle of the screen belongs to the sound, and to whatever you are
    // changing about it -- both at once, so you can watch the shape move as you
    // turn. The scope is a strip above the readout when there is a readout, and
    // takes the whole space when there is not.
    // THE BODY RUNS TO THE BOTTOM. There was a text line reserved down here for
    // shift, freeze and messages, empty in almost every frame; those have moved
    // onto the things they describe, and the waveform got the space. The strip
    // above a readout is now 27 rows where it was 18, and with nothing on
    // screen the scope has all 43.
    // ALWAYS THE SAME HEIGHT. The rows under the strip stay reserved whether or
    // not there is a readout in them, so the waveform never changes size under
    // you (Derek, 2026-09-24) -- a scope that resizes as a popup comes and goes
    // is harder to read than a smaller one that holds still.
    const bool envelope = d.readout == Readout::kEnvelope;
    if(d.mode == UiMode::kNormal && !envelope)
        DrawScope(display, scope, layout::kBodyTop, layout::kStripBot);

    switch(d.readout)
    {
        case Readout::kNone: break; // the scope has it

        case Readout::kChoice:
        {
            // A stepped pot is a choice: name it. A meter here would say
            // nothing about WHICH destination you landed on.
            FixedCapStr<32> choice(d.param);
            choice.Append(": ");
            choice.Append(d.choice);
            display.SetCursor(0, layout::kChoiceY);
            display.WriteString(choice, Font_6x8, true);
            break;
        }

        case Readout::kEnvelope:
            display.SetCursor(0, layout::kBodyTop);
            display.WriteString(d.param, Font_6x8, true);
            DrawEnvelope(display, d);
            break;

        default:
        {
            display.SetCursor(0, layout::kValueY);
            display.WriteString(d.param, Font_6x8, true);
            // The number, because a bar says roughly and a knob deserves
            // exactly. Pickup puts which way to turn in front of it: the value
            // shown is the one being chased, not the one the pot is at.
            FixedCapStr<12> pct("");
            if(d.pickup_hint != 0)
                pct.Append(d.pickup_hint > 0 ? "UP " : "DN ");
            pct.AppendInt(static_cast<int>(d.value * 100.f + 0.5f));
            pct.Append("%");
            const int w_px = 6 * static_cast<int>(pct.Size());
            display.SetCursor(static_cast<uint8_t>(128 - w_px), layout::kValueY);
            display.WriteString(pct, Font_6x8, true);

            const uint8_t w = static_cast<uint8_t>(d.value * 124.f);
            display.DrawRect(0, layout::kMeterTop, 127, layout::kMeterBot, true);
            if(w > 1)
                display.DrawRect(2, layout::kMeterTop + 1, static_cast<uint8_t>(2 + w),
                                 layout::kMeterBot - 1, true, true);
            break;
        }
    }

    // THE MODAL LAYER IS A LIST, AND IT TAKES THE WHOLE SCREEN. It used to be
    // a box showing one entry at a time, which meant turning blind: you could
    // not see what was above or below, so the order had to be memorised. That
    // is most of what "difficult to navigate" was (Derek, 2026-09-29). The Ui
    // has already worked out which rows are visible and what they say, so this
    // draws rows without knowing whether they are presets, settings or a menu.
    if(d.mode != UiMode::kNormal)
    {
        display.Fill(false);
        FixedCapStr<12> count("");
        if(d.list_total)
        {
            count.AppendInt(d.list_index + 1);
            count.Append("/");
            count.AppendInt(d.list_total);
        }
        DrawTitle(display, d.list_title, count.Size() ? count.Cstr() : nullptr);

        if(d.mode == UiMode::kName)
        {
            // Not a list: one word being spelled, as large as it will go, with
            // the character under the cursor underlined.
            display.SetCursor(4, 18);
            display.WriteString(d.name_text, Font_11x18, true);
            const uint8_t x = static_cast<uint8_t>(4 + d.name_cursor * 11);
            display.DrawLine(x, 38, static_cast<uint8_t>(x + 9), 38, true);
            display.SetCursor(2, 46);
            display.WriteString("MAIN spells    NAV moves", Font_4x6, true);
            display.SetCursor(2, 55);
            display.WriteString("NAV tap saves    hold goes back", Font_4x6, true);
        }
        else if(d.mode == UiMode::kResetPage)
        {
            display.SetCursor(2, 14);
            display.WriteString("Erases presets, gestures", Font_6x8, true);
            display.SetCursor(2, 24);
            display.WriteString("and settings.", Font_6x8, true);
            DrawRow(display, 3, "Factory reset", d.reset_armed ? "YES" : "NO", true);
            display.SetCursor(2, 56);
            display.WriteString(d.reset_armed ? "NAV tap ERASES it all"
                                              : "MAIN turns the answer",
                                Font_4x6, true);
        }
        else
        {
            for(int r = 0; r < d.list_rows; r++)
                DrawRow(display,
                        r,
                        d.list_left[r],
                        d.list_right[r],
                        d.list_first + r == d.list_index);
            DrawScrollBar(display, d.list_first, DisplayState::kListRows, d.list_total);
        }
    }

    // Messages and a pending hold are moments, not states, so they appear over
    // what is there and then go away again -- rather than a line held empty for
    // them all the time.
    if(d.mode == UiMode::kNormal && (d.pending[0] || d.message[0]))
    {
        FixedCapStr<32> msg("");
        if(d.pending[0])
        {
            msg.Append("HOLD: ");
            msg.Append(d.pending);
        }
        else
            msg.Append(d.message);
        const int w  = 6 * static_cast<int>(msg.Size()) + 7;
        const int x0 = w >= 128 ? 0 : (128 - w) / 2;
        const int x1 = x0 + w > 127 ? 127 : x0 + w;
        display.DrawRect(static_cast<uint8_t>(x0), layout::kMsgTop, static_cast<uint8_t>(x1),
                         layout::kMsgBot, false, true);
        display.DrawRect(static_cast<uint8_t>(x0), layout::kMsgTop, static_cast<uint8_t>(x1),
                         layout::kMsgBot, true);
        display.SetCursor(static_cast<uint8_t>(x0 + 4), layout::kMsgTop + 3);
        display.WriteString(msg, Font_6x8, true);
    }
}


} // namespace screen
} // namespace rodeo002
