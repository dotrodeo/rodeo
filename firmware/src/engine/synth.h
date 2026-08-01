#pragma once
#include <cstddef>
#include "daisy_seed.h"
#include "engine/voice_manager.h"
#include "engine/patch.h"
#include "engine/note_event.h"
#include "util/spsc_queue.h"

// Audio engine: 12-voice polyphonic (FIFO alloc + steal), driven by a lock-free
// note-event queue filled from the MIDI (control) plane. Voice state lives
// entirely in the audio plane; the queue is the only crossing point.
class Synth
{
  public:
    void Init(float sample_rate);

    // Producer side (control plane / main loop). Returns false if the queue is
    // full (event dropped rather than blocking the audio thread).
    bool PushNoteEvent(const NoteEvent& ev) { return note_queue_.Push(ev); }

    // Consumer side (audio callback). out[chan][frame], `size` frames.
    void Process(daisy::AudioHandle::OutputBuffer out, size_t size);

    int ActiveVoices() const { return vm_.ActiveCount(); } // for CPU/telemetry

  private:
    void DrainNoteEvents();
    void ApplyNoteEvent(const NoteEvent& ev);

    VoiceManager             vm_;
    Patch                    patch_;
    SpscQueue<NoteEvent, 64> note_queue_;
};
