#pragma once
//
// Rodeo pot UI — pure control-surface logic, no hardware dependencies.
//
// Same deal as adc_convert.h: the parts of the panel behaviour that are just
// math and state live here, dependency-free, so they can be unit-tested on the
// HOST with no libDaisy in the build — and the firmware calls THESE functions,
// so the tests guard the real path (see common/tests/README.md).
//
// Time is passed in as a millisecond argument rather than read from
// System::GetNow(), which is what keeps this header host-safe and the tests
// deterministic.
//
#include <cstdint>

namespace rodeo
{
// Quantize a 0..1 pot reading into `steps` equal zones, with hysteresis so a
// knob parked on a boundary can't flicker between two settings.
//
// `current` is the zone in effect right now (pass a value outside 0..steps-1 to
// seed from the reading alone). A different zone only wins once the reading has
// cleared the current zone's edge by `hyst`.
inline int
StepWithHysteresis(float norm, int steps, int current, float hyst = 0.03f)
{
    if(steps <= 1)
        return 0;

    const float width = 1.0f / static_cast<float>(steps);

    int raw = static_cast<int>(norm / width);
    if(raw < 0)
        raw = 0;
    if(raw >= steps)
        raw = steps - 1; // norm == 1.0 lands one past the top zone

    if(current < 0 || current >= steps) // unseeded: take the reading as-is
        return raw;
    if(raw == current)
        return current;

    const float lo = static_cast<float>(current) * width;
    const float hi = lo + width;
    if(norm > hi + hyst) // left the zone upward, past the guard band
        return raw;
    if(norm < lo - hyst) // ... or downward
        return raw;
    return current; // inside the guard band: stay put
}

// Tracks which pot the user is currently touching, so a display can pop up that
// parameter and then fall back to its idle view.
//
// Motion is measured against `anchor` — the value at which motion was LAST
// declared — not against the previous sample. That matters: a slow deliberate
// sweep moves less than the deadband per sample, so a previous-sample
// comparison would filter it out entirely, while an anchor comparison lets the
// movement accumulate until it trips.
struct KnobWatch
{
    static constexpr int kMaxPots = 24; // board::kNumAdcChannels

    float    anchor[kMaxPots] = {0.0f};
    int      active           = -1; // most recently moved pot, -1 = none
    uint32_t t_move_ms        = 0;  // when that pot last moved

    // Seed the anchors from the current readings without declaring motion.
    // Call once after the ADC scan has populated every channel, otherwise the
    // boot-time zeros read as a full-scale move on every pot.
    void Reset(const float *norms, int n)
    {
        if(n > kMaxPots)
            n = kMaxPots;
        for(int i = 0; i < n; ++i)
            anchor[i] = norms[i];
        active    = -1;
        t_move_ms = 0;
    }

    // Feed the current raw readings. Returns true while the popup should show;
    // `active` names which pot it is for.
    bool Update(const float *norms,
                int          n,
                uint32_t     now_ms,
                float        deadband = 0.012f,
                uint32_t     hold_ms  = 800)
    {
        if(n > kMaxPots)
            n = kMaxPots;

        for(int i = 0; i < n; ++i)
        {
            float d = norms[i] - anchor[i];
            if(d < 0.0f)
                d = -d;
            if(d > deadband)
            {
                anchor[i] = norms[i];
                active    = i; // latest mover wins
                t_move_ms = now_ms;
            }
        }

        if(active < 0)
            return false;
        if(now_ms - t_move_ms >= hold_ms)
        {
            active = -1;
            return false;
        }
        return true;
    }
};

} // namespace rodeo
