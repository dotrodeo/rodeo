// Host test: FIFO voice allocation + stealing (golden sequence).
#include "engine/voice_alloc.h"
#include <cassert>
#include <cstdio>

using namespace voicealloc;

int main()
{
    constexpr int N = 3;
    bool          active[N] = {false, false, false};
    bool          held[N]   = {false, false, false};
    uint8_t       notes[N]  = {0, 0, 0};
    uint32_t      age[N]    = {0, 0, 0};
    uint32_t      next      = 1;

    auto note_on = [&](uint8_t n) -> int {
        int i     = SelectNoteOnSlot(n, active, held, notes, age, N);
        notes[i]  = n;
        held[i]   = true;
        active[i] = true;
        age[i]    = next++;
        return i;
    };
    auto note_off = [&](uint8_t n) {
        for(int i = 0; i < N; ++i)
            if(held[i] && notes[i] == n)
                held[i] = false; // still active (release tail) until env ends
    };

    // Fill the pool in order.
    assert(note_on(60) == 0);
    assert(note_on(64) == 1);
    assert(note_on(67) == 2);

    // All sounding -> steal the oldest (slot 0, note 60).
    assert(note_on(72) == 0);

    // A key already sounding retriggers its own slot.
    assert(note_on(64) == 1);

    // Release note 67 and let its envelope finish -> that slot is now free and
    // should be reused before any stealing.
    note_off(67);
    active[2] = false; // envelope completed
    assert(note_on(76) == 2);

    std::printf("voice_alloc OK\n");
    return 0;
}
