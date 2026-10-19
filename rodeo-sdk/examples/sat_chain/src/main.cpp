// The satellite chain driver on a bare Daisy Seed.
//
// Wire the Seed's pin 3 (PC10, chain TX) to pin 2 (PC11, chain RX) and it is a
// ring with no satellites: DISCOVER comes back empty and every SCAN comes back
// whole. Pull the wire and the ring is lost: the driver rediscovers, looks for
// the break, and finds it before the first satellite. Put it back and the next
// retry restores it. With real satellites on the Rev B motherboard the same
// program lists them and their controls.
//
// Status goes to the Seed's USB serial port every half second; open it with any
// terminal (screen /dev/cu.usbmodem* 115200). The LED blinks slowly while the
// ring answers and fast while it doesn't. 'B' on the serial port reboots into
// DFU for the next flash.
#include "daisy_seed.h"
#include "sat_chain.h"
#include "sat_chain_port.h"

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
        if(to_bootloader)
            System::ResetToBootloader(System::BootloaderMode::STM);

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

        if(chain.Changed())
            hw.PrintLine("-- chain changed");
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
            hw.PrintLine("  %d: kind %d, %d slot(s), firmware %d.%d", i + 1, d.kind, d.slots,
                         d.fw_major, d.fw_minor);
        }
    }
}
