// The satellite chain driver on a bare Daisy Seed.
//
// Wire the Seed's pin 3 (PC10, chain TX) to pin 2 (PC11, chain RX) and it is a
// ring with no satellites: DISCOVER comes back empty and every SCAN comes back
// whole. Pull the wire and the ring is lost: the driver rediscovers, looks for
// the break, and finds it before the first satellite. Put it back and the next
// retry restores it.
//
// On the Rev B motherboard with satellites, it is their bring-up test too. It
// lists each one by kind with its controls' values, and drives its lights from
// them, so every control and every LED can be checked by hand:
//
//   * single pot, pot bank: the LED's brightness follows the first pot;
//   * single encoder, encoder bank: the LED's brightness follows the first
//     encoder (turn it), and a push toggles slow blinking;
//   * button bank: each button's RGB LED is white while held and steps through
//     red, green, blue and off with each press.
//
// A satellite reporting a fault (a missing ID resistor, a misread kind) shows
// its fault code in the listing.
//
// Status goes to the Seed's USB serial port every half second; open it with any
// terminal (screen /dev/cu.usbmodem* 115200). The LED blinks slowly while the
// ring answers and fast while it doesn't. 'B' on the serial port reboots into
// DFU for the next flash.
#include <stdio.h>
#include <string.h>

#include "daisy_seed.h"
#include "sat_chain.h"
#include "sat_chain_port.h"
#include "dfu.h"

using namespace daisy;

DaisySeed           hw;
static volatile bool to_bootloader = false;

// 'B' on the serial port reboots into the STM32's DFU bootloader, so the next
// build flashes without the buttons (development only).
static void UsbRx(uint8_t* buf, uint32_t* len)
{
    for(uint32_t i = 0; i < *len; i++)
        if(buf[i] == 'B')
            to_bootloader = true;
}
rodeo::SatChain     chain;
rodeo::SatChainPort port;

static const char* KindName(uint8_t kind)
{
    switch(kind)
    {
        case SAT_KIND_TEST_FIXTURE: return "test fixture";
        case SAT_KIND_POT: return "single pot";
        case SAT_KIND_BUTTON: return "single button";
        case SAT_KIND_ENCODER: return "single encoder";
        case SAT_KIND_SWITCH: return "switch";
        case SAT_KIND_POT_BANK_4: return "pot bank x4";
        case SAT_KIND_ENC_BANK_4: return "encoder bank x4";
        case SAT_KIND_BUTTON_BANK_4: return "button bank x4";
        case SAT_KIND_CUSTOM: return "custom board";
        case SAT_KIND_NO_ID: return "no ID resistor";
        default: return "unknown kind";
    }
}

// What each satellite's controls have done, kept between scans so the lights
// and the listing show totals rather than the last scan's deltas.
constexpr int kMax = rodeo::SatChain::kMaxSatellites;
static int     enc_total[kMax][4], presses[kMax][4];
static uint8_t colour[kMax][4];
static bool    blink[kMax];

static void Collect()
{
    for(int i = 0; i < chain.Satellites(); i++)
    {
        const sat_descriptor& d = chain.Satellite(i);
        for(int s = 0; s < d.slots && s < 4; s++)
        {
            switch(sat_kind_input(d.kind))
            {
                case SAT_IN_ENCODER:
                {
                    enc_total[i][s] += chain.EncoderSteps(i, s);
                    const int p = chain.Presses(i, s);
                    presses[i][s] += p;
                    if(s == 0 && p % 2)
                        blink[i] = !blink[i];
                    break;
                }
                case SAT_IN_BUTTON:
                {
                    const int p = chain.Presses(i, s);
                    presses[i][s] += p;
                    colour[i][s] = (colour[i][s] + p) % 4;
                    break;
                }
                default: break;
            }
        }
    }
}

static void Drive()
{
    static const uint8_t kRgb[4][3] = {{40, 0, 0}, {0, 40, 0}, {0, 0, 40}, {0, 0, 0}};
    for(int i = 0; i < chain.Satellites(); i++)
    {
        const sat_descriptor& d = chain.Satellite(i);
        switch(sat_kind_input(d.kind))
        {
            case SAT_IN_POT:
                chain.SetLed(i, static_cast<uint8_t>(chain.Pot(i, 0) * 255.f));
                break;
            case SAT_IN_ENCODER:
            {
                int b = enc_total[i][0] * 8;
                b     = b < 0 ? 0 : (b > 255 ? 255 : b);
                chain.SetLed(i, static_cast<uint8_t>(b),
                             blink[i] ? SAT_LED_SLOW_BLINK : SAT_LED_STEADY);
                break;
            }
            case SAT_IN_BUTTON:
                if(d.kind != SAT_KIND_BUTTON_BANK_4)
                {
                    chain.SetLed(i, chain.Pressed(i, 0) ? 255 : 0);
                    break;
                }
                for(int s = 0; s < d.slots && s < 4; s++)
                {
                    if(chain.Pressed(i, s))
                        chain.SetRgb(i, s, 40, 40, 40);
                    else
                        chain.SetRgb(i, s, kRgb[colour[i][s]][0], kRgb[colour[i][s]][1],
                                     kRgb[colour[i][s]][2]);
                }
                break;
            default: break;
        }
    }
}

