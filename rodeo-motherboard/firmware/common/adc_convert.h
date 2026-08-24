#pragma once
//
// Rodeo ADC — pure code<->value conversions, no hardware dependencies.
//
// Split out of RodeoAdc's accessors so the value pipeline (clamp, volts, norm)
// can be unit-tested on the HOST with no libDaisy/SPI in the build. RodeoAdc's
// GetCode/GetVolts/GetNorm delegate here, so these functions ARE the production
// path — one source of truth, no drift between the test and the firmware.
//
#include <cstdint>

namespace rodeo
{
// ADS114S08 unipolar single-ended full-scale code. The chip returns a SIGNED
// 16-bit result; a pot at fully-off can read a few codes negative (offset +
// noise + leakage), so 32767 (not 65535) is the usable top of a 0..Vref sweep.
static constexpr int32_t kAdcCodeMax = 32767;

// Clamp a signed conversion code to the unipolar [0, 32767] range. THE GLITCH
// GUARD: a negative tail must clamp to 0, never wrap to ~full-scale when cast to
// unsigned — otherwise a fully-off knob reads as fully-on. See RODEO_ADC_LIB_DESIGN.md.
inline uint16_t ClampAdcCode(int16_t code)
{
    return code < 0 ? 0 : static_cast<uint16_t>(code);
}

// Clamped code -> volts against a ratiometric reference (Rodeo: 3.3 V).
inline float AdcCodeToVolts(uint16_t code, float vref)
{
    return (static_cast<float>(code) / static_cast<float>(kAdcCodeMax)) * vref;
}

// Clamped code -> normalized 0..1 (high end clamped for the rare over-range).
inline float AdcCodeToNorm(uint16_t code)
{
    float n = static_cast<float>(code) / static_cast<float>(kAdcCodeMax);
    return n > 1.0f ? 1.0f : n;
}

} // namespace rodeo
