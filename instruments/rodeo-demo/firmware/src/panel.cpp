// The panel. See panel.h.
#include "panel.h"

#include <math.h>
#include <stdio.h>
#include "daisy_seed.h"
#include "params.h"
#include "presets.h"
#include "samples.h"

namespace panel
{
namespace
{
// Where each bank is on the chain, or -1: the first of its kind after the Seed is
// bank 1, the next bank 2
constexpr int kBanks = 2;
int  pot_bank[kBanks] = {-1, -1}, enc_bank[kBanks] = {-1, -1};
char said[128];

struct Encoder
{
    bool was_down = false;
    bool turned   = false; // while held
};
Encoder enc[4 * kBanks];

// E1 + E4 held: disk mode. While both are down neither taps when let go.
constexpr uint32_t kHoldMs = 1000;
bool               both_down = false, both_fired = false, disk_requested = false;
uint32_t           both_since = 0;

// After a preset: which pots' parameters it holds, and where each pot was when it
// did (-1 until the pot bank is read). A pot moved further than kTakeover from
// there takes its parameter back. The anchors come from a scan made after the
// preset loaded: at power-up the preset is in before the first real readings.
constexpr int   kPots       = 4 * kBanks;
constexpr float kTakeover   = 0.02f;
unsigned        pot_held    = 0;
float           anchor[kPots] = {-1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f};
uint32_t        anchor_scan = 0; // the good scan to take them from, or 0: not chosen yet
int             preset      = -1;

int Wrap(int x, int n)
{
    return n > 0 ? ((x % n) + n) % n : 0;
}

float Clamp(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

// An encoder stepping through n values from lo to hi, evenly in ratio, and with
// off (0) below them if it has one. It carries on from the step nearest wherever
// the value was put (a preset, MIDI), so there's nothing to keep in step.
float Step(float v, float lo, float hi, int n, bool off, int steps)
{
    int s;
    if(off && v <= 0.f)
        s = -1;
    else
        s = (int)lroundf((n - 1) * logf(Clamp(v, lo, hi) / lo) / logf(hi / lo));
    s = (int)Clamp(s + steps, off ? -1 : 0, n - 1);
    return s < 0 ? 0.f : lo * powf(hi / lo, s / (float)(n - 1));
}

const char* LayerName(synth::Layer l)
{
    return l == synth::Layer::Synth ? "synth" : l == synth::Layer::Sampler ? "sampler" : "both";
}

const char* FxName(synth::Fx f)
{
    return f == synth::Fx::Chorus ? "chorus" : f == synth::Fx::Reverb ? "reverb" : "chorus + reverb";
}

void Find(rodeo::SatChain& chain)
{
    int pots = 0, encs = 0;
    for(int b = 0; b < kBanks; b++)
        pot_bank[b] = enc_bank[b] = -1;
    for(int i = 0; i < chain.Satellites(); i++)
    {
        uint8_t kind = chain.Satellite(i).kind;
        if(kind == SAT_KIND_POT_BANK_4 && pots < kBanks)
            pot_bank[pots++] = i;
        if(kind == SAT_KIND_ENC_BANK_4 && encs < kBanks)
            enc_bank[encs++] = i;
    }
}

// The pots on the chain, as PotBits
unsigned Live()
{
    return (pot_bank[0] >= 0 ? 0x0Fu : 0u) | (pot_bank[1] >= 0 ? 0xF0u : 0u);
}

// Pot k (0-7) at x (0..1) into its parameter
void Pot(int k, float x, synth::Params& p)
{
    switch(k)
    {
        case 0: p.cutoff = x; break;
        case 1: p.resonance = x; break;
        case 2: p.env_amount = x; break;
        case 3: p.drive = x; break;
        case 4: p.attack = 0.001f * powf(4000.f, x); break;  // 1 ms .. 4 s
        case 5: p.decay = 0.001f * powf(10000.f, x); break;  // 1 ms .. 10 s
        case 6: p.sustain = x; break;
        case 7: p.release = 0.005f * powf(800.f, x); break;  // 5 ms .. 4 s
    }
}

int Ms(float s)
{
    return (int)lroundf(s * 1000.f);
}

void Turn(int e, int steps, bool held, synth::Params& p)
{
    switch(e)
    {
        case 0:
            if(held)
            {
                if(presets::Count() > 0)
                    LoadPreset(Wrap(preset + steps, presets::Count()), p);
                else
                    snprintf(said, sizeof said, "no presets on the card");
            }
            else if(p.layer == synth::Layer::Synth)
            {
                p.shape = Wrap(p.shape + steps, synth::kShapes);
                snprintf(said, sizeof said, "shape: %s", synth::ShapeName(p.shape));
            }
            else if(samples::Sounds() > 0)
            {
                p.sound = Wrap(p.sound + steps, samples::Sounds());
                const samples::Sound& s = samples::GetSound(p.sound);
                snprintf(said, sizeof said, "sound %d: %s%s%s", p.sound + 1, s.name,
                         s.kit ? " (kit)" : "", s.ready ? "" : " (loading)");
            }
            break;
        case 1:
            if(held)
            {
                p.octave = (int)Clamp(p.octave + steps, -2, 2);
                snprintf(said, sizeof said, "octave: %+d", p.octave);
            }
            else
            {
                p.detune = Clamp(p.detune + 0.02f * steps, 0.f, 0.5f);
                snprintf(said, sizeof said, "detune: %d cents", (int)lroundf(p.detune * 100.f));
            }
            break;
        case 2:
            p.hpf = Step(p.hpf, 20.f, 2000.f, 21, true, steps);
            if(p.hpf > 0.f)
                snprintf(said, sizeof said, "high-pass: %d Hz", (int)lroundf(p.hpf));
            else
                snprintf(said, sizeof said, "high-pass: off");
            break;
        case 3:
            p.fx_mix = Clamp(p.fx_mix + 0.05f * steps, 0.f, 1.f);
            snprintf(said, sizeof said, "effects mix: %d %%", (int)lroundf(p.fx_mix * 100.f));
            break;
        case 4:
            p.fattack = Step(p.fattack, 0.001f, 4.f, 25, true, steps);
            if(p.fattack > 0.f)
                snprintf(said, sizeof said, "filter attack: %d ms", Ms(p.fattack));
            else
            {
                p.fattack = -1.f;
                snprintf(said, sizeof said, "filter attack: follows attack");
            }
            break;
        case 5:
            p.fdecay = Step(p.fdecay, 0.001f, 10.f, 25, false, steps);
            snprintf(said, sizeof said, "filter decay: %d ms", Ms(p.fdecay));
            break;
        case 6:
            p.fsustain = Clamp(p.fsustain + 0.05f * steps, 0.f, 1.f);
            snprintf(said, sizeof said, "filter sustain: %d %%", (int)lroundf(p.fsustain * 100.f));
            break;
        case 7:
            p.glide = Step(p.glide, 0.005f, 2.f, 19, true, steps);
            if(p.glide > 0.f)
                snprintf(said, sizeof said, "glide: %d ms%s", Ms(p.glide), p.mono ? "" : " (in mono)");
            else
                snprintf(said, sizeof said, "glide: off");
            break;
    }
}

void Tap(int e, synth::Params& p)
{
    switch(e)
    {
        case 0:
            if(samples::Sounds() == 0)
            {
                p.layer = synth::Layer::Synth;
                snprintf(said, sizeof said, "layer: synth (no samples on the card)");
                break;
            }
            p.layer = p.layer == synth::Layer::Synth     ? synth::Layer::Sampler
                      : p.layer == synth::Layer::Sampler ? synth::Layer::Both
                                                         : synth::Layer::Synth;
            snprintf(said, sizeof said, "layer: %s", LayerName(p.layer));
            break;
        case 1:
            p.detune = synth::Params().detune;
            p.octave = 0;
            snprintf(said, sizeof said, "detune and octave reset");
            break;
        case 2:
            p.lfo = !p.lfo;
            snprintf(said, sizeof said, "LFO: %s", p.lfo ? "on" : "off");
            break;
        case 3:
            p.fx = p.fx == synth::Fx::Chorus   ? synth::Fx::Reverb
                   : p.fx == synth::Fx::Reverb ? synth::Fx::Both
                                               : synth::Fx::Chorus;
            snprintf(said, sizeof said, "effects: %s", FxName(p.fx));
            break;
        case 4:
            p.fattack = -1.f;
            snprintf(said, sizeof said, "filter attack: follows attack");
            break;
        case 5:
            p.fdecay = synth::Params().fdecay;
            snprintf(said, sizeof said, "filter decay: %d ms", Ms(p.fdecay));
            break;
        case 6:
            p.fsustain = synth::Params().fsustain;
            snprintf(said, sizeof said, "filter sustain: %d %%", (int)lroundf(p.fsustain * 100.f));
            break;
        case 7:
            p.mono = !p.mono;
            snprintf(said, sizeof said, "mono: %s", p.mono ? "on" : "off");
            break;
    }
}
} // namespace

const char* Update(rodeo::SatChain& chain, synth::Params& p, Status status)
{
    bool loading = status == Status::Loading;
    said[0] = 0;
    if(chain.Changed())
        Find(chain);

    if(pot_held && anchor_scan == 0 && Live())
        anchor_scan = chain.GoodScans() + 2;
    bool    read = anchor_scan && chain.GoodScans() >= anchor_scan;
    uint8_t pot_mode = status == Status::Disk ? SAT_LED_SLOW_BLINK
                       : loading              ? SAT_LED_FAST_BLINK
                                              : SAT_LED_STEADY;
    uint8_t pot_level = synth::Active() > 0 || status != Status::Playing ? 255 : 20;
    for(int b = 0; b < kBanks; b++)
    {
        if(pot_bank[b] < 0)
            continue;
        for(int i = 0; i < 4; i++)
        {
            int      k   = 4 * b + i;
            unsigned bit = 1u << k;
            float    x   = chain.Pot(pot_bank[b], i);
            if(pot_held & bit)
            {
                if(!read)
                    continue;
                if(anchor[k] < 0.f)
                    anchor[k] = x;
                if(fabsf(x - anchor[k]) <= kTakeover)
                    continue;
                pot_held &= ~bit; // moved: the pot has it again
            }
            Pot(k, x, p);
        }
        chain.SetLed(pot_bank[b], pot_level, pot_mode);
    }

    if(enc_bank[0] >= 0)
    {
        bool first = chain.Pressed(enc_bank[0], 0), last = chain.Pressed(enc_bank[0], 3);
        if(first && last && !both_down)
        {
            both_down  = true;
            both_fired = false;
            both_since = daisy::System::GetNow();
        }
        if(both_down && !both_fired && daisy::System::GetNow() - both_since >= kHoldMs)
        {
            both_fired     = true;
            disk_requested = true;
        }
        if(both_down) // no taps from either, until both are up
            enc[0].turned = enc[3].turned = true;
        if(!first && !last)
            both_down = false;
    }

    for(int b = 0; b < kBanks; b++)
    {
        if(enc_bank[b] < 0)
            continue;
        for(int i = 0; i < 4; i++)
        {
            int      e       = 4 * b + i;
            Encoder& n       = enc[e];
            bool     down    = chain.Pressed(enc_bank[b], i);
            int      steps   = chain.EncoderSteps(enc_bank[b], i);
            int      presses = chain.Presses(enc_bank[b], i); // catches a press too quick to see
            if(steps)
            {
                Turn(e, steps, down, p);
                if(down)
                    n.turned = true;
            }
            if(!down && (n.was_down || presses > 0) && !n.turned)
                Tap(e, p);
            if(!down && !(b == 0 && both_down))
                n.turned = false;
            n.was_down = down;
        }
    }
    if(enc_bank[0] >= 0)
    {
        uint8_t mode = p.layer == synth::Layer::Synth     ? SAT_LED_STEADY
                       : p.layer == synth::Layer::Sampler ? SAT_LED_SLOW_BLINK
                                                          : SAT_LED_FAST_BLINK;
        chain.SetLed(enc_bank[0], 255, mode);
    }
    if(enc_bank[1] >= 0) // bright in mono
        chain.SetLed(enc_bank[1], p.mono ? 255 : 20, SAT_LED_STEADY);
    return said[0] ? said : nullptr;
}

const char* LoadPreset(int i, synth::Params& p)
{
    if(i < 0 || i >= presets::Count())
        return nullptr;
    preset   = i;
    pot_held = presets::Apply(i, p, Live());
    for(auto& a : anchor)
        a = -1.f;
    anchor_scan = 0;
    if(const char* m = presets::MissingSound())
        snprintf(said, sizeof said, "preset %d: %s (no sound \"%.24s\" on the card)", i + 1,
                 presets::Name(i), m);
    else
        snprintf(said, sizeof said, "preset %d: %s", i + 1, presets::Name(i));
    return said;
}

const char* Set(int id, int v, synth::Params& p)
{
    const params::Info* info = params::Find(id);
    if(!info)
        return nullptr;
    params::Put(*info, v, p);
    if(info->pot)
    {
        // Held from where the pot is on the next good scan, as after a preset
        unsigned bit = 1u << (info->pot - 1);
        pot_held |= bit;
        anchor[info->pot - 1] = -1.f;
    }
    params::Describe(*info, p, said, sizeof said);
    return said;
}

unsigned Held()
{
    return pot_held;
}

int Preset()
{
    return preset;
}

bool DiskRequested()
{
    bool r         = disk_requested;
    disk_requested = false;
    return r;
}

bool Found()
{
    return pot_bank[0] >= 0 && enc_bank[0] >= 0;
}

int PotBanks()
{
    return (pot_bank[0] >= 0) + (pot_bank[1] >= 0);
}

int EncoderBanks()
{
    return (enc_bank[0] >= 0) + (enc_bank[1] >= 0);
}
} // namespace panel
