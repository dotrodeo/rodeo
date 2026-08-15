#pragma once
//
// Rodeo motherboard carrier status LED.
//
// Wired to Seed pin 28 = seed::D21 (PC4), active high (board::pin::kStatusLed).
// Header-only + inline so every example can drive it without touching its
// Makefile. Typical use: Init() once, then either Set/Toggle explicitly to
// signal activity, or call Heartbeat() every main-loop turn for a "firmware is
// alive" blink that proves the loop isn't hung.
//
#include "daisy_seed.h"
#include "board.h"

namespace rodeo
{
class StatusLed
{
  public:
    void Init()
    {
        led_.Init(daisy::seed::D21, daisy::GPIO::Mode::OUTPUT); // board pin 28
        Set(false);
    }

    void Set(bool on)
    {
        state_ = on;
        led_.Write(on);
    }
    void Toggle() { Set(!state_); }
    bool State() const { return state_; }

    // Toggle at period_ms using the ms system tick; safe to call every loop.
    void Heartbeat(uint32_t period_ms = 500)
    {
        uint32_t now = daisy::System::GetNow();
        if(now - last_ms_ >= period_ms)
        {
            last_ms_ = now;
            Toggle();
        }
    }

  private:
    daisy::GPIO led_;
    bool        state_   = false;
    uint32_t    last_ms_ = 0;
};

} // namespace rodeo
