// The Rev B I2C bus (I2C1, 2.2 kOhm pull-ups): the OLED, the battery gauge, and
// the four tap connectors.
//
// 1. Scans every address and prints what answers, naming the ones Rodeo knows:
//
//      0x36 MAX17048 battery gauge   (needs the cell: it's powered from it)
//      0x3C SSD130x OLED
//      0x48 unknown                  (something on a tap)
//
// 2. Draws on a 128x64 SSD130x through the SDK's bulk transport: what was found,
//    the bus speed, frames per second, and a bar that sweeps every frame, so a
//    stuck picture is obvious.
// 3. Alternates 100 kHz and 400 kHz every 10 s. At each speed it also reads the
//    gauge's VERSION register 50 times a second and counts the reads that fail
//    or come back wrong:
//
//      400 kHz: 43.1 frames/s, gauge 500 reads, 0 bad, OLED ok
//
// 100 kHz is what the contract boots the bus at. 400 kHz is the fast mode the
// pull-ups were sized for with the default 150 mm tap cables; with long cables
// on the taps (the hardware notes estimate the limit around 4 x 500 mm) bad
// reads at 400 kHz and none at 100 kHz are the sign of too much capacitance.
// Plug the longest cable you ship into every tap for this.
//
// 'R' rescans; 'B' over the serial reboots into DFU.
#include <math.h>
#include <stdio.h>

#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"
#include "board.h"
#include "board_daisy.h"
#include "max17048.h"
#include "oled_ssd130x_bulk.h"
#include "dfu.h"

using namespace daisy;
using Speed = I2CHandle::Config::Speed;
using Oled  = OledDisplay<SSD130xDriver<128, 64, rodeo::Ssd130xI2cBulkTransport>>;

DaisySeed            hw;
I2CHandle            i2c;
Oled                 oled;
static volatile bool to_bootloader = false, rescan = false;

static uint8_t found[16];
static int     n_found = 0;

static void UsbRx(uint8_t* buf, uint32_t* len)
{
    for(uint32_t i = 0; i < *len; i++)
    {
        if(buf[i] == 'B')
            to_bootloader = true;
        if(buf[i] == 'R' || buf[i] == 'r')
            rescan = true;
    }
}

static const char* Name(uint8_t addr)
{
    switch(addr)
    {
        case board::i2c::kBatteryGaugeAddr: return "MAX17048 battery gauge";
        case board::i2c::kOledAddr: return "SSD130x OLED";
        case 0x3D: return "SSD130x OLED (alternate address)";
        default: return "unknown";
    }
}

static I2CHandle::Config BusConfig(Speed speed)
{
    I2CHandle::Config cfg;
    cfg.periph         = I2CHandle::Config::Peripheral::I2C_1;
    cfg.speed          = speed;
    cfg.mode           = I2CHandle::Config::Mode::I2C_MASTER;
    cfg.pin_config.scl = board::ToDaisy(board::pin::kI2cScl);
    cfg.pin_config.sda = board::ToDaisy(board::pin::kI2cSda);
    return cfg;
}

// Sets the bus speed for everything on I2C1: libDaisy keeps one handle per
// peripheral underneath, so the OLED's transport and ours share it.
static void SetSpeed(Speed speed)
{
    Oled::Config cfg;
    cfg.driver_config.transport_config.i2c_config  = BusConfig(speed);
    cfg.driver_config.transport_config.i2c_address = board::i2c::kOledAddr;
    oled.Init(cfg);
    i2c.Init(BusConfig(speed));
}

