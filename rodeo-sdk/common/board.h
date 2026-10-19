#pragma once
//
// Rodeo motherboard, Rev B — the pin map and the board's fixed facts.
//
// Every Seed pin the motherboard uses, by what it is wired to, so drivers and
// apps say board::pin::kSatTx rather than seed::D2. Host-safe: plain constants
// and constexpr functions only, no libDaisy, so host tests include it too and
// check the map itself (common/tests/board_gtest.cpp). Target code converts a
// SeedPin to libDaisy's Pin with board_daisy.h.
//
// Source of truth: hardware/rodeo-motherboard/docs/rev-b.md, "Seed pin map",
// checked against libDaisy's seed::D0-D30 and seed::A0-A11 (daisy_seed.h).
//
// Which control or sound a connector carries is an instrument's decision, in
// its own control map. Nothing here names one.
//
#include <cstdint>

namespace board
{
/** One Seed pin: the STM32 port and pin, libDaisy's seed::D number, and the
 *  physical pin on the Seed's 40-pin header. */
struct SeedPin
{
    char    port;   // 'A'..'G'
    uint8_t num;    // 0..15 within the port; also its EXTI line
    int8_t  seed_d; // seed::D<n>
    uint8_t header; // 1..40
};

/** Two pins share an external-interrupt line when their pin numbers match,
 *  whatever the port: only one of them can be an interrupt input. */
constexpr bool SharesExti(SeedPin a, SeedPin b)
{
    return a.num == b.num && a.port != b.port;
}

constexpr bool SamePin(SeedPin a, SeedPin b)
{
    return a.port == b.port && a.num == b.num;
}

namespace pin
{
// Satellite chain, USART3 (AF7). The ring's wire protocol is sat_frame.h.
constexpr SeedPin kSatRx = {'C', 11, 1, 2};
constexpr SeedPin kSatTx = {'C', 10, 2, 3};

// Second codec (CS4272) on SAI2. Reset has a 10 kOhm pull-down: hold the codec
// in reset until SAI2's MCLK and LRCK run, then release it.
constexpr SeedPin kCodec2Reset = {'C', 9, 3, 4};
constexpr SeedPin kCodec2Mclk  = {'A', 1, 24, 31};
constexpr SeedPin kCodec2Sdout = {'A', 0, 25, 32};  // from its ADC: SAI2 SD_B
constexpr SeedPin kCodec2Sdin  = {'D', 11, 26, 33}; // to its DAC: SAI2 SD_A
constexpr SeedPin kCodec2Lrck  = {'G', 9, 27, 34};  // SAI2 FS
constexpr SeedPin kCodec2Sclk  = {'A', 2, 28, 35};  // SAI2 SCK

// microSD, SDMMC1 in 1-bit mode: D1-D3 would need the satellite chain's pins.
constexpr SeedPin kSdD0  = {'C', 8, 4, 5};
constexpr SeedPin kSdCmd = {'D', 2, 5, 6};
constexpr SeedPin kSdClk = {'C', 12, 6, 7};

// SPI1 to the expansion header (an optional ADC board, or an SPI display).
// CS has a 10 kOhm pull-up, so the header's device stays deselected at boot.
constexpr SeedPin kSpiExtCs = {'B', 12, 0, 1};
constexpr SeedPin kSpiSck   = {'G', 11, 8, 9};
constexpr SeedPin kSpiMiso  = {'B', 4, 9, 10};
constexpr SeedPin kSpiMosi  = {'B', 5, 10, 11};

// I2C1: the OLED, the battery gauge and four taps. 2.2 kOhm pull-ups on board.
constexpr SeedPin kI2cScl = {'B', 8, 11, 12};
constexpr SeedPin kI2cSda = {'B', 9, 12, 13};

// MIDI, USART1, to the MIDI daughterboard.
constexpr SeedPin kMidiTx = {'B', 6, 13, 14};
constexpr SeedPin kMidiRx = {'B', 7, 14, 15};

// USB_OTG_HS on its internal full-speed PHY, to the power board's USB-C.
constexpr SeedPin kUsbDm = {'B', 14, 29, 36};
constexpr SeedPin kUsbDp = {'B', 15, 30, 37};
} // namespace pin

// --- I/O connectors ---------------------------------------------------------
// Ten 3-pin JST SH connectors: GND, the signal through 1 kOhm, +3V3_A. The
// single-pot board's pinout, so a pot plugs straight into an analog-capable one.

/** One I/O connector, named by its silkscreen. */
struct IoConnector
{
    const char* label; // silkscreen: "D1", "A1".."A7", "CV1", "CV2"
    SeedPin     pin;
    int8_t      adc;   // seed::A<n> for an analog read, or -1 (digital only)
    int8_t      dac;   // DAC channel 1 or 2, or -1
};

constexpr int kNumIo = 10;

/** In silkscreen order. Index into this with the Io enum below. */
constexpr IoConnector kIo[kNumIo] = {
    {"D1", {'G', 10, 7, 8}, -1, -1},
    {"A1", {'C', 0, 15, 22}, 0, -1},
    {"A2", {'A', 3, 16, 23}, 1, -1},
    {"A3", {'B', 1, 17, 24}, 2, -1},
    {"A4", {'A', 7, 18, 25}, 3, -1},
    {"A5", {'A', 6, 19, 26}, 4, -1},
    {"A6", {'C', 1, 20, 27}, 5, -1},
    {"A7", {'C', 4, 21, 28}, 6, -1},
    {"CV1", {'A', 4, 23, 30}, 8, 1},
    {"CV2", {'A', 5, 22, 29}, 7, 2},
};

enum Io : uint8_t
{
    kIoD1,
    kIoA1,
    kIoA2,
    kIoA3,
    kIoA4,
    kIoA5,
    kIoA6,
    kIoA7,
    kIoCv1,
    kIoCv2,
};

// --- I2C addresses (7-bit) --------------------------------------------------
namespace i2c
{
constexpr uint8_t kOledAddr         = 0x3C; // SSD130x, on the OLED's own board
constexpr uint8_t kBatteryGaugeAddr = 0x36; // MAX17048 on the motherboard
} // namespace i2c

// --- Audio --------------------------------------------------------------------
// Channels follow the back panel: 1/2 are the Seed's own codec (SAI1), 3/4 the
// CS4272 (SAI2).
constexpr int   kNumAudioIn  = 4;
constexpr int   kNumAudioOut = 4;
constexpr float kSampleRate  = 48000.0f;
// One block per millisecond, so a USB audio frame is exactly one block.
constexpr int kBlockSize = 48;

// The satellite chain's UART instance; its baud rate is part of the protocol.
constexpr int kSatUsart = 3;
} // namespace board
