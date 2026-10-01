#pragma once
//
// Rodeo OLED transport — bulk (one-transaction-per-page) SSD130x I2C transport.
//
// Drop-in replacement for daisy::SSD130xI2CTransport, which sends ONE 2-byte I2C
// transaction PER DISPLAY BYTE (libDaisy dev/oled_ssd130x.h). A 128x64 frame is
// 8 pages x 128 bytes, so a stock Update() is ~1024 blocking transactions — on
// the order of 100 ms at 400 kHz. That is fine for 08_oled_i2c, which does
// nothing else, but fatal for a synth: the same main loop pumps rodeo::RodeoAdc
// and MidiUartHandler, so every frame would freeze the knobs and delay note-on.
//
// Two changes:
//   1. SendData() pushes a whole page in ONE transaction: [0x40][page bytes].
//      ~4x fewer bus turnarounds -> ~23 ms/frame instead of ~100 ms.
//   2. A yield hook, invoked after each page. SSD130xDriver::Update() calls
//      SendData() once per page, so the hook gives the app 8 chances per frame
//      to pump the ADC scan / MIDI. Worst-case main-loop stall drops from a
//      whole frame to a single page (~3 ms).
//
// The hook is namespace-level rather than a member because the transport
// instance is unreachable from the app: OledDisplay::driver_ is PRIVATE and
// SSD130xDriver::transport_ is protected. gnu++14 (no inline variables), hence
// the .cpp.
//
// Usage — identical to the stock transport apart from the template argument:
//
//   using OledType
//       = OledDisplay<SSD130xDriver<128, 64, rodeo::Ssd130xI2cBulkTransport>>;
//   rodeo::SetOledYield(PumpIo);
//
#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"

namespace rodeo
{
class Ssd130xI2cBulkTransport
{
  public:
    // Same shape as the stock transport's, so app-side config code is unchanged.
    using Config = daisy::SSD130xI2CTransport::Config;

    void Init(const Config &config);
    void SendCommand(uint8_t cmd);
    void SendData(uint8_t *buff, size_t size);

  private:
    // One display page. Larger requests are chunked (and yield per chunk).
    static constexpr size_t kMaxChunk = 128;

    // The timeout MUST scale with bus speed. libDaisy hands this straight to
    // HAL_I2C_Master_Transmit, where it is a deadline for the WHOLE transfer,
    // not per byte. A page is [0x40][128 bytes] = 129 bytes; with the address
    // byte that is 1170 clocks:
    //
    //     100 kHz -> 11.7 ms/page   (full frame ~93 ms)
    //     400 kHz ->  2.9 ms/page   (full frame ~23 ms)
    //
    // A fixed 10 ms was sized for 400 kHz and is SHORTER than a single page at
    // 100 kHz, so every data page times out mid-transfer while the 2-byte
    // commands still get through — the panel powers on and then shows
    // uninitialized GDDRAM forever. Derive it instead, with 4x margin.
    //
    // Being generous costs nothing when no display is plugged in: an absent
    // device NAKs on the address byte and the HAL returns immediately rather
    // than burning the deadline.
    static constexpr uint32_t
    PageTimeoutMs(daisy::I2CHandle::Config::Speed speed)
    {
        // ceil(1170 clocks / speed) * 4, rounded up to whole ms.
        switch(speed)
        {
            case daisy::I2CHandle::Config::Speed::I2C_1MHZ: return 8;
            case daisy::I2CHandle::Config::Speed::I2C_400KHZ: return 12;
            default: return 48; // I2C_100KHZ
        }
    }

    daisy::I2CHandle i2c_;
    uint8_t          addr_       = 0;
    uint32_t         timeout_ms_ = PageTimeoutMs(
        daisy::I2CHandle::Config::Speed::I2C_100KHZ); // safest until Init()
    uint8_t tx_[1 + kMaxChunk];
};

// Called after each page is pushed. Must not touch the display, and must not
// block (no USB logging) — it runs in the middle of a frame.
void SetOledYield(void (*fn)());

// False once a transfer has been NAKed or timed out — i.e. no display is
// answering. Lets an app say so over serial instead of drawing into the void.
bool OledLinkOk();

} // namespace rodeo