// One satellite's slots on one line, e.g. "pot 0.512 | pot 0.000".
static void PrintSlots(int i)
{
    const sat_descriptor& d = chain.Satellite(i);
    char                  line[160];
    int                   n = 0;
    for(int s = 0; s < d.slots && s < 4 && n < (int)sizeof(line) - 40; s++)
    {
        uint8_t code, detail;
        if(s)
            n += snprintf(line + n, sizeof(line) - n, " | ");
        if(chain.Fault(i, s, &code, &detail))
        {
            n += snprintf(line + n, sizeof(line) - n, "FAULT %u/%u", code, detail);
            continue;
        }
        switch(sat_kind_input(d.kind))
        {
            case SAT_IN_POT:
                n += snprintf(line + n, sizeof(line) - n, "pot %4d/1000",
                              static_cast<int>(chain.Pot(i, s) * 1000.f + 0.5f));
                break;
            case SAT_IN_ENCODER:
                n += snprintf(line + n, sizeof(line) - n, "enc %+d%s x%d", enc_total[i][s],
                              chain.Pressed(i, s) ? " held" : "", presses[i][s]);
                break;
            case SAT_IN_BUTTON:
                n += snprintf(line + n, sizeof(line) - n, "%s x%d",
                              chain.Pressed(i, s) ? "held" : "up", presses[i][s]);
                break;
            case SAT_IN_SWITCH:
                n += snprintf(line + n, sizeof(line) - n, "switch %d",
                              chain.SwitchPosition(i, s));
                break;
            default: n += snprintf(line + n, sizeof(line) - n, "-"); break;
        }
    }
    hw.PrintLine("     %s", n ? line : "(no slots)");
}

static const char* StateName(rodeo::SatChain::State s)
{
    switch(s)
    {
        case rodeo::SatChain::State::kPowerWait: return "power-up wait";
        case rodeo::SatChain::State::kDiscover: return "discovering";
        case rodeo::SatChain::State::kScan: return "scanning";
        case rodeo::SatChain::State::kFindBreak: return "finding the break";
        case rodeo::SatChain::State::kPartial: return "broken, scanning up to it";
        case rodeo::SatChain::State::kVersion: return "firmware version mismatch";
    }
    return "?";
}

int main(void)
{
    hw.Init();
    hw.StartLog(false);
    hw.usb_handle.SetReceiveCallback(UsbRx, UsbHandle::FS_INTERNAL);

    chain.Init(rodeo::SatChain::Config(), System::GetUs());
    port.Init();

    uint32_t last_print = 0, last_blink = 0;
    bool     led = false;
    for(;;)
    {
        port.Process(chain);
        if(chain.Changed())
        {
            hw.PrintLine("-- chain changed");
            memset(enc_total, 0, sizeof(enc_total));
            memset(presses, 0, sizeof(presses));
            memset(colour, 0, sizeof(colour));
            memset(blink, 0, sizeof(blink));
        }
        Collect();
        Drive();
        if(to_bootloader)
            ResetToDfu();

        uint32_t now = System::GetNow();
        bool     ok  = chain.GetState() == rodeo::SatChain::State::kScan;
        if(now - last_blink >= (ok ? 500u : 100u))
        {
            last_blink = now;
            led        = !led;
            hw.SetLed(led);
        }
        if(now - last_print < 500)
            continue;
        last_print = now;

        hw.PrintLine("%s: %d satellites, %lu good scans, %lu bad%s", StateName(chain.GetState()),
                     chain.Satellites(), (unsigned long)chain.GoodScans(),
                     (unsigned long)chain.BadScans(),
                     chain.BrokenAfter() >= 0 ? " (broken)" : "");
        hw.PrintLine("  %lu exchanges; last reply %u bytes, starting %02x %02x %02x %02x",
                     (unsigned long)port.exchanges, port.last_len, port.last_head[0],
                     port.last_head[1], port.last_head[2], port.last_head[3]);
        for(int i = 0; i < chain.Satellites(); i++)
        {
            const sat_descriptor& d = chain.Satellite(i);
            hw.PrintLine("  %d: %s (kind %d), %d slot(s), firmware %d.%d", i + 1,
                         KindName(d.kind), d.kind, d.slots, d.fw_major, d.fw_minor);
            PrintSlots(i);
        }
    }
}
