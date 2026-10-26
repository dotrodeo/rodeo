#include "battery.h"

#include "board.h"
#include "board_daisy.h"

using namespace daisy;

namespace rodeo
{
namespace
{
constexpr uint32_t kTimeoutMs = 5;
}

bool Battery::Init()
{
    I2CHandle::Config cfg;
    cfg.periph         = I2CHandle::Config::Peripheral::I2C_1;
    cfg.speed          = I2CHandle::Config::Speed::I2C_100KHZ; // the contract's boot speed
    cfg.mode           = I2CHandle::Config::Mode::I2C_MASTER;
    cfg.pin_config.scl = board::ToDaisy(board::pin::kI2cScl);
    cfg.pin_config.sda = board::ToDaisy(board::pin::kI2cSda);
    if(i2c_.Init(cfg) != I2CHandle::Result::OK)
        return false;
    return ReadWord(max17048::kVersion, &version_) && max17048::VersionOk(version_);
}

bool Battery::Read(max17048::Reading* r)
{
    uint16_t vcell, soc, crate;
    if(!ReadWord(max17048::kVcell, &vcell) || !ReadWord(max17048::kSoc, &soc)
       || !ReadWord(max17048::kCrate, &crate))
        return false;
    *r = max17048::Decode(vcell, soc, crate);
    return true;
}

bool Battery::Status(uint16_t* status)
{
    if(!ReadWord(max17048::kStatus, status))
        return false;
    if(*status & max17048::kStatusResetIndicator)
        WriteWord(max17048::kStatus, *status & ~max17048::kStatusResetIndicator);
    return true;
}

bool Battery::ReadWord(uint8_t reg, uint16_t* value)
{
    uint8_t b[2];
    if(i2c_.ReadDataAtAddress(board::i2c::kBatteryGaugeAddr, reg, 1, b, 2, kTimeoutMs)
       != I2CHandle::Result::OK)
        return false;
    *value = max17048::Word(b);
    return true;
}

bool Battery::WriteWord(uint8_t reg, uint16_t value)
{
    uint8_t b[2] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    return i2c_.WriteDataAtAddress(board::i2c::kBatteryGaugeAddr, reg, 1, b, 2, kTimeoutMs)
           == I2CHandle::Result::OK;
}
} // namespace rodeo
