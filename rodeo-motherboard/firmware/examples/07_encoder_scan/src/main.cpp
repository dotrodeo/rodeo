// Rodeo bring-up 07 — encoder-hub scan (shared-I2C rodeo-encoder-hub boards).
//
// Brings up the Layer 2 rodeo::RodeoEncoders scanner: enumerates the hubs at
// 0x30..0x33 on I2C1 (the OLED+hub bus, SCL D11 / SDA D12), round-robin polls
// each hub's hot telemetry block, and streams a human-readable view over USB
// serial using the friendly relative-encoder API.
//
//   Boot: prints which of 0x30..0x33 answered WHO_AM_I=0x9E (a hub) with a
//         matching MAP_VERSION, plus each present hub's FW version + caps.
//   Live view (~10 Hz): for every present hub, one row per encoder that MOVED
//         or is HELD since boot —
//         hub0 e3  pos=+14  d=+2  [PRESS]      (encoder 3 on 0x30)
//         Idle encoders stay quiet so a turned/pressed knob auto-surfaces.
//   Tap latch (event): a press edge shorter than a poll interval still prints,
//         proving PopPress() catches taps via the hub latch.
//
//   PASS: turn any encoder -> its pos tracks and d shows the detents since the
//         last view; press -> [PRESS] appears and a tap logs a "tap" edge even
//         if released fast; an absent hub's address prints "no hub" at boot and
//         its rows never appear.
//
// Bench harness for the multi-hub enumerate + poll validation (needs a fabbed
// hub on the Qwiic bus). See rodeo-001/docs/RODEO_HUB_I2C_CONTRACT.md.

#include "daisy_seed.h"
#include "board.h"
#include "rodeo_encoders.h"
#include "status_led.h"

using namespace daisy;

DaisySeed            hw;
rodeo::RodeoEncoders enc;
rodeo::StatusLed     status;

static constexpr uint8_t kHubs = rodeo::RodeoEncoders::kMaxHubs;
static constexpr uint8_t kSlots = rodeo::RodeoEncoders::kEncPerHub;

// Print a signed integer field "+N"/"-N" (Daisy logger %d handles the sign).
static void PrintSigned(const char *label, int v)
{
    hw.Print("%s%s%d", label, v >= 0 ? "+" : "", v);
}

int main(void)
{
    hw.Init();
    hw.StartLog(false);
    status.Init();

    rodeo::RodeoEncoders::Config cfg; // all four addresses, 100 kHz, 1 kHz/hub
    enc.Init(cfg);

    hw.PrintLine("=== Rodeo 07_encoder_scan (hubs 0x30-0x33, I2C1) ===");
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
    if(enc.NumHubsPresent() == 0)
        hw.PrintLine("  (no hubs answered — check the Qwiic cable + pull-ups)");

    uint32_t last_view = System::GetNow();
    for(;;)
    {
        enc.Update(); // pump the round-robin poll as fast as the loop turns

        uint32_t now = System::GetNow();

        // Event-driven tap report: catch fast taps the instant they latch, so a
        // press+release between two ~10 Hz views is never swallowed by the view.
        for(uint8_t h = 0; h < kHubs; ++h)
        {
            if(!enc.HubPresent(h))
                continue;
            for(uint8_t s = 0; s < kSlots; ++s)
                if(enc.PopPress(h, s))
                    hw.PrintLine("hub%u e%u  tap", h, s + 1);
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
                    int16_t d      = enc.GetDelta(h, s); // read-clears
                    int32_t pos    = enc.GetPosition(h, s);
                    bool    held   = enc.IsPressed(h, s);
                    if(d == 0 && pos == 0 && !held)
                        continue; // idle encoder — stay quiet
                    hw.Print("hub%u e%u  ", h, s + 1);
                    PrintSigned("pos=", static_cast<int>(pos));
                    PrintSigned("  d=", d);
                    hw.PrintLine(held ? "  [PRESS]" : "");
                }
            }
        }
    }
}
