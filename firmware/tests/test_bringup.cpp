// Host-native smoke test.
//
// M0 has little pure logic yet; this establishes the `make host-test` path and
// pins the invariants of the board contract that later milestones rely on.
#include "platform/board.h"
#include <cassert>
#include <cstdio>

int main()
{
    // Control-surface topology is fixed by the hardware.
    static_assert(board::kNumAdcChannels == board::kNumChips * board::kNumAdcPerChip,
                  "ADC channel count must equal chips x per-chip");
    static_assert(board::kNumAdcChannels == 24, "24 external ADC channels");
    static_assert(board::kNumDigitalIo == 12, "12 digital IO lines");
    static_assert(board::kBlockSize > 0
                      && (board::kBlockSize & (board::kBlockSize - 1)) == 0,
                  "block size should be a power of two");

    // Verified SPI mapping (see docs/architecture.md): MOSI on pin 11, MISO pin 10.
    assert(board::pin::kSpiMosi == 11);
    assert(board::pin::kSpiMiso == 10);
    assert(board::pin::kStatusLed == 28);

    std::printf("board OK: %d ADC ch, %d GPIO, block %d @ %.0f Hz\n",
                board::kNumAdcChannels,
                board::kNumDigitalIo,
                board::kBlockSize,
                static_cast<double>(board::kSampleRate));
    return 0;
}
