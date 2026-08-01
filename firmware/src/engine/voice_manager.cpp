#include "engine/voice_manager.h"
#include "engine/voice_alloc.h"

void VoiceManager::Init(float sample_rate)
{
    for(int i = 0; i < kNumVoices; ++i)
        voices_[i].Init(sample_rate);
}

void VoiceManager::RefreshActive()
{
    for(int i = 0; i < kNumVoices; ++i)
        active_[i] = voices_[i].IsActive();
}

void VoiceManager::NoteOn(uint8_t note, uint8_t velocity, const Patch& patch)
{
    RefreshActive();
    const int idx = voicealloc::SelectNoteOnSlot(
        note, active_, held_, notes_, age_, kNumVoices);
    notes_[idx] = note;
    held_[idx]  = true;
    age_[idx]   = next_age_++;
    voices_[idx].NoteOn(note, velocity, patch);
}

void VoiceManager::NoteOff(uint8_t note)
{
    for(int i = 0; i < kNumVoices; ++i)
        if(held_[i] && notes_[i] == note)
        {
            held_[i] = false;
            voices_[i].NoteOff(); // enter release; stays active until env ends
        }
}

float VoiceManager::Process(const Patch& patch)
{
    float sum = 0.0f;
    for(int i = 0; i < kNumVoices; ++i)
        sum += voices_[i].Process(patch);
    return sum;
}

int VoiceManager::ActiveCount() const
{
    int c = 0;
    for(int i = 0; i < kNumVoices; ++i)
        if(voices_[i].IsActive())
            ++c;
    return c;
}
