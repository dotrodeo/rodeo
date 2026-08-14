// Rodeo bring-up 04 — external ADC single-channel read (ADS114S08, U3).
//
// The 24-channel control surface is read through two TI ADS114S08 16-bit SPI
// ADCs (U3/U4) on SPI1 — NOT the Daisy's onboard ADC. This test brings up ONE
// channel (AIN0 vs AINCOM) on U3 and streams the reading over USB serial.
//
//   PASS: open a serial monitor after flashing (the Daisy's onboard USB). A
//         value on AIN0 (e.g. a pot wiper between AGND and the ADC reference)
//         prints ~10x/sec and tracks the input.
//
// ---------------------------------------------------------------------------
// FIRST BRING-UP DRAFT — the external ADCs are the one subsystem not yet
// hardware-verified. Two things to confirm on the bench before trusting it:
//   1. Reference: this configures the ADS114S08 INTERNAL 2.5 V reference. If the
//      board provides an external reference on REFP0/REFN0, change REG_REF below
//      (and the 2.5f volts scale) to match. Coordinate with hardware.
//   2. DRDY# is not wired to the MCU, so we poll RDATA on a fixed cadence rather
//      than waiting on data-ready. Fine for a slow single-channel test.
// SPI pins (SCK D8 / MISO D9 / MOSI D10) and the U3 chip-select (D25) are from
// board.h, which marks the ADC SPI wiring as verified.
// ---------------------------------------------------------------------------

#include "daisy_seed.h"
#include "board.h"

using namespace daisy;

DaisySeed hw;
SpiHandle spi;
GPIO      adc_cs; // U3 chip-select, soft NSS (active low)

// --- ADS114S08 SPI commands ---
static constexpr uint8_t CMD_RESET = 0x06;
static constexpr uint8_t CMD_START = 0x08; // start/restart conversions
static constexpr uint8_t CMD_RDATA = 0x12; // read conversion data
static constexpr uint8_t CMD_WREG  = 0x40; // OR with register address

// --- register addresses ---
static constexpr uint8_t REG_INPMUX = 0x02; // input multiplexer
static constexpr uint8_t REG_REF    = 0x05; // reference control

// --- register values ---
// INPMUX: MUXP = AIN0 (0x0), MUXN = AINCOM (0xC) -> single-ended AIN0.
static constexpr uint8_t INPMUX_AIN0 = 0x0C;
// REF: internal 2.5 V reference, always on, both ref buffers enabled.
//   bits[1:0] REFSEL=10 (internal 2.5V), bits[3:2] REFCON=10 (always on).
static constexpr uint8_t REF_INTERNAL_2V5 = 0x0A;
static constexpr float   kVref            = 2.5f;

static inline void CsLow() { adc_cs.Write(false); }
static inline void CsHigh() { adc_cs.Write(true); }

static void SendCmd(uint8_t cmd)
{
    CsLow();
    spi.BlockingTransmit(&cmd, 1);
    CsHigh();
}

static void WriteReg(uint8_t reg, uint8_t val)
{
    // WREG frame: (0x40|addr), (num regs - 1), data...
    uint8_t tx[3] = {static_cast<uint8_t>(CMD_WREG | reg), 0x00, val};
    CsLow();
    spi.BlockingTransmit(tx, 3);
    CsHigh();
}

static int16_t ReadData()
{
    uint8_t tx[3] = {CMD_RDATA, 0x00, 0x00};
    uint8_t rx[3] = {0, 0, 0};
    CsLow();
    spi.BlockingTransmitAndReceive(tx, rx, 3);
    CsHigh();
    return static_cast<int16_t>((rx[1] << 8) | rx[2]); // 16-bit signed
}

int main(void)
{
    hw.Init();
    hw.StartLog(false); // telemetry over onboard USB once a monitor connects

    // SPI1, mode 1 (CPOL=0, CPHA=1) as required by the ADS114S08.
    SpiHandle::Config cfg;
    cfg.periph          = SpiHandle::Config::Peripheral::SPI_1;
    cfg.mode            = SpiHandle::Config::Mode::MASTER;
    cfg.direction       = SpiHandle::Config::Direction::TWO_LINES;
    cfg.nss             = SpiHandle::Config::NSS::SOFT;
    cfg.clock_polarity  = SpiHandle::Config::ClockPolarity::LOW;
    cfg.clock_phase     = SpiHandle::Config::ClockPhase::TWO_EDGE;
    cfg.baud_prescaler  = SpiHandle::Config::BaudPrescaler::PS_32;
    cfg.pin_config.sclk = seed::D8;
    cfg.pin_config.miso = seed::D9;
    cfg.pin_config.mosi = seed::D10;
    spi.Init(cfg);

    adc_cs.Init(seed::D25, GPIO::Mode::OUTPUT); // U3 CS
    CsHigh();

    System::Delay(10); // power-up / internal reference settle
    SendCmd(CMD_RESET);
    System::Delay(5);

    WriteReg(REG_REF, REF_INTERNAL_2V5);
    WriteReg(REG_INPMUX, INPMUX_AIN0);
    // DATARATE left at reset default (0x14 -> 20 SPS, normal mode).

    SendCmd(CMD_START); // begin continuous conversions

    for(;;)
    {
        int16_t code  = ReadData();
        float   volts = (static_cast<float>(code) / 32768.0f) * kVref;

        // Daisy's logger has no %f; print millivolts as an integer.
        int mv = static_cast<int>(volts * 1000.0f);
        hw.PrintLine("AIN0 code=%d  mV=%d", code, mv);

        System::Delay(100);
    }
}
