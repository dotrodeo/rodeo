#pragma once
//
// Rodeo ADC — Layer 1 device driver for the TI ADS114S08 (SBAS815A).
//
// One instance per physical chip (Rodeo has two, U3/U4, on shared SPI1 with
// separate chip-selects). Thin and generic: owns a CS line + a reference to an
// already-initialized shared SpiHandle, knows the chip's command/register map,
// and bakes in the Rodeo board defaults (external 3.3V ref, PGA bypass,
// low-latency filter @ 1000 SPS). It knows nothing about "which pot" — the
// two-chip scan + channel mapping live in Layer 2 (RodeoAdc) and board.h.
//
// See planning/RODEO_ADC_LIB_DESIGN.md for the full design + datasheet references.
//
#include "daisy_seed.h"

namespace rodeo
{
// TI ADS114S08: 16-bit delta-sigma, 12-channel mux, SPI mode 1.
class Ads114s08
{
  public:
    // --- SPI commands (SBAS815A Table 16) ---
    static constexpr uint8_t kCmdReset = 0x06;
    static constexpr uint8_t kCmdStart = 0x08; // start/restart conversions
    static constexpr uint8_t kCmdStop  = 0x0A;
    static constexpr uint8_t kCmdRdata = 0x12; // read conversion data
    static constexpr uint8_t kCmdRreg  = 0x20; // OR with register address
    static constexpr uint8_t kCmdWreg  = 0x40; // OR with register address

    // --- register addresses ---
    static constexpr uint8_t kRegInpmux   = 0x02;
    static constexpr uint8_t kRegPga      = 0x03;
    static constexpr uint8_t kRegDatarate = 0x04;
    static constexpr uint8_t kRegRef      = 0x05;

    // --- Rodeo board register values (see design doc §6) ---
    // REF: external REFP0/REFN0 (board +3V3/GND), both ref buffers bypassed
    //   (they sit at the rails), internal 2.5V ref off.
    static constexpr uint8_t kRefExternal3v3 = 0x30;
    // PGA: PGA_EN=00 (bypass, rail-to-rail), GAIN=000, DELAY=000 (14*t_MOD).
    //   This is also the reset default; written explicitly for clarity.
    static constexpr uint8_t kPgaBypass = 0x00;
    // DATARATE: FILTER=1 (low-latency, single-cycle settle) + DR=1011 (1000 SPS).
    static constexpr uint8_t kDatarate1000Ll = 0x1B;
    // INPMUX MUXN nibble: AINCOM (board-tied to GND) for single-ended reads.
    static constexpr uint8_t kMuxnAincom = 0x0C;

    static constexpr float   kVref            = 3.3f; // external ref span
    static constexpr uint8_t kNumChannels     = 12;   // AIN0..AIN11 per chip

    // Build the shared SPI1 config the ADS114S08 needs: mode 1 (CPOL=0, CPHA=1),
    // soft NSS, two-line master. The bus is shared by both chips, so the OWNER
    // inits one SpiHandle with this and passes it to each Ads114s08::Init.
    // Pins per board.h: SCK seed::D8, MISO seed::D9 (<-DOUT), MOSI seed::D10 (->DIN).
    static daisy::SpiHandle::Config DefaultSpiConfig();

    // Configure this chip's CS as an idle-HIGH output with NO bus traffic.
    // MUST be called on every chip on a shared bus BEFORE any chip is probed:
    // the CS nets have no pull-ups, so a not-yet-initialized sibling's CS
    // floats — if it drifts low it selects that chip and both drive the shared
    // MISO during the first chip's presence read (contention -> the first chip
    // spuriously "fails"). Parking all CS high first makes probing order-safe.
    void InitCsIdle(daisy::Pin cs_pin);

    // spi: already-initialized shared handle. cs_pin: this chip's chip-select
    // (seed::D25 for U3, seed::D26 for U4). Resets the chip, applies the board
    // defaults, and verifies presence via a register write+readback.
    // Returns false (and leaves Present()==false) if the chip does not answer.
    bool Init(daisy::SpiHandle *spi, daisy::Pin cs_pin);

    bool Present() const { return present_; }

    void    Reset();
    void    Start();
    void    Stop();
    void    WriteReg(uint8_t addr, uint8_t val);
    uint8_t ReadReg(uint8_t addr);

    // WREG INPMUX to select single-ended AINx vs AINCOM. Writing INPMUX
    // auto-resets the digital filter and restarts the conversion (SBAS815A
    // §9.5.3.12), so this IS the per-mux-hop restart — no separate Start needed.
    void SelectChannel(uint8_t ain);

    // RDATA — latest conversion, signed 16-bit. Unipolar single-ended pots read
    // 0..+32767, but a fully-off input can dip a few codes negative (offset +
    // noise + leakage); callers MUST clamp before any unsigned use (Layer 2).
    int16_t ReadData();

  private:
    void CsLow() { cs_.Write(false); }  // active low
    void CsHigh() { cs_.Write(true); }

    daisy::SpiHandle *spi_ = nullptr;
    daisy::GPIO       cs_;
    bool              present_ = false;
};

} // namespace rodeo
