// The panel. See panel.h.
#include "panel.h"

#include <math.h>
#include <stdio.h>
#include "daisy_seed.h"
#include "samples.h"

namespace panel
{
namespace
{
int  pot_bank = -1, enc_bank = -1; // where on the chain, or -1
char said[96];

struct Encoder
{
    bool was_down = false;
    bool turned   = false; // while held
};
Encoder enc[4];

// E1 + E4 held: disk mode. While both are down neither taps when let go.
constexpr uint32_t kHoldMs = 1000;
bool               both_down = false, both_fired = false, disk_requested = false;
uint32_t           both_since = 0;

constexpr int kAttackSteps = 24;
int           attack_step  = 4; // 0.001 * 4000^(4/24): about 4 ms

float AttackFor(int step)
{
    return 0.001f * powf(4000.f, step / (float)kAttackSteps);
}

int Wrap(int x, int n)
{
    return n > 0 ? ((x % n) + n) % n : 0;
}

float Clamp(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
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
    pot_bank = enc_bank = -1;
    for(int i = 0; i < chain.Satellites(); i++)
    {
        uint8_t kind = chain.Satellite(i).kind;
        if(kind == SAT_KIND_POT_BANK_4 && pot_bank < 0)
            pot_bank = i;
        if(kind == SAT_KIND_ENC_BANK_4 && enc_bank < 0)
            enc_bank = i;
    }
}

void Turn(int e, int steps, bool held, synth::Params& p)
{
    switch(e)
    {
        case 0:
            if(p.layer == synth::Layer::Synth)
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
            attack_step = (int)Clamp(attack_step + steps, 0, kAttackSteps);
            p.attack    = AttackFor(attack_step);
            snprintf(said, sizeof said, "attack: %d ms", (int)lroundf(p.attack * 1000.f));
            break;
        case 3:
            p.fx_mix = Clamp(p.fx_mix + 0.05f * steps, 0.f, 1.f);
            snprintf(said, sizeof said, "effects mix: %d %%", (int)lroundf(p.fx_mix * 100.f));
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
    }
}
} // namespace

const char* Update(rodeo::SatChain& chain, synth::Params& p, Status status)
{
    bool loading = status == Status::Loading;
    said[0] = 0;
    if(chain.Changed())
        Find(chain);

    if(pot_bank >= 0)
    {
        p.cutoff     = chain.Pot(pot_bank, 0);
        p.resonance  = chain.Pot(pot_bank, 1);
        p.env_amount = chain.Pot(pot_bank, 2);
        p.release    = 0.005f * powf(800.f, chain.Pot(pot_bank, 3)); // 5 ms .. 4 s
        uint8_t mode = status == Status::Disk ? SAT_LED_SLOW_BLINK
                       : loading              ? SAT_LED_FAST_BLINK
                                              : SAT_LED_STEADY;
        chain.SetLed(pot_bank, synth::Active() > 0 || status != Status::Playing ? 255 : 20,
                     mode);
    }

    if(enc_bank >= 0)
    {
        bool first = chain.Pressed(enc_bank, 0), last = chain.Pressed(enc_bank, 3);
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

        for(int e = 0; e < 4; e++)
        {
            bool down    = chain.Pressed(enc_bank, e);
            int  steps   = chain.EncoderSteps(enc_bank, e);
            int  presses = chain.Presses(enc_bank, e); // catches a press too quick to see
            if(steps)
            {
                Turn(e, steps, down, p);
                if(down)
                    enc[e].turned = true;
            }
            if(!down && (enc[e].was_down || presses > 0) && !enc[e].turned)
                Tap(e, p);
            if(!down && !both_down)
                enc[e].turned = false;
            enc[e].was_down = down;
        }
        uint8_t mode = p.layer == synth::Layer::Synth     ? SAT_LED_STEADY
                       : p.layer == synth::Layer::Sampler ? SAT_LED_SLOW_BLINK
                                                          : SAT_LED_FAST_BLINK;
        chain.SetLed(enc_bank, 255, mode);
    }
    return said[0] ? said : nullptr;
}

bool DiskRequested()
{
    bool r         = disk_requested;
    disk_requested = false;
    return r;
}

bool Found()
{
    return pot_bank >= 0 && enc_bank >= 0;
}
} // namespace panel
