#pragma once
#include <cstdint>

// A note on/off crossing from the MIDI (control) plane to the audio plane.
struct NoteEvent
{
    uint8_t note;     // MIDI note number 0..127
    uint8_t velocity; // 0..127
    bool    gate;     // true = note on, false = note off
};
