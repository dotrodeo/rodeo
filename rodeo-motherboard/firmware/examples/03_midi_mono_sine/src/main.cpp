// Rodeo bring-up 03 — MIDI in -> mono sine voice.
//
// Proves MIDI IN (USART1, opto-isolated) end to end: play a MIDI keyboard and
// hear a single sine voice track pitch, with a short ADSR so note on/off is
// click-free. Last-note priority (monophonic).
//   PASS: notes sound at correct pitch on OUT L/R; release silences the voice.
//
// libDaisy's default MIDI UART transport already targets USART1 on PB6 (D13, TX)
// and PB7 (D14, RX) — exactly the Rodeo MIDI pins — so no pin overrides needed.
//
// Flash: put the Daisy in DFU (hold BOOT, tap RESET), then `make program-dfu`.

#include "daisy_seed.h"
#include "daisysp.h"
#include "board.h"
#include "status_led.h"

using namespace daisy;
using namespace daisysp;

DaisySeed        hw;
MidiUartHandler  midi;
Oscillator       osc;
Adsr             env;
rodeo::StatusLed status;
bool             gate = false;

// MIDI note number -> "C4"-style name (note 60 = middle C = C4).
static const char* NoteName(uint8_t note)
{
    static const char* const kNames[12]
        = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static char buf[5];
    int octave = static_cast<int>(note) / 12 - 1;
    const char* n = kNames[note % 12];
    // e.g. "C#4" — small manual format (Daisy logger, no std::string).
    int i = 0;
    buf[i++] = n[0];
    if(n[1])
        buf[i++] = n[1];
    if(octave < 0)
        buf[i++] = '-';
    buf[i++] = static_cast<char>('0' + (octave < 0 ? -octave : octave));
    buf[i]   = '\0';
    return buf;
}

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    (void)in;
    for(size_t i = 0; i < size; i++)
    {
        float amp = env.Process(gate);
        float s   = osc.Process() * amp;
        out[0][i] = s;
        out[1][i] = s;
    }
}

void HandleNoteOn(const NoteOnEvent& n)
{
    if(n.velocity == 0) // running-status note-off
    {
        gate = false;
        hw.PrintLine("NoteOff note=%d %s (running-status)",
                     n.note,
                     NoteName(n.note));
        return;
    }
    float freq = mtof(n.note);
    osc.SetFreq(freq);
    gate = true;
    hw.PrintLine("NoteOn  note=%d %-3s vel=%d  freq=%d Hz",
                 n.note,
                 NoteName(n.note),
                 n.velocity,
                 static_cast<int>(freq));
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(board::kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    const float sr = hw.AudioSampleRate();
    osc.Init(sr);
    osc.SetWaveform(Oscillator::WAVE_SIN);
    osc.SetAmp(0.5f);

    env.Init(sr);
    env.SetTime(ADSR_SEG_ATTACK, 0.005f);
    env.SetTime(ADSR_SEG_DECAY, 0.05f);
    env.SetSustainLevel(0.8f);
    env.SetTime(ADSR_SEG_RELEASE, 0.08f);

    MidiUartHandler::Config midi_cfg; // defaults: USART1, PB6 TX / PB7 RX
    midi.Init(midi_cfg);
    midi.StartReceive();

    hw.StartLog(false);
    status.Init();

    hw.StartAudio(AudioCallback);

    hw.PrintLine("=== Rodeo 03_midi_mono_sine ===");
    hw.PrintLine("MIDI IN on USART1 — play a keyboard; status LED lights while a "
                 "note sounds");

    for(;;)
    {
        midi.Listen();
        while(midi.HasEvents())
        {
            MidiEvent e = midi.PopEvent();
            switch(e.type)
            {
                case NoteOn: HandleNoteOn(e.AsNoteOn()); break;
                case NoteOff:
                {
                    gate       = false;
                    uint8_t nn = e.AsNoteOff().note;
                    hw.PrintLine("NoteOff note=%d %s", nn, NoteName(nn));
                    break;
                }
                default: break;
            }
        }
        // Status LED mirrors the voice gate: lit while a note is held.
        status.Set(gate);
    }
}
