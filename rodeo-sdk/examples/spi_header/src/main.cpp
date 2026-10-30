// The Rev B SPI expansion header (SPI1, the 7-pin JST SH), as a loop-back.
//
// Join the header's MOSI (pin 3) to MISO (pin 4) with a wire, and every byte
// sent comes straight back. On start, and on 'R' over the Seed's USB serial:
//
//   1. CS (pin 6) is read as an input before SPI touches it: the board's 10 kOhm
//      pull-up should hold it high, so a device on the header stays deselected
//      while the Seed boots.
//   2. 1 KiB of numbered bytes goes out and back at each clock divider from /256
//      down to /4, checked byte for byte and timed. The rate shows roughly what
//      clock each divider gives; the fastest divider that passes is what the
//      header carries with that wire on it. MOSI and SCK have 33 Ohm at the
//      Seed, MISO none, so the loop's own wire is the weak point at the top.
//   3. With the wire pulled, it repeats step 2 expecting nothing back: MISO
//      floating reads as noise or a constant, never the pattern, which proves
//      the pass was the wire and not a stuck line.
//
// Anything plugged into the header (the ADC expander, a display) must come off
// for this. 'B' reboots into DFU.
#include <string.h>

#include "daisy_seed.h"
#include "board.h"
#include "board_daisy.h"
#include "dfu.h"

using namespace daisy;
using Ps = SpiHandle::Config::BaudPrescaler;

constexpr size_t kBytes = 1024;

DaisySeed            hw;
SpiHandle            spi;
GPIO                 cs;
static uint8_t       tx[kBytes], rx[kBytes];
static volatile bool to_bootloader = false, run_again = false;

static void UsbRx(uint8_t* b, uint32_t* len)
{
    for(uint32_t i = 0; i < *len; i++)
    {
        if(b[i] == 'B')
            to_bootloader = true;
        if(b[i] == 'R' || b[i] == 'r')
            run_again = true;
    }
}

static bool Loop(Ps ps, int divider, bool expect_back)
{
    SpiHandle::Config cfg;
    cfg.periph          = SpiHandle::Config::Peripheral::SPI_1;
    cfg.mode            = SpiHandle::Config::Mode::MASTER;
    cfg.direction       = SpiHandle::Config::Direction::TWO_LINES;
    cfg.nss             = SpiHandle::Config::NSS::SOFT;
    cfg.baud_prescaler  = ps;
    cfg.pin_config.sclk = board::ToDaisy(board::pin::kSpiSck);
    cfg.pin_config.miso = board::ToDaisy(board::pin::kSpiMiso);
    cfg.pin_config.mosi = board::ToDaisy(board::pin::kSpiMosi);
    cfg.pin_config.nss  = Pin(); // CS is ours, below
    spi.Init(cfg);

    memset(rx, 0, sizeof(rx));
    cs.Write(false);
    const uint32_t t0 = System::GetUs();
    const bool     ok = spi.BlockingTransmitAndReceive(tx, rx, kBytes, 100)
                    == SpiHandle::Result::OK;
    const uint32_t us = System::GetUs() - t0;
    cs.Write(true);

    int wrong = 0, first = -1;
    for(size_t i = 0; i < kBytes; i++)
        if(rx[i] != tx[i] && wrong++ == 0)
            first = static_cast<int>(i);
    const bool pass = ok && (expect_back ? wrong == 0 : wrong > static_cast<int>(kBytes / 2));
    const unsigned kbps = us ? static_cast<unsigned>(kBytes * 8u * 1000u / us) : 0;
    if(!ok)
        hw.PrintLine("  /%-3d transfer failed", divider);
    else if(expect_back)
        hw.PrintLine("  /%-3d %6u kbit/s  %s", divider, kbps,
                     wrong ? "" : "ok");
    else
        hw.PrintLine("  /%-3d %d of %d bytes differ  %s", divider, wrong, (int)kBytes,
                     pass ? "ok (nothing looped back)" : "PATTERN CAME BACK");
    if(ok && expect_back && wrong)
        hw.PrintLine("        %d bytes wrong, first at %d (sent 0x%02x, got 0x%02x)", wrong,
                     first, tx[first], rx[first]);
    return pass;
}

static const struct
{
    Ps  ps;
    int div;
} kSpeeds[] = {{Ps::PS_256, 256}, {Ps::PS_64, 64}, {Ps::PS_16, 16}, {Ps::PS_8, 8},
               {Ps::PS_4, 4}};

static void Run()
{
    // 1. CS as an input first, with nothing driving it but the pull-up.
    GPIO cs_in;
    cs_in.Init(board::ToDaisy(board::pin::kSpiExtCs), GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
    System::Delay(1);
    const bool cs_high = cs_in.Read();
    hw.PrintLine("spi_header: CS idles %s", cs_high ? "high (pull-up ok)" : "LOW: no pull-up?");
    cs.Init(board::ToDaisy(board::pin::kSpiExtCs), GPIO::Mode::OUTPUT);
    cs.Write(true);

    for(size_t i = 0; i < kBytes; i++)
        tx[i] = static_cast<uint8_t>(i * 7 + (i >> 8)); // every value, no long runs

    // 2. Loop-back at each divider.
    hw.PrintLine("MOSI (pin 3) to MISO (pin 4), 1 KiB each way:");
    int fastest = 0;
    for(auto& s : kSpeeds)
        if(Loop(s.ps, s.div, true))
            fastest = s.div;
    if(fastest)
        hw.PrintLine("fastest passing divider /%d. Now pull the wire and press R for the "
                     "no-wire check.",
                     fastest);
    else
    {
        // 3. Nothing came back at any speed: either no wire, or a broken line.
        hw.PrintLine("nothing looped back. Without the wire that's right; checking:");
        bool none = true;
        for(auto& s : kSpeeds)
            none = Loop(s.ps, s.div, false) && none;
        hw.PrintLine(none ? "no-wire check PASS" : "no-wire check FAIL");
    }
    hw.PrintLine(cs_high && fastest ? "PASS" : (fastest ? "FAIL (CS)" : ""));
}

int main(void)
{
    hw.Init();
    hw.StartLog(true); // wait for a terminal, so the result is seen
    hw.usb_handle.SetReceiveCallback(UsbRx, UsbHandle::FS_INTERNAL);

    Run();
    for(;;)
    {
        if(run_again)
        {
            run_again = false;
            hw.PrintLine("");
            Run();
        }
        if(to_bootloader)
            ResetToDfu();
    }
}
