#include "engine/synth.h"
#include <math.h>

using namespace daisy;

namespace
{
// Smooth bounded saturation on the master bus so stacked voices never hard-clip.
// Proper gain-staging / limiting is M8; this just keeps the sum well-behaved.
inline float SoftClip(float x)
{
    return tanhf(x);
}
} // namespace

void Synth::Init(float sample_rate)
{
    vm_.Init(sample_rate);
}

void Synth::Process(AudioHandle::OutputBuffer out, size_t size)
{
    DrainNoteEvents(); // apply all pending note events at the block boundary
    for(size_t i = 0; i < size; ++i)
    {
        const float s = SoftClip(vm_.Process(patch_) * patch_.master_vol);
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
        vm_.NoteOn(ev.note, ev.velocity, patch_);
    else
        vm_.NoteOff(ev.note);
}
