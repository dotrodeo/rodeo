// Presets. See presets.h.
#include "presets.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "daisy_seed.h"
#include "fatfs.h"
#include "samples.h"

namespace presets
{
namespace
{
constexpr int    kMax      = 64;
constexpr size_t kFileSize = 2048; // more than any preset needs; the rest is ignored

struct Preset
{
    char          name[32];
    synth::Params p;         // from the defaults, with what the file sets
    char          sound[32]; // by name, or empty
    unsigned      pots;      // PotBits the file sets
};

Preset presets[kMax];
int    n_presets = 0;
char   problems[512];
const char* missing = nullptr;

// FatFS goes through these, so the SDMMC's DMA can reach them (not DTCM)
FIL      file DSY_SDRAM_BSS;
DIR      dir DSY_SDRAM_BSS;
FILINFO  info DSY_SDRAM_BSS;
alignas(32) char text[kFileSize + 1] DSY_SDRAM_BSS;

void Problem(const char* fmt, const char* a, int b, const char* c)
{
    size_t n = strlen(problems);
    if(n < sizeof problems - 1)
        snprintf(problems + n, sizeof problems - n, fmt, a, b, c);
}

// Lower case, without spaces: "Saw + Sub" matches "saw+sub"
void Squash(char* to, const char* from, size_t n)
{
    size_t k = 0;
    for(; *from && k < n - 1; from++)
        if(!isspace((unsigned char)*from))
            to[k++] = (char)tolower((unsigned char)*from);
    to[k] = 0;
}

bool Number(const char* v, float lo, float hi, float* out)
{
    char* end;
    float x = strtof(v, &end);
    if(end == v || *end)
        return false;
    *out = x < lo ? lo : (x > hi ? hi : x);
    return true;
}

// One `key = value`; false if it means nothing.
bool Set(Preset& pr, const char* key, const char* value)
{
    char k[16], v[32];
    Squash(k, key, sizeof k);
    Squash(v, value, sizeof v);
    synth::Params& p = pr.p;
    float          x;
    if(!strcmp(k, "layer"))
    {
        if(!strcmp(v, "synth"))
            p.layer = synth::Layer::Synth;
        else if(!strcmp(v, "sampler"))
            p.layer = synth::Layer::Sampler;
        else if(!strcmp(v, "both"))
            p.layer = synth::Layer::Both;
        else
            return false;
        return true;
    }
    if(!strcmp(k, "shape"))
    {
        for(int s = 0; s < synth::kShapes; s++)
        {
            char name[16];
            Squash(name, synth::ShapeName(s), sizeof name);
            if(!strcmp(v, name))
            {
                p.shape = s;
                return true;
            }
        }
        return false;
    }
    if(!strcmp(k, "sound"))
    {
        // the name as written (spaces and case kept for the message), trimmed
        strncpy(pr.sound, value, sizeof pr.sound - 1);
        pr.sound[sizeof pr.sound - 1] = 0;
        return pr.sound[0] != 0;
    }
    if(!strcmp(k, "lfo"))
    {
        if(strcmp(v, "on") && strcmp(v, "off"))
            return false;
        p.lfo = !strcmp(v, "on");
        return true;
    }
    if(!strcmp(k, "fx"))
    {
        if(!strcmp(v, "chorus"))
            p.fx = synth::Fx::Chorus;
        else if(!strcmp(v, "reverb"))
            p.fx = synth::Fx::Reverb;
        else if(!strcmp(v, "both"))
            p.fx = synth::Fx::Both;
        else
            return false;
        return true;
    }
    if(!strcmp(k, "mono"))
    {
        if(strcmp(v, "on") && strcmp(v, "off"))
            return false;
        p.mono = !strcmp(v, "on");
        return true;
    }
    if(!strcmp(k, "filter"))
    {
        if(!strcmp(v, "svf"))
            p.filter = synth::Filter::Svf;
        else if(!strcmp(v, "ladder"))
            p.filter = synth::Filter::Ladder;
        else if(!strcmp(v, "ladder12"))
            p.filter = synth::Filter::Ladder12;
        else
            return false;
        return true;
    }
    if(!strcmp(k, "decay") && Number(v, 1.f, 10000.f, &x))
        return p.decay = x / 1000.f, pr.pots |= kDecay, true;
    if(!strcmp(k, "sustain") && Number(v, 0.f, 100.f, &x))
        return p.sustain = x / 100.f, pr.pots |= kSustain, true;
    if(!strcmp(k, "fattack") && Number(v, 1.f, 4000.f, &x))
        return p.fattack = x / 1000.f, true;
    if(!strcmp(k, "fdecay") && Number(v, 1.f, 10000.f, &x))
        return p.fdecay = x / 1000.f, true;
    if(!strcmp(k, "fsustain") && Number(v, 0.f, 100.f, &x))
        return p.fsustain = x / 100.f, true;
    if(!strcmp(k, "drive") && Number(v, 0.f, 100.f, &x))
        return p.drive = x / 100.f, pr.pots |= kDrive, true;
    if(!strcmp(k, "hpf") && Number(v, 0.f, 2000.f, &x))
        return p.hpf = x, true;
    if(!strcmp(k, "keytrack") && Number(v, 0.f, 100.f, &x))
        return p.keytrack = x / 100.f, true;
    if(!strcmp(k, "glide") && Number(v, 0.f, 2000.f, &x))
        return p.glide = x / 1000.f, true;
    if(!strcmp(k, "detune") && Number(v, 0.f, 50.f, &x))
        return p.detune = x / 100.f, true;
    if(!strcmp(k, "octave") && Number(v, -2.f, 2.f, &x))
        return p.octave = (int)lroundf(x), true;
    if(!strcmp(k, "attack") && Number(v, 1.f, 4000.f, &x))
        return p.attack = x / 1000.f, pr.pots |= kAttack, true;
    if(!strcmp(k, "release") && Number(v, 5.f, 4000.f, &x))
        return p.release = x / 1000.f, pr.pots |= kRelease, true;
    if(!strcmp(k, "cutoff") && Number(v, 0.f, 100.f, &x))
        return p.cutoff = x / 100.f, pr.pots |= kCutoff, true;
    if(!strcmp(k, "resonance") && Number(v, 0.f, 100.f, &x))
        return p.resonance = x / 100.f, pr.pots |= kResonance, true;
    if(!strcmp(k, "env") && Number(v, 0.f, 100.f, &x))
        return p.env_amount = x / 100.f, pr.pots |= kEnv, true;
    if(!strcmp(k, "mix") && Number(v, 0.f, 100.f, &x))
        return p.fx_mix = x / 100.f, true;
    return false;
}

char* Trim(char* s)
{
    while(isspace((unsigned char)*s))
        s++;
    char* e = s + strlen(s);
    while(e > s && isspace((unsigned char)e[-1]))
        *--e = 0;
    return s;
}

void Parse(Preset& pr)
{
    int   line = 0;
    char* next = text;
    while(next && *next)
    {
        char* s = next;
        next    = strchr(s, '\n');
        if(next)
            *next++ = 0;
        line++;
        char* c = strpbrk(s, "#;");
        if(c)
            *c = 0;
        s = Trim(s);
        if(!*s)
            continue;
        char* eq = strchr(s, '=');
        if(eq)
            *eq = 0;
        if(!eq || !Set(pr, Trim(s), Trim(eq + 1)))
            Problem("  %s, line %d: can't read \"%s\"\r\n", pr.name, line, s);
    }
}

bool IsTxt(const char* name)
{
    size_t n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".txt") && name[0] != '.';
}
} // namespace

