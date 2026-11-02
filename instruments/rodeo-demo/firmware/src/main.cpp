// rodeo-demo: a small synth and sampler for Rev B.
//
// Played from a MIDI keyboard over USB (or the TRS MIDI jack), shaped from a pot
// bank and an encoder bank on the satellite chain (panel.h), and heard on the
// line outs and, over the same USB cable, on the computer:
//
//   OUT 1/2       the demo                 USB in 1/2   the demo, to record
//   OUT 3/4       what the computer plays  USB in 3/4   inputs 1/2
//                 to USB channels 1/2
//
// Samples come from the SD card (samples.h). The serial port on the same USB
// cable says what each control just did, 's' prints the state, and 'B' reboots
// into the bootloader for `make program-dfu`.
//
// What runs where: the audio callback (1/3 ms blocks) makes the sound and moves USB
// audio; the main loop services USB, MIDI, the chain, the panel and the sample
// loader, each a little at a time so none holds up the others.
#include "daisy_seed.h"
#include "board.h"
#include "board_daisy.h"
#include "audio_4x4.h"
#include "dfu.h"
#include "sat_chain.h"
#include "sat_chain_port.h"
#include "tusb.h"
#include "usb_port.h"

#include "panel.h"
#include "samples.h"
#include "synth.h"
#include "usb_audio.h"

using namespace daisy;

namespace
{
// 16 frames, 1/3 ms. The USB interrupt waits behind the audio callback (audio is
// the higher priority), and with 1 ms blocks a few voices held it off long enough
// to miss isochronous packets: about 36 underruns a second.
constexpr size_t kBlock = 16;

DaisySeed           hw;
MidiUartHandler     trs_midi;
rodeo::SatChain     chain;
rodeo::SatChainPort chain_port;
CpuLoadMeter        cpu;

synth::Params params; // written by the main loop, read by the audio callback

float from_computer[usb_audio::kChannels][kBlock];

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t frames)
{
    cpu.OnBlockStart();
    float* pc[usb_audio::kChannels]
        = {from_computer[0], from_computer[1], from_computer[2], from_computer[3]};
    usb_audio::Receive(pc, frames);

    synth::Process(params, out[0], out[1], frames);
    for(size_t i = 0; i < frames; i++)
    {
        out[2][i] = from_computer[0][i];
        out[3][i] = from_computer[1][i];
    }

    const float* to_computer[usb_audio::kChannels] = {out[0], out[1], in[0], in[1]};
    usb_audio::Send(to_computer, frames);
    cpu.OnBlockEnd();
}

//------------- MIDI, from USB and the TRS jack alike -------------//

void Midi(uint8_t status, uint8_t d1, uint8_t d2)
{
    switch(status & 0xF0)
    {
        case 0x90: synth::NoteOn(d1, d2); break;
        case 0x80: synth::NoteOff(d1); break;
        case 0xE0: synth::PitchBend(2.f * ((d1 | (d2 << 7)) - 8192) / 8192.f); break;
        case 0xB0:
            if(d1 == 64)
                synth::Sustain(d2 >= 64);
            else if(d1 == 120 || d1 == 123) // all sound off, all notes off
                synth::AllNotesOff();
            break;
        default: break;
    }
}

void MidiTask()
{
    uint8_t packet[4];
    while(tud_midi_available() && tud_midi_packet_read(packet))
        Midi(packet[1], packet[2], packet[3]);

    trs_midi.Listen();
    while(trs_midi.HasEvents())
    {
        MidiEvent e = trs_midi.PopEvent();
        switch(e.type)
        {
            case NoteOn: Midi(0x90, e.data[0], e.data[1]); break;
            case NoteOff: Midi(0x80, e.data[0], e.data[1]); break;
            case ControlChange: Midi(0xB0, e.data[0], e.data[1]); break;
            case PitchBend: Midi(0xE0, e.data[0], e.data[1]); break;
            default: break;
        }
    }
}

//------------- The serial port -------------//

void Print(const char* s)
{
    if(!tud_cdc_connected())
        return;
    tud_cdc_write_str(s);
    tud_cdc_write_flush();
}

