#pragma once
//
// The MAX17048 fuel gauge on the Rev B motherboard: its registers, and what
// their raw values mean. Host-safe, like io_plan.h; battery.h is the driver
// that reads it over I2C.
//
//   rodeo::max17048::Reading r = rodeo::max17048::Decode(vcell, soc, crate);
//   r.volts;          // 3.871
//   r.percent;        // 64.5
//   r.percent_per_hr; // +12.3 while charging, negative while running on the cell
//
// The gauge sits on the cell (CELL and VDD on BATT_P) and works out the state of
// charge from the voltage alone, with no sense resistor. It can't see USB power:
// the charger's PGOOD and CHG only light the power board's LEDs and don't reach
// the Seed. Whether the cell is charging is read from the rate of change, which
// takes a minute or two to settle after the charger starts or stops.
//
#include <cstdint>

namespace rodeo
{
namespace max17048
{
constexpr uint8_t kVcell   = 0x02; // 78.125 uV per bit
constexpr uint8_t kSoc     = 0x04; // 1/256 % per bit
constexpr uint8_t kMode    = 0x06;
constexpr uint8_t kVersion = 0x08; // 0x001x
constexpr uint8_t kConfig  = 0x0C;
constexpr uint8_t kCrate   = 0x16; // signed, 0.208 %/hr per bit
constexpr uint8_t kVresetId = 0x18;
constexpr uint8_t kStatus  = 0x1A;
constexpr uint8_t kCommand = 0xFE;

/** STATUS bits, in the 16-bit register. */
constexpr uint16_t kStatusResetIndicator = 1u << 8;  // RI: just powered up, not configured
constexpr uint16_t kStatusVoltageHigh    = 1u << 9;  // VH
constexpr uint16_t kStatusVoltageLow     = 1u << 10; // VL
constexpr uint16_t kStatusVoltageReset   = 1u << 11; // VR
constexpr uint16_t kStatusSocLow         = 1u << 12; // HD: SOC under CONFIG's threshold
constexpr uint16_t kStatusSocChange      = 1u << 13; // SC: SOC moved by 1 %

/** Above this rate, either way, the cell counts as charging or discharging. */
constexpr float kIdleBandPercentPerHr = 1.0f;

/** A 16-bit register as the gauge sends it: big-endian. */
constexpr uint16_t Word(const uint8_t b[2]) { return static_cast<uint16_t>((b[0] << 8) | b[1]); }

constexpr float Volts(uint16_t vcell) { return vcell * 78.125e-6f; }
constexpr float Percent(uint16_t soc) { return soc / 256.0f; }
constexpr float PercentPerHr(uint16_t crate)
{
    return static_cast<int16_t>(crate) * 0.208f;
}

/** True if the VERSION register reads as a MAX17048/49. */
constexpr bool VersionOk(uint16_t version) { return (version & 0xFFF0u) == 0x0010u; }

enum class Trend : uint8_t
{
    kIdle, // full, or neither charging nor in use enough to tell
    kCharging,
    kDischarging,
};

struct Reading
{
    float volts;
    float percent; // can read a little over 100 when full
    float percent_per_hr;
    Trend trend;
};

inline Reading Decode(uint16_t vcell, uint16_t soc, uint16_t crate)
{
    Reading r;
    r.volts          = Volts(vcell);
    r.percent        = Percent(soc);
    r.percent_per_hr = PercentPerHr(crate);
    r.trend          = r.percent_per_hr > kIdleBandPercentPerHr    ? Trend::kCharging
                       : r.percent_per_hr < -kIdleBandPercentPerHr ? Trend::kDischarging
                                                                   : Trend::kIdle;
    return r;
}

inline const char* TrendName(Trend t)
{
    switch(t)
    {
        case Trend::kCharging: return "charging";
        case Trend::kDischarging: return "discharging";
        default: return "idle";
    }
}
} // namespace max17048
} // namespace rodeo
