// The parameter table. See params.h.
#include "params.h"
#include <math.h>
#include <stdio.h>
#include "samples.h"

namespace params
{
namespace
{
const char* const kOffOn[]   = {"off", "on"};
const char* const kFx[]      = {"chorus", "reverb", "both"};
const char* const kLayer[]   = {"synth", "sampler", "both"};
const char* const kShape[]   = {"saw", "square", "pulse", "triangle", "saw + sub", "square + sub", "fm"};
const char* const kFilter[]  = {"svf", "ladder", "ladder12"};
static_assert(sizeof kShape / sizeof kShape[0] == synth::kShapes, "a name for every shape");

#define PERMILLE "\\u2030"
// id, key, min, max, default (Init() fills it in), unit, names, pot
Info kTable[] = {
    {0, "cutoff", 0, 1000, 0, PERMILLE, nullptr, 1},
    {1, "resonance", 0, 1000, 0, PERMILLE, nullptr, 2},
    {2, "env", 0, 1000, 0, PERMILLE, nullptr, 3},
    {3, "release", 5, 4000, 0, "ms", nullptr, 8},
    {4, "attack", 1, 4000, 0, "ms", nullptr, 5},
    {5, "detune", 0, 50, 0, "cents", nullptr, 0},
    {6, "octave", -2, 2, 0, "", nullptr, 0},
    {7, "mix", 0, 100, 0, "%", nullptr, 0},
    {8, "lfo", 0, 1, 0, "", kOffOn, 0},
    {9, "fx", 0, 2, 0, "", kFx, 0},
    {10, "layer", 0, 2, 0, "", kLayer, 0},
    {11, "shape", 0, synth::kShapes - 1, 0, "", kShape, 0},
    {12, "sound", 0, 0, 0, "", nullptr, 0}, // max: the card's sounds, less one
    {16, "decay", 1, 10000, 0, "ms", nullptr, 6},
    {17, "sustain", 0, 100, 0, "%", nullptr, 7},
    {18, "fattack", 0, 4000, 0, "ms", nullptr, 0}, // 0: follows attack
    {19, "fdecay", 1, 10000, 0, "ms", nullptr, 0},
    {20, "fsustain", 0, 100, 0, "%", nullptr, 0},
    {21, "filter", 0, 2, 0, "", kFilter, 0},
    {22, "drive", 0, 100, 0, "%", nullptr, 4},
    {23, "hpf", 0, 2000, 0, "Hz", nullptr, 0},
    {24, "keytrack", 0, 100, 0, "%", nullptr, 0},
    {25, "mono", 0, 1, 0, "", kOffOn, 0},
    {26, "glide", 0, 2000, 0, "ms", nullptr, 0},
};
constexpr int kCount = sizeof kTable / sizeof kTable[0];

int R(float x)
{
    return (int)lroundf(x);
}
} // namespace

void Init()
{
    const synth::Params defaults;
    for(Info& info : kTable)
        info.def = (int16_t)Get(info, defaults);
}

int Count()
{
    return kCount;
}

const Info& At(int i)
{
    return kTable[i];
}

const Info* Table()
{
    return kTable;
}

const Info* Find(int id)
{
    for(const Info& info : kTable)
        if(info.id == id)
            return &info;
    return nullptr;
}

int Max(const Info& info)
{
    if(info.id == 12)
        return samples::Sounds() > 0 ? samples::Sounds() - 1 : 0;
    return info.max;
}

int Get(const Info& info, const synth::Params& p)
{
    switch(info.id)
    {
        case 0: return R(p.cutoff * 1000.f);
        case 1: return R(p.resonance * 1000.f);
        case 2: return R(p.env_amount * 1000.f);
        case 3: return R(p.release * 1000.f);
        case 4: return R(p.attack * 1000.f);
        case 5: return R(p.detune * 100.f);
        case 6: return p.octave;
        case 7: return R(p.fx_mix * 100.f);
        case 8: return p.lfo ? 1 : 0;
        case 9: return (int)p.fx;
        case 10: return (int)p.layer;
        case 11: return p.shape;
        case 12: return p.sound;
        case 16: return R(p.decay * 1000.f);
        case 17: return R(p.sustain * 100.f);
        case 18: return p.fattack < 0.f ? 0 : R(p.fattack * 1000.f);
        case 19: return R(p.fdecay * 1000.f);
        case 20: return R(p.fsustain * 100.f);
        case 21: return (int)p.filter;
        case 22: return R(p.drive * 100.f);
        case 23: return R(p.hpf);
        case 24: return R(p.keytrack * 100.f);
        case 25: return p.mono ? 1 : 0;
        case 26: return R(p.glide * 1000.f);
    }
    return 0;
}

void Put(const Info& info, int v, synth::Params& p)
{
    int hi = Max(info);
    v      = v < info.min ? info.min : v > hi ? hi : v;
    switch(info.id)
    {
        case 0: p.cutoff = v / 1000.f; break;
        case 1: p.resonance = v / 1000.f; break;
        case 2: p.env_amount = v / 1000.f; break;
        case 3: p.release = v / 1000.f; break;
        case 4: p.attack = v / 1000.f; break;
        case 5: p.detune = v / 100.f; break;
        case 6: p.octave = v; break;
        case 7: p.fx_mix = v / 100.f; break;
        case 8: p.lfo = v != 0; break;
        case 9: p.fx = (synth::Fx)v; break;
        case 10:
            // Nothing to sample without sounds on the card, as a tap on E1 says
            p.layer = samples::Sounds() > 0 ? (synth::Layer)v : synth::Layer::Synth;
            break;
        case 11: p.shape = v; break;
        case 12: p.sound = v; break;
        case 16: p.decay = v / 1000.f; break;
        case 17: p.sustain = v / 100.f; break;
        case 18: p.fattack = v == 0 ? -1.f : v / 1000.f; break;
        case 19: p.fdecay = v / 1000.f; break;
        case 20: p.fsustain = v / 100.f; break;
        case 21: p.filter = (synth::Filter)v; break;
        case 22: p.drive = v / 100.f; break;
        case 23: p.hpf = (float)v; break;
        case 24: p.keytrack = v / 100.f; break;
        case 25: p.mono = v != 0; break;
        case 26: p.glide = v / 1000.f; break;
    }
}

void Describe(const Info& info, const synth::Params& p, char* buf, size_t n)
{
    int v = Get(info, p);
    if(info.id == 12)
    {
        const char* name = v < samples::Sounds() ? samples::GetSound(v).name : "none";
        snprintf(buf, n, "sound %d: %s", v + 1, name);
    }
    else if(info.id == 18 && v == 0)
        snprintf(buf, n, "fattack: follows attack");
    else if(info.names)
        snprintf(buf, n, "%s: %s", info.key, info.names[v - info.min]);
    else if(info.unit[0] == '\\') // per mille, shown as the panel shows it: a percentage
        snprintf(buf, n, "%s: %d %%", info.key, (v + 5) / 10);
    else if(info.unit[0])
        snprintf(buf, n, "%s: %d %s", info.key, v, info.unit);
    else
        snprintf(buf, n, "%s: %+d", info.key, v);
}
} // namespace params
