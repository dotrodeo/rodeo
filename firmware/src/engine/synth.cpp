#include "engine/synth.h"

using namespace daisy;

void Synth::Init(float sample_rate)
{
    voice_.Init(sample_rate);
    notes_.Clear();
}

void Synth::Process(AudioHandle::OutputBuffer out, size_t size)
{
    DrainNoteEvents(); // apply all pending note events at the block boundary
    for(size_t i = 0; i < size; ++i)
    {
        const float s = voice_.Process(patch_) * patch_.master_vol;
        out[0][i]     = s; // left  -> J5 tip
        out[1][i]     = s; // right -> J5 ring
    }
}

void Synth::DrainNoteEvents()
{
    NoteEvent ev;
    while(note_queue_.Pop(ev))
        ApplyNoteEvent(ev);
}

void Synth::ApplyNoteEvent(const NoteEvent& ev)
{
    if(ev.gate)
    {
        notes_.Push(ev.note, ev.velocity);
        voice_.NoteOn(ev.note, ev.velocity, patch_); // retrigger newest note
    }
    else
    {
        notes_.Remove(ev.note);
        if(notes_.Empty())
            voice_.NoteOff();
        else // fall back to the most-recent key still held
            voice_.NoteOn(notes_.TopNote(), notes_.TopVel(), patch_);
    }
}
