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
// Samples come from the SD card (samples.h), which the computer can have as a
// USB drive in disk mode (disk.h): hold E1 and E4 for a second, or 'd' on the
// serial port; eject the drive to come back. The serial port on the same USB
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

#include "disk.h"
#include "panel.h"
#include "presets.h"
#include "samples.h"
#include "state_line.h"
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
    char                  buf[800];
    samples::Stats        s = samples::GetStats();
    usb_audio::Stats      u = usb_audio::GetStats();
    const char* const     layers[] = {"synth", "sampler", "both"};
    snprintf(buf, sizeof buf,
             "\r\npreset %d of %d%s%s; layer %s, shape %s, sound %d of %d\r\n"
             "cutoff %d %%, resonance %d %%, envelope %d %%, attack %d ms, release %d ms\r\n"
             "%d voices sounding; CPU %d %% (peak %d %%)\r\n"
             "chain: %d satellites, %s; %lu good scans, %lu bad\r\n"
             "card: %s; %d sounds, %d of %d samples loaded (%d skipped), %lu KB in %lu ms%s\r\n"
             "USB audio: computer %s, recording %s; %lu underruns, %lu overruns\r\n",
             panel::Preset() + 1, presets::Count(), panel::Preset() >= 0 ? ": " : "",
             presets::Name(panel::Preset()), layers[(int)params.layer],
             synth::ShapeName(params.shape), params.sound + 1, s.sounds,
             (int)lroundf(params.cutoff * 100.f), (int)lroundf(params.resonance * 100.f),
             (int)lroundf(params.env_amount * 100.f), (int)(params.attack * 1000.f),
             (int)(params.release * 1000.f), synth::Active(), (int)(cpu.GetAvgCpuLoad() * 100.f),
             (int)(cpu.GetMaxCpuLoad() * 100.f), chain.Satellites(),
             panel::Found() ? "pot bank and encoder bank found" : "panel incomplete",
             (unsigned long)chain.GoodScans(), (unsigned long)chain.BadScans(),
             disk::Active() ? "the computer's (disk mode)" : s.card ? "mounted" : "none", s.sounds, s.loaded, s.samples, s.skipped,
             (unsigned long)(s.bytes_used / 1024), (unsigned long)s.load_ms,
             s.loading ? " so far" : "", u.playing ? "playing" : "idle",
             u.recording ? "on" : "off", (unsigned long)u.underruns, (unsigned long)u.overruns);
    Print(buf);
    if(presets::Problems()[0])
    {
        Print("preset lines skipped:\r\n");
        Print(presets::Problems());
    }
    cpu.Reset();
}

// Writes all of s, a piece at a time: the CDC buffer is 512 bytes and a state line
// with a long chain is more. Gives up (the rest unsent) if the computer stops
// reading for 50 ms.
void Send(const char* s, size_t n)
{
    uint32_t start = System::GetNow();
    while(n && tud_cdc_connected() && System::GetNow() - start < 50)
    {
        uint32_t w = tud_cdc_write(s, n);
        s += w;
        n -= w;
        tud_cdc_write_flush();
        if(n)
            tud_task();
    }
}

