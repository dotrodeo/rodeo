#pragma once
//
// Platform / MCU-level setup helpers (target-only).
//
namespace platform
{
/// Enable FPU flush-to-zero so denormals are treated as 0.
/// Denormal arithmetic on the Cortex-M7 is dramatically slower and can blow the
/// audio-block budget in filter/LFO feedback paths; flushing keeps DSP timing
/// deterministic. Call once after hardware init.
void EnableFlushToZero();
} // namespace platform