static void Scan()
{
    i2c.Init(BusConfig(Speed::I2C_100KHZ));
    n_found = 0;
    hw.PrintLine("I2C1 scan at 100 kHz:");
    for(uint8_t a = 0x08; a < 0x78; a++)
    {
        uint8_t none = 0;
        // A zero-length write: the address byte and a stop. A device ACKs its
        // address; an empty address NAKs and the HAL returns at once.
        if(i2c.TransmitBlocking(a, &none, 0, 2) == I2CHandle::Result::OK)
        {
            hw.PrintLine("  0x%02x %s", a, Name(a));
            if(n_found < 16)
                found[n_found++] = a;
        }
    }
    if(n_found == 0)
        hw.PrintLine("  nothing answered: check the pull-ups and 3V3_D");
    bool gauge = false, oled_seen = false;
    for(int i = 0; i < n_found; i++)
    {
        gauge     = gauge || found[i] == board::i2c::kBatteryGaugeAddr;
        oled_seen = oled_seen || found[i] == board::i2c::kOledAddr;
    }
    if(!gauge)
        hw.PrintLine("  no gauge at 0x36: is a cell connected?");
    if(!oled_seen)
        hw.PrintLine("  no OLED at 0x3C");
}

// One gauge VERSION read: true if the bus carried it intact.
static bool GaugeRead()
{
    uint8_t b[2];
    if(i2c.ReadDataAtAddress(board::i2c::kBatteryGaugeAddr, rodeo::max17048::kVersion, 1, b,
                             2, 5)
       != I2CHandle::Result::OK)
        return false;
    return rodeo::max17048::VersionOk(rodeo::max17048::Word(b));
}

static void Draw(int khz, int fps_x10, uint32_t frame)
{
    char line[24];
    oled.Fill(false);
    oled.SetCursor(0, 0);
    oled.WriteString("Rodeo I2C", Font_7x10, true);
    snprintf(line, sizeof(line), "%d kHz  %d.%d fps", khz, fps_x10 / 10, fps_x10 % 10);
    oled.SetCursor(0, 14);
    oled.WriteString(line, Font_6x8, true);
    for(int i = 0; i < n_found && i < 6; i++)
    {
        snprintf(line, sizeof(line), "%02x", found[i]);
        oled.SetCursor((i % 6) * 21, 26);
        oled.WriteString(line, Font_6x8, true);
    }
    oled.DrawRect(0, 40, 127, 52, true, false);
    const int x = static_cast<int>(frame % 120);
    oled.DrawRect(2 + x, 42, 2 + x + 5, 50, true, true);
    oled.SetCursor(0, 56);
    oled.WriteString(rodeo::OledLinkOk() ? "link ok" : "link FAILED", Font_6x8, true);
    oled.Update();
}

int main(void)
{
    hw.Init();
    hw.StartLog(true); // wait for a terminal, so the scan is seen
    hw.usb_handle.SetReceiveCallback(UsbRx, UsbHandle::FS_INTERNAL);

    Scan();

    bool     fast        = false;
    uint32_t phase_start = System::GetNow(), frames = 0, frame = 0, reads = 0, bad = 0;
    uint32_t last_read = 0;
    int      fps_x10   = 0;
    SetSpeed(Speed::I2C_100KHZ);
    for(;;)
    {
        if(to_bootloader)
            ResetToDfu();
        if(rescan)
        {
            rescan = false;
            Scan();
            SetSpeed(fast ? Speed::I2C_400KHZ : Speed::I2C_100KHZ);
        }

        Draw(fast ? 400 : 100, fps_x10, frame++);
        frames++;

        uint32_t now = System::GetNow();
        if(now - last_read >= 20)
        {
            last_read = now;
            reads++;
            bad += !GaugeRead();
        }
        const uint32_t elapsed = now - phase_start;
        if(elapsed >= 1000 && frames % 10 == 0)
            fps_x10 = static_cast<int>(frames * 10000u / elapsed);
        if(elapsed >= 10000)
        {
            hw.PrintLine("%d kHz: %d.%d frames/s, gauge %lu reads, %lu bad, OLED %s",
                         fast ? 400 : 100, fps_x10 / 10, fps_x10 % 10, (unsigned long)reads,
                         (unsigned long)bad, rodeo::OledLinkOk() ? "ok" : "not answering");
            fast = !fast;
            SetSpeed(fast ? Speed::I2C_400KHZ : Speed::I2C_100KHZ);
            phase_start = System::GetNow();
            frames = reads = bad = 0;
        }
        hw.SetLed(bad == 0 && (frame & 8));
    }
}
