#include "ladder.h"

void ladder_init(sat_ladder* l, const uint32_t r_switch[4], uint32_t r_pull, uint16_t full_scale)
{
    for(uint8_t m = 0; m < 16; m++)
    {
        // Conductance to ground in nanosiemens; the reading is the divider ratio.
        uint64_t g = 0;
        for(uint8_t i = 0; i < 4; i++)
            if(m & (1u << i))
                g += 1000000000ull / r_switch[i];
        l->level[m] = (uint16_t)((uint64_t)full_scale * 1000000000ull
                                 / (1000000000ull + (uint64_t)r_pull * g));
    }
    l->pressed   = 0;
    l->candidate = 0;
    l->seen      = 0;
    for(uint8_t i = 0; i < 4; i++)
        l->presses[i] = 0;
}

uint8_t ladder_decode(const sat_ladder* l, uint16_t reading)
{
    uint8_t  best = 0;
    uint16_t dist = 0xFFFF;
    for(uint8_t m = 0; m < 16; m++)
    {
        uint16_t d = reading > l->level[m] ? reading - l->level[m] : l->level[m] - reading;
        if(d < dist)
        {
            dist = d;
            best = m;
        }
    }
    return best;
}

void ladder_update(sat_ladder* l, uint16_t reading)
{
    uint8_t m = ladder_decode(l, reading);
    if(m != l->candidate)
    {
        l->candidate = m;
        l->seen      = 1;
        return;
    }
    if(l->seen < LADDER_STABLE)
        l->seen++;
    if(l->seen == LADDER_STABLE && m != l->pressed)
    {
        for(uint8_t i = 0; i < 4; i++)
            if((m & (1u << i)) && !(l->pressed & (1u << i)))
                l->presses[i]++;
        l->pressed = m;
    }
}