void PrintState()
{
    char                  buf[600];
    samples::Stats        s = samples::GetStats();
    usb_audio::Stats      u = usb_audio::GetStats();
    const char* const     layers[] = {"synth", "sampler", "both"};
    snprintf(buf, sizeof buf,
             "\r\nlayer %s, shape %s, sound %d of %d; attack %d ms, release %d ms; %d voices sounding; CPU %d %% (peak %d %%)\r\n"
             "chain: %d satellites, %s; %lu good scans, %lu bad\r\n"
             "card: %s; %d sounds, %d of %d samples loaded (%d skipped), %lu KB in %lu ms%s\r\n"
             "USB audio: computer %s, recording %s; %lu underruns, %lu overruns\r\n",
             layers[(int)params.layer], synth::ShapeName(params.shape), params.sound + 1,
             s.sounds, (int)(params.attack * 1000.f), (int)(params.release * 1000.f), synth::Active(), (int)(cpu.GetAvgCpuLoad() * 100.f),
             (int)(cpu.GetMaxCpuLoad() * 100.f), chain.Satellites(),
             panel::Found() ? "pot bank and encoder bank found" : "panel incomplete",
             (unsigned long)chain.GoodScans(), (unsigned long)chain.BadScans(),
             s.card ? "mounted" : "none", s.sounds, s.loaded, s.samples, s.skipped,
             (unsigned long)(s.bytes_used / 1024), (unsigned long)s.load_ms,
             s.loading ? " so far" : "", u.playing ? "playing" : "idle",
             u.recording ? "on" : "off", (unsigned long)u.underruns, (unsigned long)u.overruns);
    Print(buf);
    cpu.Reset();
}

void SerialTask()
{
    static bool was_connected = false;
    bool        connected     = tud_cdc_connected();
    if(connected && !was_connected)
    {
        Print("\r\nrodeo-demo. s = state, B = reboot into the bootloader\r\n");
        PrintState();
    }
    was_connected = connected;

    while(tud_cdc_available())
    {
        char c = (char)tud_cdc_read_char();
        if(c == 's')
            PrintState();
        else if(c == 'B')
        {
            Print("rebooting into the bootloader (DFU on the USB-C)\r\n");
            System::Delay(50);
            rodeo::StopAudio4x4(hw);
            ResetToDfu();
        }
    }
}
} // namespace

int main(void)
{
    hw.Init();
    rodeo::InitAudio4x4(hw, kBlock);
    float rate = hw.AudioSampleRate();
    synth::Init(rate);
    cpu.Init(rate, kBlock);

    samples::Mount(); // lists the sounds; Step() loads them

    MidiUartHandler::Config midi_cfg;
    midi_cfg.transport_config.periph = UartHandler::Config::Peripheral::USART_1;
    midi_cfg.transport_config.rx     = board::ToDaisy(board::pin::kMidiRx);
    midi_cfg.transport_config.tx     = board::ToDaisy(board::pin::kMidiTx);
    trs_midi.Init(midi_cfg);
    trs_midi.StartReceive();

    chain.Init(rodeo::SatChain::Config(), System::GetUs());
    chain_port.Init();

    rodeo::UsbPortInit();
    tusb_rhport_init_t dev_init = {TUSB_ROLE_DEVICE, TUSB_SPEED_AUTO};
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    rodeo::StartAudio4x4(hw, AudioCallback);

    bool     loading = true;
    uint32_t blink   = 0;
    for(;;)
    {
        tud_task();
        SerialTask();
        MidiTask();
        chain_port.Process(chain);
        if(const char* said = panel::Update(chain, params, loading))
        {
            Print(said);
            Print("\r\n");
        }
        if(loading && !(loading = samples::Step()))
            PrintState(); // the card is all in

        // The Seed's own LED: a heartbeat, quicker once the computer has found us
        uint32_t now = System::GetNow();
        if(now - blink >= (tud_mounted() ? 250u : 500u))
        {
            blink = now;
            static bool on = false;
            hw.SetLed(on = !on);
        }
    }
}
