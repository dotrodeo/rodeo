#include "lights.h"

void lights_init(sat_lights* l)
{
    for(uint8_t i = 0; i < LIGHTS_N; i++)
    {
        l->r[i] = l->g[i] = l->b[i] = 0;
        l->mode[i] = SAT_LED_STEADY;
    }
}

void lights_take(sat_lights* l, const sat_slot* taken, uint8_t n)
{
    for(uint8_t i = 0; i < n && i < LIGHTS_N; i++)
    {
        sat_slot s = taken[i];
        if(!sat_slot_ok(s))
            continue;
        if(s.kind == SAT_OUT_RGB)
        {
            sat_rgb_unpack(s, &l->r[i], &l->g[i], &l->b[i]);
            l->mode[i] = SAT_LED_STEADY;
        }
        else if(s.kind == SAT_OUT_LED)
        {
            l->r[i] = l->g[i] = l->b[i] = s.d0;
            l->mode[i] = s.d1;
        }
    }
}

static bool lit(uint8_t mode, uint32_t now_ms)
{
    if(mode == SAT_LED_SLOW_BLINK)
        return (now_ms % 1000u) < 500u;
    if(mode == SAT_LED_FAST_BLINK)
        return (now_ms % 250u) < 125u;
    return true;
}

void lights_render(const sat_lights* l, uint32_t now_ms, bool identify, uint16_t cap_ma,
                   uint32_t grb[LIGHTS_N])
{
    uint8_t  r[LIGHTS_N], g[LIGHTS_N], b[LIGHTS_N];
    uint32_t sum = 0; // all channels, in 1/255 of LIGHTS_MA_PER_CHANNEL
    for(uint8_t i = 0; i < LIGHTS_N; i++)
    {
        bool on = identify ? lit(SAT_LED_FAST_BLINK, now_ms) : lit(l->mode[i], now_ms);
        r[i]    = !on ? 0 : identify ? 255 : l->r[i];
        g[i]    = !on ? 0 : identify ? 255 : l->g[i];
        b[i]    = !on ? 0 : identify ? 255 : l->b[i];
        sum += (uint32_t)r[i] + g[i] + b[i];
    }
    // Total current is sum x 12 mA / 255. Over the cap, scale everything by
    // cap / total, rounding down so the result never exceeds it.
    uint32_t limit = (uint32_t)cap_ma * 255u / LIGHTS_MA_PER_CHANNEL;
    for(uint8_t i = 0; i < LIGHTS_N; i++)
    {
        if(sum > limit)
        {
            r[i] = (uint8_t)((uint32_t)r[i] * limit / sum);
            g[i] = (uint8_t)((uint32_t)g[i] * limit / sum);
            b[i] = (uint8_t)((uint32_t)b[i] * limit / sum);
        }
        grb[i] = ((uint32_t)g[i] << 16) | ((uint32_t)r[i] << 8) | b[i];
    }
}
