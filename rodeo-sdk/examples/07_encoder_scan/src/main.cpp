// Rodeo bring-up 07 — encoder-hub scan (shared-I2C rodeo-encoder-hub boards),
// with OLED readout so the bench test needs no serial monitor.
//
// Brings up the Layer 2 rodeo::RodeoEncoders scanner: enumerates the hubs at
// 0x30..0x33 on I2C1 (the OLED+hub bus, SCL D11 / SDA D12), round-robin polls
// each hub's hot telemetry block, and shows the result BOTH on the SSD1309 and
// over USB serial using the friendly relative-encoder API.
//
// --- This example also tests the hub's I2C pass-through --------------------
// The hub is the Rev-A BUS ANCHOR: it carries the only pull-ups on the bus
// (2 x 2.2 kΩ, LOCKED 2026-08-20 in RODEO_HUB_I2C_CONTRACT.md §Pull-ups). The
// produced motherboard has none and the HiLetgo SSD1309 has none, so the
// accepted consequence is **no hub = no I2C at all**. Two useful corollaries
// for bench triage:
//
//   - The OLED showing ANYTHING already proves the hub is powered, its pull-ups
//     are alive, and the pass-through connector is wired through. That is the
//     first pass criterion, before you even turn a knob.
//   - Hub present but OLED dark => the pass-through leg or the OLED module,
//     not the bus. Hub absent AND OLED dark => the bus itself. The boot banner
//     prints exactly this pair so serial can still triage a dark screen.
//
// --- One bus, one speed ----------------------------------------------------
// RodeoEncoders and the OLED transport each own a daisy::I2CHandle, but libDaisy
// keeps ONE impl per peripheral (`static I2CHandle::Impl i2c_handles[4]`, and
// `pimpl_ = &i2c_handles[periph]`), so both objects drive the SAME I2C1 and the
// LAST Init() wins on speed. They must therefore agree: kI2cSpeed below is used
// for both. Contract boot speed is 100 kHz — promote both to 400 kHz together
// once Bumble validates the populated bus.
//
// At 100 kHz a full 128x64 frame is ~93 ms even with the bulk transport, which
// is why the display refreshes at ~6 Hz and yields to the hub poll between
// pages — exactly the "schedule partial / low-rate OLED refresh against the hub
// polls" the contract §8 calls for. Detents are never lost while a frame is out:
// the hub accumulates them and Update() folds them into RAM.
//
//   Boot: prints which of 0x30..0x33 answered WHO_AM_I=0x9E (a hub) with a
//         matching MAP_VERSION, plus each present hub's FW version + caps, and
//         whether the OLED ACKed.
//   OLED: a 3x3 grid of the first present hub's nine slots showing running
//         position, '*' while held, and a footer with the last event.
//   Live serial view (~10 Hz): one row per encoder that MOVED or is HELD.
//   Tap latch (event): a press edge shorter than a poll interval still prints.
//
//   PASS: the OLED lights up (pass-through OK) -> turn any encoder -> its cell
//         counts up CW / down CCW and the footer names it; press -> '*' appears;
//         a fast tap still registers in the footer; an absent hub's address
//         prints "no hub" at boot and its rows never appear.
//
// Bench harness for the multi-hub enumerate + poll validation (needs a fabbed
// hub on the Qwiic bus). See rev-a/rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md.

#include "daisy_seed.h"
#include "dev/oled_ssd130x.h" // OLED driver (not pulled in by daisy_seed.h)
#include "board.h"
#include "rodeo_encoders.h"
#include "status_led.h"
#include "oled_ssd130x_bulk.h"

using namespace daisy;

// Bulk transport: one I2C transaction per 128-byte page instead of libDaisy's
// one-per-BYTE, plus a yield hook so the hub poll keeps running between pages.
using OledType
    = OledDisplay<SSD130xDriver<128, 64, rodeo::Ssd130xI2cBulkTransport>>;

DaisySeed            hw;
rodeo::RodeoEncoders enc;
rodeo::StatusLed     status;
OledType             display;

// ONE speed for the whole bus — see the "One bus, one speed" note above.
static constexpr I2CHandle::Config::Speed kI2cSpeed
    = I2CHandle::Config::Speed::I2C_100KHZ;

static constexpr uint8_t kHubs  = rodeo::RodeoEncoders::kMaxHubs;
static constexpr uint8_t kSlots = rodeo::RodeoEncoders::kEncPerHub;

static bool     g_have_oled = false;
static uint8_t  g_shown_hub = 0; // first present hub, the one drawn on screen

// Last thing that happened, for the OLED footer (serial has the full log).
static char     g_event[20] = "waiting...";

