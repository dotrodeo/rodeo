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


// --- Pot span calibration ---------------------------------------------------
//
// A rodeo-potentiometer satellite does NOT swing rail to rail. Its schematic
// feeds the pot's top leg from the connector's +3V3 pin through R1 = 1 kOhm
// (J1 pin 3 -> R1 -> pot top; C1 100 nF decouples that node; the wiper goes
// straight out on pin 2 and the bottom leg straight to GND). So the top of pot
// travel sits at
//
//     Vtop = 3.3 V * Rpot / (Rpot + 1 kOhm)
//
// which for the nominal 10 kOhm pot is 3.00 V, not 3.3 V. The ADC's reference
// is that same +3V3, so this does NOT cancel ratiometrically: uncorrected, a
// knob at its clockwise stop reads 0.909, and every parameter tops out at 91 %.
// Alps RK09K tolerance (+/-20 %) then moves the real endpoint per pot, roughly
// 0.889 .. 0.923 of full scale -- which is why the endpoint is LEARNED per
// channel rather than fixed to one constant.
//
// This is what RODEO_ADC_LIB_DESIGN.md section 9 assumed when it said absolute
// accuracy does not matter because "endpoints self-normalize via GetNorm".
//
// Note the split: GetVolts() stays the true measured voltage (it is a bring-up
// diagnostic and must not lie), while GetNorm() is the *control* value and is
// the one that gets span-normalized.

constexpr float kPotSeriesOhms  = 1000.0f;  // R1 on the satellite
constexpr float kPotNominalOhms = 10000.0f; // control-map contract: <= 10 kOhm
constexpr float kPotTolerance   = 0.20f;    // Alps RK09K, +/-20 %

// Fraction of Vref reached at the top of travel for a pot of `rpot` ohms.
constexpr float PotTopFraction(float rpot)
{
    return rpot / (rpot + kPotSeriesOhms);
}
constexpr uint16_t PotTopCode(float rpot)
{
    return static_cast<uint16_t>(static_cast<float>(kAdcCodeMax)
                                 * PotTopFraction(rpot));
}

// Seed the learned span PESSIMISTICALLY -- narrower than any real pot -- because
// the learner only ever widens. Seeding at the worst-case (-20 %) pot means a
// knob reaches a full 1.0 from the very first boot on any unit; sweeping it then
// widens the span to that pot's true endpoint and recovers the lost resolution.
// Seeding at nominal instead would leave an under-spec pot permanently unable to
// reach 1.0, which is the failure this whole mechanism exists to prevent.
constexpr uint16_t kPotSeedHiCode
    = PotTopCode(kPotNominalOhms * (1.0f - kPotTolerance)); // ~29126 = 88.9 %
constexpr uint16_t kPotSeedLoCode = static_cast<uint16_t>(kAdcCodeMax / 100);

// A satellite pot physically cannot read above this: even a +20 % pot only
// reaches 12/13 of full scale through R1. Anything higher is a glitch, not a
// knob, so the learned span refuses to follow it -- otherwise one spurious
// full-scale sample would permanently undo the calibration.
constexpr uint16_t kPotMaxPlausibleHiCode
    = static_cast<uint16_t>(static_cast<float>(kAdcCodeMax) * 0.95f);

// Pulled in from each end so the last sliver of travel saturates: with an exact
// span you would reach 0.999 at the stop and never quite 1.0, because the code
// dithers by a few LSB.
constexpr uint16_t kPotEdgeMarginCodes
    = static_cast<uint16_t>(kAdcCodeMax / 200); // 0.5 % of full scale

// Below this the span is not a working pot (absent, shorted, never swept), so
// normalization falls back to the raw full-scale conversion.
constexpr uint16_t kPotMinSpanCodes = static_cast<uint16_t>(kAdcCodeMax / 10);

// One channel's learned travel. Widens only -- see kPotSeedHiCode.
struct PotSpan
{
    uint16_t lo = kPotSeedLoCode;
    uint16_t hi = kPotSeedHiCode;

    // Fold a fresh (already clamped) code into the span. Returns true if the
    // span grew, which callers can use to log a calibration change.
    bool Observe(uint16_t code)
    {
        bool grew = false;
        if(code < lo)
        {
            lo   = code;
            grew = true;
        }
        if(code > hi && code <= kPotMaxPlausibleHiCode)
        {
            hi   = code;
            grew = true;
        }
        return grew;
    }
};

// Clamped code -> normalized 0..1 across a channel's learned travel.
inline float AdcCodeToNormSpan(uint16_t code, const PotSpan &span)
{
    if(span.hi <= span.lo || span.hi - span.lo < kPotMinSpanCodes)
        return AdcCodeToNorm(code); // not a usable span: raw full-scale

    const float lo = static_cast<float>(span.lo) + kPotEdgeMarginCodes;
    const float hi = static_cast<float>(span.hi) - kPotEdgeMarginCodes;
    if(hi <= lo)
        return AdcCodeToNorm(code);

    const float n = (static_cast<float>(code) - lo) / (hi - lo);
    if(n < 0.0f)
        return 0.0f;
    if(n > 1.0f)
        return 1.0f;
    return n;
}

} // namespace rodeo
