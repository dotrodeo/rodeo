// rodeo-demo over MIDI: rodeo-demo's side of the SDK's host protocol, and its
// CC map. See remote.h and planning/RODEO_DEMO_MIDI_PROTOCOL.md.
#include "remote.h"
#include <math.h>
#include <stdio.h>
#include "daisy_seed.h"
#include "host.h"
#include "host_usb.h"
#include "disk.h"
#include "panel.h"
#include "params.h"
#include "presets.h"
#include "samples.h"

#ifndef BUILD_ID
#define BUILD_ID "unknown"
#endif

namespace remote
{
namespace
{
// rodeo-demo's own commands, in the range the SDK leaves to a firmware
enum Cmd : uint8_t
{
    kPresets = 0x20, // the preset list
    kLoad    = 0x21, // load preset n: two 7-bit bytes, low first
    kSounds  = 0x22, // the sounds on the card
};

void (*say)(const char*) = nullptr;
synth::Params* params    = nullptr;

rodeo::Host host;
uint8_t     memory[rodeo::Host::kMemory] DSY_SDRAM_BSS; // out of DTCM, which holds the stack

// What a set did, to the serial port and back to the computer
void Tell(const char* s)
{
    if(s)
        say(s);
}

struct App : rodeo::HostApp
{
    int Get(const rodeo::HostParam& p) override { return params::Get(p, *params); }
    // As if its control had moved there (panel::Set): a pot's is held until the pot moves
    void Set(const rodeo::HostParam& p, int v) override { Tell(panel::Set(p.id, v, *params)); }
    int  Max(const rodeo::HostParam& p) override { return params::Max(p); }
    uint32_t Held() override { return panel::Held(); }

    void Hello(rodeo::HostWriter& w) override { w.Add(",\"voices\":%d", synth::kVoices); }

    void State(rodeo::HostWriter& w) override
    {
        samples::Stats s    = samples::GetStats();
        int            card = disk::Active() ? 2 : s.card ? 1 : 0;
        w.Add(",\"preset\":%d,\"name\":\"", panel::Preset());
        w.Name(panel::Preset() >= 0 ? presets::Name(panel::Preset()) : "");
        w.Add("\",\"voices\":%d,\"card\":%d,\"sounds\":%d,\"loaded\":%d,\"samples\":%d,"
              "\"panel\":%d",
              synth::Active(), card, s.sounds, s.loaded, s.samples, panel::Found() ? 1 : 0);
    }

    bool Command(uint8_t cmd, const uint8_t* data, size_t len, rodeo::Host& h) override
    {
        switch(cmd)
        {
            case kPresets:
            {
                rodeo::HostWriter& w = h.Begin(cmd | rodeo::Host::kReply);
                w.Add("{\"presets\":[");
                for(int i = 0; i < presets::Count(); i++)
                {
                    w.Add("%s\"", i ? "," : "");
                    w.Name(presets::Name(i));
                    w.Add("\"");
                }
                w.Add("],\"current\":%d}", panel::Preset());
                h.End();
                return true;
            }
            case kLoad:
                if(len >= 2)
                {
                    int i = data[0] | (data[1] << 7);
                    if(const char* said = panel::LoadPreset(i, *params))
                        say(said);
                    else
                        h.Error("no preset %d", i);
                }
                return true;
            case kSounds:
            {
                rodeo::HostWriter& w = h.Begin(cmd | rodeo::Host::kReply);
                w.Add("{\"sounds\":[");
                for(int i = 0; i < samples::Sounds(); i++)
                {
                    const samples::Sound& s = samples::GetSound(i);
                    w.Add("%s{\"name\":\"", i ? "," : "");
                    w.Name(s.name);
                    w.Add("\",\"kit\":%d,\"ready\":%d}", s.kit ? 1 : 0, s.ready ? 1 : 0);
                }
                w.Add("]}");
                h.End();
                return true;
            }
        }
        return false;
    }
} app;
} // namespace

void Init(void (*s)(const char*), synth::Params& p, const rodeo::SatChain& chain)
{
    say    = s;
    params = &p;
    params::Init();
    rodeo::Host::Config cfg;
    cfg.firmware    = "rodeo-demo";
    cfg.build       = BUILD_ID;
    cfg.params      = params::Table();
    cfg.count       = params::Count();
    cfg.app         = &app;
    cfg.chain       = &chain;
    cfg.memory      = memory;
    cfg.memory_size = sizeof memory;
    host.Init(cfg);
}

bool Packet(const uint8_t packet[4])
{
    return host.Receive(packet, daisy::System::GetNow());
}

bool Cc(int cc, int v)
{
    float x  = v / 127.f;
    int   id = -1, value = 0;
    // Choices: the 128 values split evenly over them
    auto pick = [&](int n) { return n > 0 ? (v * n) / 128 : 0; };
    switch(cc)
    {
        case 74: id = 0, value = (int)lroundf(x * 1000.f); break;
        case 71: id = 1, value = (int)lroundf(x * 1000.f); break;
        case 79: id = 2, value = (int)lroundf(x * 1000.f); break;
        case 72: id = 3, value = (int)lroundf(5.f * powf(800.f, x)); break; // as pot 8
        case 73: id = 4, value = (int)lroundf(powf(4000.f, x)); break;      // as pot 5
        case 75: id = 16, value = (int)lroundf(powf(10000.f, x)); break; // as pot 6
        case 94: id = 5, value = (int)lroundf(x * 50.f); break;
        case 91: id = 7, value = (int)lroundf(x * 100.f); break;
        case 102: id = 6, value = pick(5) - 2; break;
        case 103: id = 11, value = pick(synth::kShapes); break;
        case 104: id = 10, value = pick(3); break;
        case 105: id = 12, value = pick(samples::Sounds()); break;
        case 106: id = 8, value = v >= 64; break;
        case 107: id = 9, value = pick(3); break;
        default: return false;
    }
    Tell(panel::Set(id, value, *params));
    return true;
}

void Task(int cpu)
{
    rodeo::HostUsbTask(host, daisy::System::GetNow(), cpu);
}

void Said(const char* s)
{
    host.Say(s);
}

bool DfuRequested()
{
    if(!host.DfuRequested())
        return false;
    rodeo::HostUsbFinish(); // the reply leaves before the reboot
    return true;
}
} // namespace remote
