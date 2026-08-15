// Rodeo ADC — Layer 1 device driver for the TI ADS114S08. See ads114s08.h.

#include "ads114s08.h"

using namespace daisy;

namespace rodeo
{
SpiHandle::Config Ads114s08::DefaultSpiConfig()
{
    SpiHandle::Config cfg;
    cfg.periph         = SpiHandle::Config::Peripheral::SPI_1;
    cfg.mode           = SpiHandle::Config::Mode::MASTER;
    cfg.direction      = SpiHandle::Config::Direction::TWO_LINES;
    cfg.nss            = SpiHandle::Config::NSS::SOFT;
    cfg.clock_polarity = SpiHandle::Config::ClockPolarity::LOW;      // CPOL=0
    cfg.clock_phase    = SpiHandle::Config::ClockPhase::TWO_EDGE;    // CPHA=1 (mode 1)
    cfg.baud_prescaler = SpiHandle::Config::BaudPrescaler::PS_32;
    cfg.pin_config.sclk = seed::D8;  // SCK
    cfg.pin_config.miso = seed::D9;  // <- ADC DOUT
    cfg.pin_config.mosi = seed::D10; // -> ADC DIN
    return cfg;
}

void Ads114s08::InitCsIdle(Pin cs_pin)
{
    cs_.Init(cs_pin, GPIO::Mode::OUTPUT);
    CsHigh(); // deselected — no chip can be probed while a sibling's CS floats
}

bool Ads114s08::Init(SpiHandle *spi, Pin cs_pin)
{
    spi_ = spi;
    InitCsIdle(cs_pin); // idempotent if RodeoAdc already parked it

    System::Delay(10); // power-up settle
    Reset();
    System::Delay(5); // device reset recovery

    // Presence check: write a distinctive pattern to INPMUX and read it back.
    // A live, correctly-wired chip echoes it; a dead/mis-wired one won't.
    // (0x5C = MUXP=AIN5, MUXN=AINCOM — a valid, writable INPMUX value.)
    WriteReg(kRegInpmux, 0x5C);
    present_ = (ReadReg(kRegInpmux) == 0x5C);
    if(!present_)
        return false;

    // Apply Rodeo board defaults (Reset restored them to POR values).
    WriteReg(kRegRef, kRefExternal3v3);
    WriteReg(kRegPga, kPgaBypass);
    WriteReg(kRegDatarate, kDatarate1000Ll);

    SelectChannel(0); // default to AIN0
    Start();          // begin continuous conversions
    return true;
}

void Ads114s08::Reset()
{
    uint8_t cmd = kCmdReset;
    CsLow();
    spi_->BlockingTransmit(&cmd, 1);
    CsHigh();
}

void Ads114s08::Start()
{
    uint8_t cmd = kCmdStart;
    CsLow();
    spi_->BlockingTransmit(&cmd, 1);
    CsHigh();
}

void Ads114s08::Stop()
{
    uint8_t cmd = kCmdStop;
    CsLow();
    spi_->BlockingTransmit(&cmd, 1);
    CsHigh();
}

void Ads114s08::WriteReg(uint8_t addr, uint8_t val)
{
    // WREG frame: (0x40|addr), (num regs - 1), data...
    uint8_t tx[3] = {static_cast<uint8_t>(kCmdWreg | addr), 0x00, val};
    CsLow();
    spi_->BlockingTransmit(tx, 3);
    CsHigh();
}

uint8_t Ads114s08::ReadReg(uint8_t addr)
{
    // RREG frame: (0x20|addr), (num regs - 1), then device returns the data.
    uint8_t tx[3] = {static_cast<uint8_t>(kCmdRreg | addr), 0x00, 0x00};
    uint8_t rx[3] = {0, 0, 0};
    CsLow();
    spi_->BlockingTransmitAndReceive(tx, rx, 3);
    CsHigh();
    return rx[2];
}

void Ads114s08::SelectChannel(uint8_t ain)
{
    // INPMUX: MUXP[7:4] = AINx, MUXN[3:0] = AINCOM -> single-ended AINx.
    uint8_t inpmux = static_cast<uint8_t>((ain << 4) | kMuxnAincom);
    WriteReg(kRegInpmux, inpmux); // auto-restarts the filter / conversion
}

int16_t Ads114s08::ReadData()
{
    uint8_t tx[3] = {kCmdRdata, 0x00, 0x00};
    uint8_t rx[3] = {0, 0, 0};
    CsLow();
    spi_->BlockingTransmitAndReceive(tx, rx, 3);
    CsHigh();
    return static_cast<int16_t>((rx[1] << 8) | rx[2]); // 16-bit signed
}

} // namespace rodeo