// FixedCapStr has AppendInt but no hex, and an I2C address shown in decimal
// under an "0x" prefix would be actively misleading on the bench.
template <size_t N>
static void AppendHex8(FixedCapStr<N> &s, uint8_t v)
{
    static const char kHex[] = "0123456789ABCDEF";
    s.Append(kHex[(v >> 4) & 0x0F]);
    s.Append(kHex[v & 0x0F]);
}

static void SetEvent(uint8_t slot, const char *what, int amount, bool signed_amt)
{
    // Small manual format: the Daisy logger has no sprintf into a buffer here.
    FixedCapStr<20> s("e");
    s.AppendInt(slot + 1);
    s.Append(" ");
    s.Append(what);
    if(signed_amt)
    {
        s.Append(" ");
        s.AppendInt(amount, true); // alwaysIncludeSign
    }
    int i = 0;
    for(; i < 19 && s.Cstr()[i]; ++i)
        g_event[i] = s.Cstr()[i];
    g_event[i] = '\0';
}

// OLED yield hook: runs between display pages so the hub keeps being polled
// while a frame is going out. Deliberately does NOT log — hw.PrintLine can
// block, and this runs in the middle of an I2C frame.
static void PumpEncoders()
{
    enc.Update();
}

static void DrawScreen()
{
    const uint16_t w = display.Width();

    display.Fill(false);

    // --- header: which hub we're showing, and how many answered -------------
    FixedCapStr<40> hdr;
    if(enc.NumHubsPresent() == 0)
    {
        hdr.Append("NO HUB - check Qwiic");
    }
    else
    {
        hdr.Append("hub");
        hdr.AppendInt(g_shown_hub);
        hdr.Append(" 0x");
        AppendHex8(hdr, enc.HubAddress(g_shown_hub));
        hdr.Append(" fw");
        hdr.AppendInt(enc.Hub(g_shown_hub).FirmwareVersion());
        if(enc.NumHubsPresent() > 1)
        {
            hdr.Append(" +");
            hdr.AppendInt(enc.NumHubsPresent() - 1);
        }
    }
    display.SetCursor(2, 2);
    display.WriteString(hdr, Font_4x6, true);
    display.DrawLine(0, 10, w - 1, 10, true);

    // --- 3x3 grid of the shown hub's nine slots -----------------------------
    // Cell = slot number, '*' while held, then running position (clamped to a
    // readable 3 digits — this is a "does it count and which way" test, not a
    // precision readout).
    for(uint8_t s = 0; s < kSlots; ++s)
    {
        const int col = s % 3;
        const int row = s / 3;
        const int x   = 2 + col * 42;
        const int y   = 14 + row * 13;

        FixedCapStr<12> cell;
        cell.AppendInt(s + 1);
        cell.Append(enc.IsPressed(g_shown_hub, s) ? "*" : " ");

        int32_t pos = enc.GetPosition(g_shown_hub, s); // never read-clears
        if(pos > 999)
            pos = 999;
        if(pos < -999)
            pos = -999;
        cell.AppendInt(static_cast<int>(pos), true); // always show the sign

        display.SetCursor(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
        display.WriteString(cell, Font_6x8, true);
    }

    // --- footer: last event -------------------------------------------------
    display.DrawLine(0, 52, w - 1, 52, true);
    display.SetCursor(2, 55);
    display.WriteString(g_event, Font_6x8, true);

    display.Update(); // 8 pages, PumpEncoders() between each
}

int main(void)
{
    hw.Init();
    hw.StartLog(false);
    status.Init();

    // Encoders first: probing the hubs is the primary subject, and it also tells
    // the banner whether a dark OLED means "no bus" or "no pass-through".
    rodeo::RodeoEncoders::Config cfg; // all four addresses, 1 kHz/hub
    cfg.speed = kI2cSpeed;
    enc.Init(cfg);

    for(uint8_t h = 0; h < kHubs; ++h)
        if(enc.HubPresent(h))
        {
            g_shown_hub = h;
            break;
        }

    // Same bus, same speed. Transport defaults already target I2C1 on PB8/PB9
    // @ 0x3C — set explicitly so the wiring contract stays visible.
    OledType::Config ocfg;
    auto            &tcfg          = ocfg.driver_config.transport_config;
    tcfg.i2c_config.periph         = I2CHandle::Config::Peripheral::I2C_1;
    tcfg.i2c_config.pin_config.scl = seed::D11; // PB8 — board.h::pin::kI2cScl
    tcfg.i2c_config.pin_config.sda = seed::D12; // PB9 — board.h::pin::kI2cSda
    tcfg.i2c_config.speed          = kI2cSpeed;
    tcfg.i2c_address               = board::i2c::kOledAddr; // 0x3C
    display.Init(ocfg);
    display.Fill(false);
    display.Update();
    g_have_oled = rodeo::OledLinkOk(); // did 0x3C ACK during init + first frame?

    // Only now let frames yield to the hub poll.
    rodeo::SetOledYield(PumpEncoders);

    hw.PrintLine("=== Rodeo 07_encoder_scan (hubs 0x30-0x33 + OLED, I2C1) ===");
    for(uint8_t h = 0; h < kHubs; ++h)
    {
        if(enc.HubPresent(h))
            hw.PrintLine("  0x%02x: hub present  fw=%u caps=0x%02x",
                         enc.HubAddress(h),
                         enc.Hub(h).FirmwareVersion(),
                         enc.Hub(h).Capabilities());
        else
            hw.PrintLine("  0x%02x: no hub", enc.HubAddress(h));
    }
    hw.PrintLine("  0x%02x: OLED %s", board::i2c::kOledAddr,
                 g_have_oled ? "ACK" : "no ACK");

    // The pass-through triage table, spelled out so a dark screen is decodable
    // without a scope.
    if(enc.NumHubsPresent() == 0 && !g_have_oled)
        hw.PrintLine("  -> nothing on the bus. The hub is the Rev-A pull-up "
                     "anchor, so no hub = no I2C: check the Qwiic cable/power.");
    else if(enc.NumHubsPresent() > 0 && !g_have_oled)
        hw.PrintLine("  -> hub answers but the OLED does not: suspect the hub's "
                     "I2C PASS-THROUGH leg or the display module itself.");
    else if(enc.NumHubsPresent() == 0 && g_have_oled)
        hw.PrintLine("  -> OLED answers but no hub: bus pulls are coming from "
                     "somewhere else; check hub power / address straps.");
    else
        hw.PrintLine("  -> bus OK: hub + OLED both answer through the "
                     "pass-through. Turn a knob.");

    uint32_t last_view  = System::GetNow();
    uint32_t last_frame = System::GetNow();
    for(;;)
    {
        enc.Update(); // pump the round-robin poll as fast as the loop turns

        uint32_t now = System::GetNow();

        // Event-driven tap report: catch fast taps the instant they latch, so a
        // press+release between two views is never swallowed. Update() has
        // already folded the hub's latch into RAM, including from inside a
        // display frame, so nothing is lost while the screen is refreshing.
        for(uint8_t h = 0; h < kHubs; ++h)
        {
            if(!enc.HubPresent(h))
                continue;
            for(uint8_t s = 0; s < kSlots; ++s)
                if(enc.PopPress(h, s))
                {
                    hw.PrintLine("hub%u e%u  tap", h, s + 1);
                    if(h == g_shown_hub)
                        SetEvent(s, "TAP", 0, false);
                }
        }

        if(now - last_view >= 100) // ~10 Hz labeled live view
        {
            last_view = now;
            status.Heartbeat(100);

            for(uint8_t h = 0; h < kHubs; ++h)
            {
                if(!enc.HubPresent(h))
                    continue;
                for(uint8_t s = 0; s < kSlots; ++s)
                {
                    // GetDelta read-clears, so it is read ONCE here and shared
                    // with the OLED footer; the grid uses GetPosition, which
                    // never clears, so the two views cannot steal each other's
                    // detents.
                    int16_t d    = enc.GetDelta(h, s);
                    int32_t pos  = enc.GetPosition(h, s);
                    bool    held = enc.IsPressed(h, s);
                    if(d != 0 && h == g_shown_hub)
                        SetEvent(s, d > 0 ? "CW" : "CCW", d, true);
                    if(d == 0 && pos == 0 && !held)
                        continue; // idle encoder — stay quiet
                    hw.Print("hub%u e%u  ", h, s + 1);
                    hw.Print("pos=%s%d", pos >= 0 ? "+" : "", static_cast<int>(pos));
                    hw.Print("  d=%s%d", d >= 0 ? "+" : "", d);
                    hw.PrintLine(held ? "  [PRESS]" : "");
                }
            }
        }

        // ~6 Hz on a 100 kHz bus (a full frame is ~93 ms). If the display never
        // ACKed, retry slowly instead of burning the bus on every pass — that
        // also means plugging the Qwiic cable in mid-session lights it up.
        const uint32_t frame_ms = g_have_oled ? 160 : 2000;
        if(now - last_frame >= frame_ms)
        {
            last_frame = now;
            DrawScreen();
            g_have_oled = rodeo::OledLinkOk();
        }
    }
}
