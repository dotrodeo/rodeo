#pragma once
#include <cstdint>
#include "engine/voice.h"
#include "engine/patch.h"

// 12-voice polyphonic pool with FIFO allocation and FIFO (oldest) stealing.
// Each voice has its own filter + filter envelope, so filter sweeps track per
// note. Allocation policy lives in voice_alloc.h (pure, host-tested); this class
// owns the real Voices and the bookkeeping.
class VoiceManager
{
  public:
    static constexpr int kNumVoices = 12;

    void  Init(float sample_rate);
    void  NoteOn(uint8_t note, uint8_t velocity, const Patch& patch);
    void  NoteOff(uint8_t note);
    float Process(const Patch& patch); // summed voices, one sample
    int   ActiveCount() const;

  private:
    void RefreshActive();

    Voice    voices_[kNumVoices];
    bool     active_[kNumVoices] = {false}; // refreshed from Voice::IsActive()
    bool     held_[kNumVoices]   = {false}; // key currently down
    uint8_t  notes_[kNumVoices]  = {0};
    uint32_t age_[kNumVoices]    = {0};
    uint32_t next_age_           = 1;
};
