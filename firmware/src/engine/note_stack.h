#pragma once
#include <cstdint>

// Last-note-priority monophonic note stack.
//
// Pure logic (no DSP / hardware deps) so it is unit-tested on the host. Holding
// several keys and releasing the newest falls back to the most-recent key still
// held — standard mono synth behaviour. Full mode variants (legato, etc.) arrive
// in M7; this is the M1 building block.
template <int Capacity = 16>
class NoteStack
{
  public:
    void Clear() { count_ = 0; }
    int  Count() const { return count_; }
    bool Empty() const { return count_ == 0; }

    // Add a note. A duplicate is moved to the top (most-recent).
    void Push(uint8_t note, uint8_t velocity)
    {
        Remove(note);
        if(count_ < Capacity)
        {
            notes_[count_] = note;
            vels_[count_]  = velocity;
            ++count_;
        }
        else
        {
            // Full: drop the oldest, append the newest.
            for(int i = 0; i < Capacity - 1; ++i)
            {
                notes_[i] = notes_[i + 1];
                vels_[i]  = vels_[i + 1];
            }
            notes_[Capacity - 1] = note;
            vels_[Capacity - 1]  = velocity;
        }
    }

    // Remove a note if present. Returns true if it was held.
    bool Remove(uint8_t note)
    {
        int idx = -1;
        for(int i = 0; i < count_; ++i)
            if(notes_[i] == note)
            {
                idx = i;
                break;
            }
        if(idx < 0)
            return false;
        for(int i = idx; i < count_ - 1; ++i)
        {
            notes_[i] = notes_[i + 1];
            vels_[i]  = vels_[i + 1];
        }
        --count_;
        return true;
    }

    uint8_t TopNote() const { return count_ > 0 ? notes_[count_ - 1] : 0; }
    uint8_t TopVel() const { return count_ > 0 ? vels_[count_ - 1] : 0; }

  private:
    uint8_t notes_[Capacity];
    uint8_t vels_[Capacity];
    int     count_ = 0;
};