int Load()
{
    n_presets   = 0;
    problems[0] = 0;
    char path[160];
    snprintf(path, sizeof path, "%spresets", samples::Root());
    if(f_opendir(&dir, path) != FR_OK)
        return 0;
    while(f_readdir(&dir, &info) == FR_OK && info.fname[0] && n_presets < kMax)
    {
        if(info.fattrib & (AM_DIR | AM_HID | AM_SYS) || !IsTxt(info.fname))
            continue;
        Preset& pr = presets[n_presets];
        pr         = Preset();
        strncpy(pr.name, info.fname, sizeof pr.name - 1);
        pr.name[sizeof pr.name - 1] = 0;
        char* dot = strrchr(pr.name, '.');
        if(dot && !strcasecmp(dot, ".txt"))
            *dot = 0;

        char file_path[160];
        snprintf(file_path, sizeof file_path, "%.95s/%.63s", path, info.fname);
        UINT got = 0;
        if(f_open(&file, file_path, FA_READ) != FR_OK)
            continue;
        bool ok = f_read(&file, text, kFileSize, &got) == FR_OK;
        f_close(&file);
        if(!ok)
            continue;
        text[got] = 0;
        Parse(pr);
        n_presets++;
    }
    f_closedir(&dir);

    // In name order, as the kits are
    for(int i = 1; i < n_presets; i++)
        for(int j = i; j > 0 && strcasecmp(presets[j - 1].name, presets[j].name) > 0; j--)
        {
            Preset t       = presets[j];
            presets[j]     = presets[j - 1];
            presets[j - 1] = t;
        }
    return n_presets;
}

int Count()
{
    return n_presets;
}

const char* Name(int i)
{
    return i >= 0 && i < n_presets ? presets[i].name : "";
}

unsigned Apply(int i, synth::Params& p, unsigned live)
{
    if(i < 0 || i >= n_presets)
        return 0;
    const Preset& pr = presets[i];
    synth::Params was = p;
    p                 = pr.p;
    // The pots' parameters the preset leaves out stay where the pots have them
    unsigned keep = live & ~pr.pots;
    if(keep & kCutoff)
        p.cutoff = was.cutoff;
    if(keep & kResonance)
        p.resonance = was.resonance;
    if(keep & kEnv)
        p.env_amount = was.env_amount;
    if(keep & kDrive)
        p.drive = was.drive;
    if(keep & kAttack)
        p.attack = was.attack;
    if(keep & kDecay)
        p.decay = was.decay;
    if(keep & kSustain)
        p.sustain = was.sustain;
    if(keep & kRelease)
        p.release = was.release;
    p.sound = was.sound;
    missing = pr.sound[0] ? pr.sound : nullptr;
    if(pr.sound[0])
    {
        char want[32];
        Squash(want, pr.sound, sizeof want);
        for(int s = 0; s < samples::Sounds(); s++)
        {
            char have[32];
            Squash(have, samples::GetSound(s).name, sizeof have);
            if(!strcmp(want, have))
            {
                p.sound = s;
                missing = nullptr;
                break;
            }
        }
    }
    if(samples::Sounds() == 0)
        p.layer = synth::Layer::Synth; // nothing to sample
    return pr.pots;
}

const char* MissingSound()
{
    return missing;
}

const char* Problems()
{
    return problems;
}
} // namespace presets
