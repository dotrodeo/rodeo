// Rodeo bring-up 08 — OLED (I2C).
//
// Smoke test for the SSD1309 128x64 feedback display on the Qwiic/J42 bus.
// The board carries the display on I2C1 @ 0x3C, sharing SCL=Seed pin 12 (PB8)
// and SDA=Seed pin 13 (PB9) with the encoder hub — the ONLY SPI bus (SPI1) is
// fully owned by the two ADS114S08 ADCs, so the OLED lives on I2C (Decision A,
// 2026-08-15; see RODEO_CONTROL_MAP.md "Display" row).
//
//   PASS: the OLED shows a bordered "RODEO 001 / OLED I2C 0x3C" header, a live
//         uptime-seconds counter that ticks once/sec, and a sweep bar that
//         crawls left->right and wraps — proving the frame buffer is being
//         pushed continuously. USB serial logs one line per refresh.
//
//   FAIL modes:
//     - Blank / garbage: check the module has its own I2C pull-ups. The Rodeo
//       board has NONE on-board (Issue 2). No pull-ups => no ACK => blank.
//     - Wrong address: some SSD1309 breakouts strap 0x3D. Change kOledAddr.
//     - Flicker/tearing only at 1 MHz: drop kI2cSpeed to 400 kHz (default here).
//
// Flash: put the Daisy in DFU (hold BOOT, tap RESET), then `make program-dfu`.

#include "daisy_seed.h"
#include "dev/oled_ssd130x.h" // OLED driver (not pulled in by daisy_seed.h)
#include "board.h"

using namespace daisy;

// 128x64 SSD1306/SSD1309 over I2C. The transport defaults already target
// I2C_1 on PB8/PB9 @ 0x3C — i.e. exactly the Rodeo J42 bus — but we set them
// explicitly below so the wiring contract is visible in the example.
using OledType = OledDisplay<SSD130xI2c128x64Driver>;

DaisySeed hw;
OledType  display;

int main(void)
{
    hw.Init();
    hw.StartLog(false); // non-blocking: runs with or without a serial monitor

    OledType::Config cfg;
    auto& tcfg = cfg.driver_config.transport_config;
    tcfg.i2c_config.periph         = I2CHandle::Config::Peripheral::I2C_1;
    tcfg.i2c_config.pin_config.scl = seed::D11; // PB8 — board.h::pin::kI2cScl
    tcfg.i2c_config.pin_config.sda = seed::D12; // PB9 — board.h::pin::kI2cSda
    // 400 kHz: safe on a bus with no on-board pull-ups. Full 128x64 frame ~23 ms
    // (~30 fps ceiling). Bump to I2C_1MHZ once pull-ups are sized (Bumble).
    tcfg.i2c_config.speed          = I2CHandle::Config::Speed::I2C_400KHZ;
    tcfg.i2c_address               = board::i2c::kOledAddr; // 0x3C

    display.Init(cfg);
    display.Fill(false);
    display.Update();

    hw.PrintLine("=== Rodeo 08_oled_i2c ===");
    hw.PrintLine("SSD1309 128x64 @ I2C1 0x3C (SCL=pin12/PB8, SDA=pin13/PB9)");

    const uint16_t w = display.Width();  // 128
    const uint16_t h = display.Height(); //  64

    uint32_t frame = 0;
    for(;;)
    {
        const uint32_t secs   = System::GetNow() / 1000u;
        const uint8_t  sweepX = frame % w; // crawls 0..127, wraps

        display.Fill(false);

        // Outer frame so a live edge is always visible.
        display.DrawRect(0, 0, w - 1, h - 1, true);

        display.SetCursor(6, 4);
        display.WriteString("RODEO 001", Font_7x10, true);
        display.SetCursor(6, 18);
        display.WriteString("OLED I2C 0x3C", Font_6x8, true);

        // Uptime seconds, large.
        FixedCapStr<16> line("up ");
        line.AppendInt(secs);
        line.Append("s");
        display.SetCursor(6, 32);
        display.WriteString(line, Font_11x18, true);

        // Sweep bar along the bottom interior — obvious proof of refresh.
        display.DrawLine(sweepX, h - 4, sweepX, h - 2, true);

        display.Update();

        hw.PrintLine("frame %lu  up %lus  sweepX %u", frame++, secs, sweepX);
        System::Delay(100); // ~10 Hz refresh
    }
}
