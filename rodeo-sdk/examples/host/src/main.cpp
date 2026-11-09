// The host protocol (common/host.h) in a firmware small enough to read in one
// go: a drone, off until something turns it on, with six parameters.
//
//   pitch, level, cutoff, resonance   the first pot bank's four pots
//   shape, on                         the computer only
//
// Everything about the parameters a computer needs is in kParams: the Host
// sends that table in its hello, so tools/rodeo-console shows a panel for this
// firmware without knowing it exists. The App below is the firmware's side:
// reading and setting a parameter by its id.
//
// MIDI notes play it too (note on: that pitch, on; note off: off), through the
// firmware's own handler, beside the protocol: the Host takes SysEx and nothing
// else. A pot takes over its parameter once it moves, whatever set it last.
//
// The computer's dfu command reboots into the bootloader for the next
// `make program-dfu`. There is no serial port: MIDI is all it needs.
#include <stdio.h>

#include "daisy_seed.h"
#include "daisysp.h"
#include "audio_4x4.h"
#include "dfu.h"
#include "host.h"
#include "host_usb.h"
#include "sat_chain.h"
#include "sat_chain_port.h"
#include "tusb.h"
#include "usb_port.h"

#ifndef BUILD_ID
#define BUILD_ID "unknown"
#endif

using namespace daisy;

namespace
{
DaisySeed           hw;
CpuLoadMeter        cpu;
rodeo::SatChain     chain;
rodeo::SatChainPort chain_port;
rodeo::Host         host;
uint8_t             host_memory[rodeo::Host::kMemory] DSY_SDRAM_BSS;

daisysp::Oscillator osc;
daisysp::Svf        svf;

enum Id : uint8_t
{
    kPitch,
    kLevel,
    kCutoff,
    kResonance,
    kShape,
    kOn,
    kCount
};

const char* const kShapes[] = {"sine", "triangle", "saw", "square"};
const uint8_t     kWaveforms[] = {daisysp::Oscillator::WAVE_SIN, daisysp::Oscillator::WAVE_TRI,
                                  daisysp::Oscillator::WAVE_POLYBLEP_SAW,
                                  daisysp::Oscillator::WAVE_POLYBLEP_SQUARE};
const char* const kOffOn[]  = {"off", "on"};

// id, key, min, max, default, unit, names, pot
const rodeo::HostParam kParams[] = {
    {kPitch, "pitch", 24, 96, 45, "note", nullptr, 1},
    {kLevel, "level", 0, 100, 50, "%", nullptr, 2},
    {kCutoff, "cutoff", 0, 1000, 600, "\\u2030", nullptr, 3}, // per mille
    {kResonance, "resonance", 0, 1000, 200, "\\u2030", nullptr, 4},
    {kShape, "shape", 0, 3, 2, "", kShapes, 0},
    {kOn, "on", 0, 1, 0, "", kOffOn, 0},
};

// The parameters, as the audio callback reads them
volatile int value[kCount];

struct App : rodeo::HostApp
{
    int  Get(const rodeo::HostParam& p) override { return value[p.id]; }
    void Set(const rodeo::HostParam& p, int v) override { value[p.id] = v; }
} app;

void Audio(AudioHandle::InputBuffer, AudioHandle::OutputBuffer out, size_t size)
{
    cpu.OnBlockStart();
    osc.SetFreq(daisysp::mtof((float)value[kPitch]));
    osc.SetWaveform(kWaveforms[value[kShape]]);
    // 20 Hz to 20 kHz, exponentially
    svf.SetFreq(20.f * powf(1000.f, value[kCutoff] / 1000.f));
    svf.SetRes(value[kResonance] / 1000.f * 0.95f);
    float level = value[kOn] ? value[kLevel] / 100.f * 0.5f : 0.f;
    for(size_t i = 0; i < size; i++)
    {
        svf.Process(osc.Process());
        float s = svf.Low() * level;
        for(int ch = 0; ch < 4; ch++)
            out[ch][i] = s;
    }
    cpu.OnBlockEnd();
}

// The first pot bank: a pot takes over its parameter once it moves 1 %
void Pots()
{
    static int      bank = -1;
    static bool     read = false; // where the pots are, from a scan since it was found
    static uint32_t found_at;
    static int      last[4];
    if(chain.Changed())
        bank = -1;
    if(bank < 0)
    {
        for(int i = 0; i < chain.Satellites() && bank < 0; i++)
            if(chain.Satellite(i).kind == SAT_KIND_POT_BANK_4)
                bank = i;
        if(bank < 0)
            return;
        read     = false;
        found_at = chain.GoodScans();
    }
    // A satellite reads 0 until its first scan comes back: wait for one, or
    // every pot would seem to have moved
    if(!read)
    {
        if(chain.GoodScans() == found_at)
            return;
        for(int s = 0; s < 4; s++)
            last[s] = (int)(chain.Pot(bank, s) * 1000.f);
        read = true;
    }
    for(const rodeo::HostParam& p : kParams)
    {
        if(!p.pot)
            continue;
        int s = p.pot - 1;
        int x = (int)(chain.Pot(bank, s) * 1000.f);
        if(x > last[s] - 10 && x < last[s] + 10)
            continue;
        last[s] = x;
        int v   = p.min + (x * (p.max - p.min) + 500) / 1000;
        if(v == value[p.id])
            continue;
        value[p.id] = v;
        char said[32];
        snprintf(said, sizeof said, "%s: %d", p.key, value[p.id]);
        host.Say(said);
    }
}

// MIDI the firmware handles itself: notes
void Midi(const uint8_t pk[4])
{
    uint8_t type = pk[1] & 0xF0;
    if(type == 0x90 && pk[3] > 0)
    {
        value[kPitch] = pk[2] < 24 ? 24 : pk[2] > 96 ? 96 : pk[2];
        value[kOn]    = 1;
    }
    else if((type == 0x80 || type == 0x90) && pk[2] == value[kPitch])
        value[kOn] = 0;
}
} // namespace

