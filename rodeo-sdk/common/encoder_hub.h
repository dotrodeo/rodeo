#pragma once
//
// Rodeo Encoder Hub — Layer 1 device driver for one rodeo-encoder-hub board.
//
// One instance per physical hub (Rodeo's shared I2C1 bus carries up to four,
// addresses 0x30..0x33, plus the SSD1309 OLED at 0x3C). Thin and generic: owns
// a 7-bit address + a reference to an already-initialized shared I2CHandle,
// speaks the register-pointer protocol from the shared wire contract, and
// presence-checks the hub identity. It knows nothing about "which control" a
// slot means — the multi-hub scan + slot mapping live in Layer 2
// (RodeoEncoders) and the control map.
//
// The register map is the single shared header include/rodeo_hub_regs.h, so the
// byte layout can never drift from the hub target firmware.
//
// See rev-a/rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md for the full protocol.
//
#include "daisy_seed.h"
#include "rodeo_hub_regs.h"

namespace rodeo
{
// One rodeo-encoder-hub (STM32C031C6 I2C target) on the shared bus.
class EncoderHub
{
  public:
    static constexpr uint8_t  kNumEncoders = RODEO_HUB_ENC_COUNT; // 9
    static constexpr uint32_t kI2cTimeoutMs = 2; // per blocking transaction

    // Build the shared I2C1 config for the Rodeo bus. The bus is shared by the
    // OLED + up to four hubs, so the OWNER (Layer 2) inits ONE I2CHandle with
    // this and passes it to each EncoderHub::Init.
    // Pins per board.h: SCL seed::D11 (PB8), SDA seed::D12 (PB9), I2C1, master.
    // Contract boot speed is 100 kHz; promote to 400 kHz once Bumble validates
    // the populated bus.
    static daisy::I2CHandle::Config
    DefaultI2cConfig(daisy::I2CHandle::Config::Speed speed
                     = daisy::I2CHandle::Config::Speed::I2C_100KHZ);

    // i2c: already-initialized shared handle. address: this hub's 7-bit address
    // (0x30..0x33). Probes WHO_AM_I (== 0x9E) and MAP_VERSION (== contract 0x01)
    // and caches CAPABILITIES / FW_VERSION. Returns false (and leaves
    // Present() == false) if nothing answers, the identity byte is wrong (e.g.
    // the OLED at 0x3C), or the map version mismatches.
    bool Init(daisy::I2CHandle *i2c, uint8_t address);

    bool    Present() const { return present_; }
    uint8_t Address() const { return addr_; }
    uint8_t Capabilities() const { return caps_; }
    uint8_t FirmwareVersion() const { return fw_ver_; }
    bool HasEncoders() const { return caps_ & RODEO_HUB_CAP_ENCODERS; }
    bool HasSwitches() const { return caps_ & RODEO_HUB_CAP_SWITCHES; }
    bool HasLeds() const { return caps_ & RODEO_HUB_CAP_LED; }

    // --- register-pointer access (single byte) ---
    bool ReadReg(uint8_t reg, uint8_t *val);
    bool WriteReg(uint8_t reg, uint8_t val);

    // Read a contiguous block starting at `reg` (auto-increment). Reading a
    // read-to-clear register (encoder deltas 0x10.., press latches 0x20/0x21)
    // clears it on the hub as its byte is read — see the contract.
    bool ReadBlock(uint8_t reg, uint8_t *buf, uint8_t len);

    // The hot 16-byte telemetry burst from 0x10: nine read-to-clear int8 deltas
    // (0x10..0x18) + live switch level (0x19/0x1A) + reserved pad. Clears the
    // hub's delta accumulators.
    bool ReadTelemetry(uint8_t buf[RODEO_HUB_TELEMETRY_LEN]);

    // Read-to-clear the switch press-edge latches (0x20 lo / 0x21 hi). Catches a
    // tap that begins and ends between polls.
    bool ReadPressLatch(uint8_t *lo, uint8_t *hi);

    bool SendCommand(uint8_t cmd) { return WriteReg(RODEO_HUB_REG_CMD, cmd); }
    bool SetConfig(uint8_t cfg) { return WriteReg(RODEO_HUB_REG_CONFIG, cfg); }

  private:
    daisy::I2CHandle *i2c_     = nullptr;
    uint8_t           addr_    = 0;
    bool              present_ = false;
    uint8_t           caps_    = 0;
    uint8_t           fw_ver_  = 0;
};

} // namespace rodeo
