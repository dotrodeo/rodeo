#include "state_line.h"

#include <stdarg.h>
#include <stdio.h>

namespace state_line
{
namespace
{
constexpr int kMax = rodeo::SatChain::kMaxSatellites;

int     count = -1;      // the layout the totals belong to
uint8_t kinds[kMax];
uint8_t raw[SAT_MAX_SLOTS]; // each encoder's counter when last seen
int     total[SAT_MAX_SLOTS];
int     pot[SAT_MAX_SLOTS];  // each pot as reported, 0..1000

// A pot reading moves on only once it is this far from what was reported: one
// sitting between two values would otherwise flicker, and a stream never rest.
constexpr int kPotHysteresis = 3;

const char* StateWord(rodeo::SatChain::State s)
{
    switch(s)
    {
        case rodeo::SatChain::State::kPowerWait: return "powerwait";
        case rodeo::SatChain::State::kDiscover: return "discover";
        case rodeo::SatChain::State::kScan: return "scan";
        case rodeo::SatChain::State::kFindBreak: return "findbreak";
        case rodeo::SatChain::State::kPartial: return "partial";
        case rodeo::SatChain::State::kVersion: return "version";
    }
    return "?";
}

// Appends to buf at *len; false once it no longer fits.
bool Add(char* buf, size_t n, size_t* len, const char* fmt, ...)
{
    if(*len >= n)
        return false;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + *len, n - *len, fmt, ap);
    va_end(ap);
    if(w < 0 || (size_t)w >= n - *len)
    {
        *len = n;
        return false;
    }
    *len += w;
    return true;
}

int Slots(const rodeo::SatChain& chain, int i)
{
    return chain.Satellite(i).slots;
}
} // namespace

void Track(const rodeo::SatChain& chain)
{
    bool same = chain.Satellites() == count;
    for(int i = 0; same && i < count; i++)
        same = chain.Satellite(i).kind == kinds[i];
    if(!same)
    {
        count = chain.Satellites();
        for(int i = 0; i < count; i++)
            kinds[i] = chain.Satellite(i).kind;
    }

    int k = 0;
    for(int i = 0; i < count; i++)
        for(int s = 0; s < Slots(chain, i) && k < (int)SAT_MAX_SLOTS; s++, k++)
        {
            sat_slot x = chain.Input(i, s);
            int      p = (int)(chain.Pot(i, s) * 1000.f + 0.5f);
            if(!same)
            {
                raw[k]   = x.d0;
                total[k] = 0;
                pot[k]   = p;
            }
            else if(x.kind == SAT_IN_ENCODER)
            {
                total[k] += sat_delta8(x.d0, raw[k]);
                raw[k] = x.d0;
            }
            else if(x.kind == SAT_IN_POT
                    && (p >= pot[k] + kPotHysteresis || p <= pot[k] - kPotHysteresis
                        || p == 0 || p == 1000))
                pot[k] = p;
        }
}

size_t Chain(const rodeo::SatChain& chain, bool panel, char* buf, size_t n)
{
    size_t len = 0;
    Add(buf, n, &len, "\"chain\":{\"state\":\"%s\",\"sats\":%d,\"broken\":%d,\"panel\":%d,\"s\":[",
        StateWord(chain.GetState()), chain.Satellites(), chain.BrokenAfter(), panel ? 1 : 0);

    int k = 0;
    for(int i = 0; i < chain.Satellites(); i++)
    {
        const sat_descriptor& d     = chain.Satellite(i);
        const int             slots = Slots(chain, i);
        const bool tracked = i < count && kinds[i] == d.kind; // Track() has seen it
        Add(buf, n, &len, "%s{\"kind\":%u,\"fw\":\"%u.%u\",\"in\":[", i ? "," : "", d.kind,
            d.fw_major, d.fw_minor);

        bool encoder = false, fault = false;
        for(int s = 0; s < slots; s++)
        {
            sat_slot x = chain.Input(i, s);
            int      v = 0;
            switch(x.kind)
            {
                case SAT_IN_POT:
                    v = tracked && k + s < (int)SAT_MAX_SLOTS
                            ? pot[k + s]
                            : (int)(chain.Pot(i, s) * 1000.f + 0.5f);
                    break;
                case SAT_IN_ENCODER:
                    v       = tracked && k + s < (int)SAT_MAX_SLOTS ? total[k + s] : 0;
                    encoder = true;
                    break;
                case SAT_IN_BUTTON: v = chain.Pressed(i, s); break;
                case SAT_IN_SWITCH: v = chain.SwitchPosition(i, s); break;
                case SAT_IN_FAULT: fault = true; break;
                default: break;
            }
            Add(buf, n, &len, "%s%d", s ? "," : "", v);
        }
        Add(buf, n, &len, "]");

        // Buttons are already in in[]; only an encoder's push switch needs btn[]
        if(encoder)
        {
            Add(buf, n, &len, ",\"btn\":[");
            for(int s = 0; s < slots; s++)
                Add(buf, n, &len, "%s%d", s ? "," : "", chain.Pressed(i, s) ? 1 : 0);
            Add(buf, n, &len, "]");
        }
        if(fault)
        {
            Add(buf, n, &len, ",\"fault\":[");
            for(int s = 0; s < slots; s++)
            {
                uint8_t code, detail;
                if(chain.Fault(i, s, &code, &detail))
                    Add(buf, n, &len, "%s[%u,%u]", s ? "," : "", code, detail);
                else
                    Add(buf, n, &len, "%snull", s ? "," : "");
            }
            Add(buf, n, &len, "]");
        }
        Add(buf, n, &len, "}");
        k += slots;
    }
    if(!Add(buf, n, &len, "]"))
        return 0;
    return len;
}
} // namespace state_line
