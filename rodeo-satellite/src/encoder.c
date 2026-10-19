#include "encoder.h"

// Quarter-step change for (previous << 2 | current) Gray-code states: +1 one
// way round 11 -> 01 -> 00 -> 10 -> 11, -1 the other, 0 for no change or an
// impossible double step.
static const int8_t kStep[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

void quad_init(sat_quad* q, uint8_t a, uint8_t b)
{
    q->prev    = (uint8_t)((a << 1) | b);
    q->quarter = 0;
    q->detents = 0;
}

void quad_update(sat_quad* q, uint8_t a, uint8_t b)
{
    uint8_t now = (uint8_t)((a << 1) | b);
    if(now == q->prev)
        return;
    q->quarter = (int8_t)(q->quarter + kStep[(q->prev << 2) | now]);
    q->prev    = now;
    if(now == 3) // back at rest: count a detent if it went far enough
    {
        if(q->quarter >= 2)
            q->detents++;
        else if(q->quarter <= -2)
            q->detents--;
        q->quarter = 0;
    }
}
