// Rodeo Encoder Hub — Layer 1 device driver. See encoder_hub.h.

#include "encoder_hub.h"
#include "board.h"

using namespace daisy;

namespace rodeo
{
I2CHandle::Config EncoderHub::DefaultI2cConfig(I2CHandle::Config::Speed speed)
{
    I2CHandle::Config c;
    c.periph = I2CHandle::Config::Peripheral::I2C_1;
    c.mode   = I2CHandle::Config::Mode::I2C_MASTER;
    c.speed  = speed;
    // board.h: shared bus on I2C1 — SCL seed::D11 (PB8), SDA seed::D12 (PB9),
    // brought out on the J42 Qwiic header (OLED 0x3C + hubs 0x30..0x33).
    c.pin_config.scl = seed::D11;
    c.pin_config.sda = seed::D12;
    return c;
}

bool EncoderHub::ReadReg(uint8_t reg, uint8_t *val)
{
    if(!i2c_)
        return false;
    return i2c_->ReadDataAtAddress(addr_, reg, 1, val, 1, kI2cTimeoutMs)
           == I2CHandle::Result::OK;
}

bool EncoderHub::WriteReg(uint8_t reg, uint8_t val)
{
    if(!i2c_)
        return false;
    return i2c_->WriteDataAtAddress(addr_, reg, 1, &val, 1, kI2cTimeoutMs)
           == I2CHandle::Result::OK;
}

bool EncoderHub::ReadBlock(uint8_t reg, uint8_t *buf, uint8_t len)
{
    if(!i2c_)
        return false;
    return i2c_->ReadDataAtAddress(addr_, reg, 1, buf, len, kI2cTimeoutMs)
           == I2CHandle::Result::OK;
}

bool EncoderHub::ReadTelemetry(uint8_t buf[RODEO_HUB_TELEMETRY_LEN])
{
    return ReadBlock(RODEO_HUB_REG_ENC_DELTA_BASE, buf, RODEO_HUB_TELEMETRY_LEN);
}

bool EncoderHub::ReadPressLatch(uint8_t *lo, uint8_t *hi)
{
    uint8_t b[2];
    if(!ReadBlock(RODEO_HUB_REG_SW_PRESSED_LO, b, 2))
        return false;
    *lo = b[0];
    *hi = b[1];
    return true;
}

bool EncoderHub::Init(I2CHandle *i2c, uint8_t address)
{
    i2c_     = i2c;
    addr_    = address;
    present_ = false;
    caps_    = 0;
    fw_ver_  = 0;

    // Identity gate: a hub answers WHO_AM_I = 0x9E; the OLED (0x3C) and empty
    // addresses do not. Then confirm the wire contract version matches ours.
    uint8_t who = 0, mapver = 0;
    if(!ReadReg(RODEO_HUB_REG_WHO_AM_I, &who) || who != RODEO_HUB_WHO_AM_I)
        return false;
    if(!ReadReg(RODEO_HUB_REG_MAP_VERSION, &mapver)
       || mapver != RODEO_HUB_MAP_VERSION)
        return false;

    ReadReg(RODEO_HUB_REG_CAPABILITIES, &caps_);
    ReadReg(RODEO_HUB_REG_FW_VERSION, &fw_ver_);
    present_ = true;
    return true;
}

} // namespace rodeo
