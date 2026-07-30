#include "platform/system.h"
#include "daisy_core.h" // pulls in CMSIS core intrinsics for the STM32H7

namespace platform
{
void EnableFlushToZero()
{
    // Cortex-M7 FPU: set FZ (bit 24) in the FPSCR so denormal results flush to
    // zero. (DAZ/denormals-are-zero is covered by the same FZ behaviour on the
    // ARMv7-M single-precision FPU.)
    __set_FPSCR(__get_FPSCR() | (1u << 24));
}
} // namespace platform
