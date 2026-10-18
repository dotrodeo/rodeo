#include "oled_ssd130x_bulk.h"

using namespace daisy;

namespace rodeo
{
namespace
{
    void (*g_yield)() = nullptr;
    bool g_link_ok    = true;
} // namespace

void SetOledYield(void (*fn)())
{
    g_yield = fn;
}

bool OledLinkOk()
{
    return g_link_ok;
}

void Ssd130xI2cBulkTransport::Init(const Config &config)
{
    addr_       = config.i2c_address;
    timeout_ms_ = PageTimeoutMs(config.i2c_config.speed);
    i2c_.Init(config.i2c_config);
    g_link_ok = true;
}

void Ssd130xI2cBulkTransport::SendCommand(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd}; // 0x00 = Co=0, D/C#=0 -> command stream
    g_link_ok = i2c_.TransmitBlocking(addr_, buf, 2, timeout_ms_)
                == I2CHandle::Result::OK;
}

void Ssd130xI2cBulkTransport::SendData(uint8_t *buff, size_t size)
{
    size_t sent = 0;
    while(sent < size)
    {
        size_t n = size - sent;
        if(n > kMaxChunk)
            n = kMaxChunk;

        tx_[0] = 0x40; // Co=0, D/C#=1 -> everything after this is display data
        for(size_t i = 0; i < n; ++i)
            tx_[1 + i] = buff[sent + i];

        g_link_ok = i2c_.TransmitBlocking(
                        addr_, tx_, static_cast<uint16_t>(n + 1), timeout_ms_)
                    == I2CHandle::Result::OK;
        sent += n;

        // The whole point: hand the main loop back between pages.
        if(g_yield)
            g_yield();
    }
}

} // namespace rodeo
