#include "midi/midi_engine.h"

using namespace daisy;

void MidiEngine::Init()
{
    MidiUartHandler::Config cfg; // defaults to USART1 on PB6/PB7 (board MIDI)
    midi_.Init(cfg);
    midi_.StartReceive();
}

void MidiEngine::Process(Synth& synth)
{
    midi_.Listen();
    while(midi_.HasEvents())
    {
        MidiEvent m = midi_.PopEvent();
        switch(m.type)
        {
            case NoteOn:
            {
                const NoteOnEvent n = m.AsNoteOn();
                // A Note On with velocity 0 is a Note Off by convention.
                const NoteEvent ev{n.note, n.velocity, n.velocity > 0};
                synth.PushNoteEvent(ev);
                break;
            }
            case NoteOff:
            {
                const NoteOffEvent n = m.AsNoteOff();
                const NoteEvent    ev{n.note, 0, false};
                synth.PushNoteEvent(ev);
                break;
            }
            default: break;
        }
    }
}
