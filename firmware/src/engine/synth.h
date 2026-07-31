#pragma once
#include <cstddef>
#include "daisy_seed.h"
#include "engine/voice.h"
#include "engine/patch.h"
#include "engine/note_event.h"
#include "engine/note_stack.h"
#include "util/spsc_queue.h"

// M1 audio engine: one voice, monophonic (last-note priority), driven by a
// lock-free note-event queue filled from the MIDI (control) plane. Voice state
// lives entirely in the audio plane; the queue is the only crossing point.
//
// M2 replaces the single voice + NoteStack with a 12-voice VoiceManager.
class Synth
{
  public:
    void Init(float sample_rate);

    // Producer side (control plane / main loop). Returns false if the queue is
    // full (event dropped rather than blocking the audio thread).
    bool PushNoteEvent(const NoteEvent& ev) { return note_queue_.Push(ev); }

    // Consumer side (audio callback). out[chan][frame], `size` frames.
    void Process(daisy::AudioHandle::OutputBuffer out, size_t size);

  private:
    void DrainNoteEvents();
    void ApplyNoteEvent(const NoteEvent& ev);

    Voice                    voice_;
    Patch                    patch_;
    NoteStack<16>            notes_;
    SpscQueue<NoteEvent, 64> note_queue_;
};