// The `#S` state line for tools/rodeo-console: the state as one line of JSON
// (planning/RODEO_CONSOLE_STATE_LINE.md). Unlike PrintState() it leaves the CPU
// peak alone, so polling doesn't wipe it. Returns a hash of everything but the
// CPU figures and scan counts, which change all the time: the stream sends while
// that changes.
uint32_t StateLine(bool send)
{
    static char       buf[4096];
    samples::Stats    s        = samples::GetStats();
    const char* const layers[] = {"synth", "sampler", "both"};
    int               card     = disk::Active() ? 2 : s.card ? 1 : 0;
    // The preset's name, made safe for a JSON string
    char        name[64];
    const char* from = panel::Preset() >= 0 ? presets::Name(panel::Preset()) : "";
    size_t      m    = 0;
    for(; *from && m < sizeof name - 1; from++)
        if(*from != '"' && *from != '\\' && (unsigned char)*from >= 0x20)
            name[m++] = *from;
    name[m] = 0;

    int len = snprintf(buf, sizeof buf,
                       "#S {\"preset\":%d,\"name\":\"%s\",\"layer\":\"%s\",\"shape\":\"%s\","
                       "\"sound\":%d,\"nsounds\":%d,\"cut\":%d,\"res\":%d,\"env\":%d,\"atk\":%d,"
                       "\"rel\":%d,\"voices\":%d,\"card\":%d,\"loaded\":%d,\"samples\":%d,"
                       "\"skipped\":%d,\"kb\":%lu,\"loadms\":%lu,",
                       panel::Preset(), name, layers[(int)params.layer],
                       synth::ShapeName(params.shape), params.sound, s.sounds,
                       (int)lroundf(params.cutoff * 100.f), (int)lroundf(params.resonance * 100.f),
                       (int)lroundf(params.env_amount * 100.f), (int)(params.attack * 1000.f),
                       (int)(params.release * 1000.f), synth::Active(), card, s.loaded,
                       s.samples, s.skipped, (unsigned long)(s.bytes_used / 1024),
                       (unsigned long)s.load_ms);
    size_t c = state_line::Chain(chain, panel::Found(), buf + len, sizeof buf - len - 96);
    if(c == 0)
        c = snprintf(buf + len, sizeof buf - len - 96, "\"chain\":{\"error\":\"too long\"");
    len += c;

    uint32_t hash = 2166136261u; // FNV-1a
    for(int i = 0; i < len; i++)
        hash = (hash ^ (uint8_t)buf[i]) * 16777619u;

    len += snprintf(buf + len, sizeof buf - len,
                    ",\"good\":%lu,\"bad\":%lu},\"cpu\":%d,\"peak\":%d}\r\n",
                    (unsigned long)chain.GoodScans(), (unsigned long)chain.BadScans(),
                    (int)(cpu.GetAvgCpuLoad() * 100.f), (int)(cpu.GetMaxCpuLoad() * 100.f));
    if(send)
        Send(buf, len);
    return hash;
}

// J: the state line about 20 times a second while anything changes, quiet once
// nothing has for a second
bool     streaming = false;
uint32_t last_hash = 0, last_change = 0, last_sent = 0;

void StreamTask()
{
    uint32_t now = System::GetNow();
    if(!streaming || now - last_sent < 50)
        return;
    last_sent     = now;
    uint32_t hash = StateLine(false);
    if(hash != last_hash)
    {
        last_hash   = hash;
        last_change = now;
    }
    if(now - last_change < 1000)
        StateLine(true);
}

bool disk_wanted = false; // asked for on the serial port

// Hand the card to the computer, saying so first: the serial port closes as the
// Rodeo re-enumerates.
bool EnterDisk()
{
    Print("disk mode: the serial port will close; the SD card comes back as a drive\r\n");
    System::Delay(50);
    if(disk::Enter())
        return true;
    Print("no SD card to hand over\r\n");
    return false;
}

void SerialTask()
{
    static bool was_connected = false;
    bool        connected     = tud_cdc_connected();
    if(connected && !was_connected)
    {
        if(disk::Active())
            Print("\r\nrodeo-demo, DISK MODE: the SD card is the computer's. Eject the drive\r\n"
                  "to hand it back. s = state, n = leave now (without the eject)\r\n");
        else
            Print("\r\nrodeo-demo. s = state, d = disk mode (the SD card as a USB drive),\r\n"
                  "j = state as JSON, J = stream it, B = reboot into the bootloader\r\n");
        PrintState();
    }
    if(!connected)
        streaming = false;
    was_connected = connected;

    while(tud_cdc_available())
    {
        char c = (char)tud_cdc_read_char();
        if(c == 's')
            PrintState();
        else if(c == 'j')
            StateLine(true);
        else if(c == 'J')
        {
            streaming   = !streaming;
            last_change = System::GetNow(); // send straight away
        }
        else if(c == 'd' && !disk::Active())
            disk_wanted = true;
        else if(c == 'n' && disk::Active())
            disk::Leave();
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
    if(presets::Load() > 0)
        panel::LoadPreset(0, params);

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
        StreamTask();
        MidiTask();
        chain_port.Process(chain);
        state_line::Track(chain);
        panel::Status status = disk::Active() ? panel::Status::Disk
                               : loading      ? panel::Status::Loading
                                              : panel::Status::Playing;
        if(const char* said = panel::Update(chain, params, status))
        {
            Print(said);
            Print("\r\n");
        }
        if((panel::DiskRequested() || disk_wanted) && !disk::Active() && EnterDisk())
            loading = false;
        disk_wanted = false;
        if(disk::Task())
        {
            // The card is back, maybe with other sounds on it: load them afresh.
            loading = true;
            presets::Load(); // maybe changed too; the sound playing stays
            if(params.sound >= samples::Sounds())
                params.sound = 0;
            if(samples::Sounds() == 0)
                params.layer = synth::Layer::Synth;
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