int main(void)
{
    hw.Init();
    rodeo::InitAudio4x4(hw);
    float rate = hw.AudioSampleRate();
    cpu.Init(rate, hw.AudioBlockSize());
    osc.Init(rate);
    osc.SetAmp(1.f);
    svf.Init(rate);
    svf.SetDrive(0.5f); // DaisySP's default is unstable (common/tests/svf_stability_gtest.cpp)
    for(const rodeo::HostParam& p : kParams)
        value[p.id] = p.def;

    chain.Init(rodeo::SatChain::Config(), System::GetUs());
    chain_port.Init();

    rodeo::Host::Config cfg;
    cfg.firmware    = "host-example";
    cfg.build       = BUILD_ID;
    cfg.params      = kParams;
    cfg.count       = sizeof kParams / sizeof kParams[0];
    cfg.app         = &app;
    cfg.chain       = &chain;
    cfg.memory      = host_memory;
    cfg.memory_size = sizeof host_memory;
    host.Init(cfg);

    rodeo::UsbPortInit();
    tusb_rhport_init_t dev_init = {TUSB_ROLE_DEVICE, TUSB_SPEED_AUTO};
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    rodeo::StartAudio4x4(hw, Audio);

    uint32_t blink = 0;
    bool     led   = false;
    for(;;)
    {
        tud_task();
        uint8_t pk[4];
        while(tud_midi_available() && tud_midi_packet_read(pk))
            if(!host.Receive(pk, System::GetNow()))
                Midi(pk);
        chain_port.Process(chain);
        Pots();
        rodeo::HostUsbTask(host, System::GetNow(), (int)(cpu.GetAvgCpuLoad() * 100.f));
        if(host.DfuRequested())
        {
            rodeo::HostUsbFinish(); // the reply leaves before the reboot
            rodeo::StopAudio4x4(hw);
            ResetToDfu();
        }

        // Slow while no computer has configured USB, fast once one has
        uint32_t now = System::GetNow();
        if(now - blink >= (tud_mounted() ? 125u : 500u))
        {
            blink = now;
            hw.SetLed(led = !led);
        }
    }
}
