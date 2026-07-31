#pragma once
#include "daisy_seed.h"
#include "engine/synth.h"

// Wraps libDaisy's UART MIDI on USART1 (its default pins PB6/PB7 are exactly the
// board's MIDI TX/RX) and converts Note On/Off into NoteEvents pushed to the
// synth's queue. Runs in the control plane (main loop). Omni for now — responds
// on all channels (channel/bend/mod-wheel handling arrives with M6).
class MidiEngine
{
  public:
    void Init();
    void Process(Synth& synth); // call from the main loop

  private:
    daisy::MidiUartHandler midi_;
};
