// rodeo-002 — the platform's DSP utilities, under the names this instrument uses.
//
// The code moved to rodeo-motherboard/firmware/common/rodeo_dsp.h on 2026-10-01:
// none of it is about a synthesiser, and rodeo-001 had already hand-rolled its
// own one-pole smoother for want of it. What is left here is the bridge, so
// every call site in this instrument keeps reading `fx::Smoother` rather than
// `rodeo::Smoother`, and so the place a reader looks for these types says where
// they actually live.
//
// Add a using-declaration here when the platform grows something this
// instrument wants. Do NOT re-declare anything: one definition, upstream.

#pragma once

#include "rodeo_dsp.h"

namespace rodeo002
{
namespace fx
{
using rodeo::kPi;
using rodeo::kTwoPi;
using rodeo::Clamp;
using rodeo::ExpMap;
using rodeo::TimeCoeff;
using rodeo::FreqCoeff;
using rodeo::FastTanh;
using rodeo::FastSin01;
using rodeo::Rng;
using rodeo::Smoother;
using rodeo::OnePole;
using rodeo::DcBlocker;
using rodeo::Lfo;

} // namespace fx
} // namespace rodeo002
