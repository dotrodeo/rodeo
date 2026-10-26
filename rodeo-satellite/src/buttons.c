#include "buttons.h"

void buttons_init(sat_buttons* b)
{
    b->pressed = 0;
    for(uint8_t i = 0; i < 4; i++)
        b->run[i] = b->presses[i] = 0;
}

void buttons_update(sat_buttons* b, uint8_t raw)
{
    for(uint8_t i = 0; i < 4; i++)
    {
        uint8_t bit = (uint8_t)(1u << i);
        if((raw & bit) == (b->pressed & bit))
        {
            b->run[i] = 0;
            continue;
        }
        if(++b->run[i] < BUTTONS_STABLE_MS)
            continue;
        b->run[i] = 0;
        b->pressed ^= bit;
        if(b->pressed & bit)
            b->presses[i]++;
    }
}
