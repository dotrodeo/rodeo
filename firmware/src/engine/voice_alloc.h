#pragma once
#include <cstdint>

// Pure voice-allocation policy (no DSP / hardware) so it is unit-tested on the
// host with golden sequences. FIFO allocation + FIFO (oldest) stealing:
//   1. a key already sounding retriggers its own voice,
//   2. otherwise the first free (silent) voice is used,
//   3. otherwise the oldest-started voice is stolen.
namespace voicealloc
{
// Index of the held voice already playing `note`, or -1.
inline int FindHeldNote(const bool* held, const uint8_t* notes, uint8_t note, int n)
{
    for(int i = 0; i < n; ++i)
        if(held[i] && notes[i] == note)
            return i;
    return -1;
}

// Index of the first inactive (silent) voice, or -1 if all are sounding.
inline int FindFree(const bool* active, int n)
{
    for(int i = 0; i < n; ++i)
        if(!active[i])
            return i;
    return -1;
}

// Index of the oldest voice (smallest age counter). Assumes n >= 1.
inline int FindOldest(const uint32_t* age, int n)
{
    int best = 0;
    for(int i = 1; i < n; ++i)
        if(age[i] < age[best])
            best = i;
    return best;
}

// Slot a note-on should (re)trigger, applying the policy above.
inline int SelectNoteOnSlot(uint8_t        note,
                            const bool*    active,
                            const bool*    held,
                            const uint8_t* notes,
                            const uint32_t* age,
                            int            n)
{
    const int same = FindHeldNote(held, notes, note, n);
    if(same >= 0)
        return same;
    const int freeSlot = FindFree(active, n);
    if(freeSlot >= 0)
        return freeSlot;
    return FindOldest(age, n);
}
} // namespace voicealloc
