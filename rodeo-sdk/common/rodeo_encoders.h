#pragma once
//
// Rodeo Encoders — Layer 2 scanner over the shared-I2C encoder hubs.
//
// Owns the shared I2C1 handle + up to four EncoderHub device drivers, enumerates
// which hubs answer at boot (0x30..0x33), and runs a non-blocking, timed
// round-robin poll of each hub's hot telemetry block — folding read-to-clear
// deltas and press-edge latches into RAM caches. The app calls Init() once,
// pumps Update() from its control loop, and reads a friendly relative-encoder
// API — it never touches I2C or the register map.
//
// This is the encoder analogue of RodeoAdc: Layer 1 (EncoderHub) is the per-hub
// device, Layer 2 (this) is the multi-device cached scanner, and Layer 3 — which
// hub-slot maps to which synth parameter — stays OUT of the library (the control
// map's job, exactly as with the ADC channel bindings in board.h).
//
// Addressing convention:
//     hub = 0..3  -> I2C address 0x30 + hub
//     slot = 0..8 -> encoder 1..9 on that hub (connector J(slot+3))
//     flat index  = hub*9 + slot  (0..35), for uniform iteration
//
// Relative-encoder value model (why it's friendly): the hub accumulates signed
// detents in an int8 that read-clears on our burst. Update() drains that into a
// per-slot cache, so the APP can poll at ANY cadence and never lose a detent:
//   - GetDelta(): detents since YOU last asked — read-clears at this layer.
//   - GetPosition(): running sum since boot — never clears (virtual encoder).
//   - IsPressed(): live debounced button level.
//   - PopPress(): a press edge happened since you last asked — read-clears, so a
//     fast tap between polls is caught via the hub's latch, never missed.
//
#include "daisy_seed.h"
#include "encoder_hub.h"

namespace rodeo
{
class RodeoEncoders
{
  public:
    static constexpr uint8_t kMaxHubs     = 4;
    static constexpr uint8_t kEncPerHub   = EncoderHub::kNumEncoders; // 9
    static constexpr uint8_t kMaxEncoders = kMaxHubs * kEncPerHub;    // 36

    struct Config
    {
        // bit i set => probe I2C address 0x30+i at boot. Default: all four.
        uint8_t hub_mask = 0x0F;
        // Minimum interval between bus polls (µs). ONE present hub is serviced
        // per due tick (round-robin), so a full sweep of N present hubs takes up
        // to N*poll_interval_us. Default 1000 µs => a lone hub polls at 1 kHz
        // (contract cadence); lower it if you run multiple hubs and want each at
        // full rate. Deltas accumulate on the hub between polls, so slower polls
        // lose no detents (only saturate past ±127, which sets STATUS.OVERFLOW).
        uint32_t poll_interval_us = 1000;
        // I2C bus speed. Contract boot speed is 100 kHz; promote to 400 kHz once
        // Bumble validates the populated bus.
        daisy::I2CHandle::Config::Speed speed
            = daisy::I2CHandle::Config::Speed::I2C_100KHZ;
    };

    // Inits the shared I2C1 bus and probes each masked address. A hub is "present"
    // only if it answers WHO_AM_I=0x9E with a matching MAP_VERSION (so the OLED
    // at 0x3C is naturally rejected). Returns true if at least one hub answered.
    bool Init(const Config &cfg);
    bool Init(); // default Config (all four addresses, 100 kHz, 1 kHz/hub)

    // Pump from the control loop. Non-blocking: returns immediately until the
    // poll interval elapses, then services the next present hub (one blocking
    // ~16-byte telemetry read + one 2-byte latch read) and advances the cursor.
    void Update();

    // --- friendly relative-encoder API (hub 0..3, slot 0..8) ---
    // Accumulated signed detents since the last GetDelta on this slot. CW = +.
    // READ-CLEARS. Saturates at the int16 range (a fault indicator, not a path a
    // human hand reaches). Out-of-range args return 0.
    int16_t GetDelta(uint8_t hub, uint8_t slot);
    // Running detent count since boot; never clears. Wraps modulo int32.
    int32_t GetPosition(uint8_t hub, uint8_t slot) const;
    // Live debounced press level (1 = pressed).
    bool IsPressed(uint8_t hub, uint8_t slot) const;
    // True once per press edge since the last PopPress. READ-CLEARS. Catches a
    // tap shorter than the poll interval via the hub's press latch.
    bool PopPress(uint8_t hub, uint8_t slot);

    // --- flat-index variants (idx = hub*9 + slot, 0..35) ---
    int16_t GetDelta(uint8_t idx);
    int32_t GetPosition(uint8_t idx) const;
    bool    IsPressed(uint8_t idx) const;
    bool    PopPress(uint8_t idx);

    // --- topology ---
    bool    HubPresent(uint8_t hub) const;
    uint8_t HubAddress(uint8_t hub) const
    {
        return static_cast<uint8_t>(RODEO_HUB_ADDR_BASE + hub);
    }
    uint8_t NumHubsPresent() const;
    // Escape hatch for per-hub config/commands (dir invert, soft reset, LEDs).
    EncoderHub &Hub(uint8_t hub) { return hub_[hub]; }

  private:
    static bool ValidHubSlot(uint8_t hub, uint8_t slot)
    {
        return hub < kMaxHubs && slot < kEncPerHub;
    }
    static uint8_t FlatIndex(uint8_t hub, uint8_t slot)
    {
        return static_cast<uint8_t>(hub * kEncPerHub + slot);
    }
    void ServiceHub(uint8_t hub); // burst-read one hub, fold into the caches
    // lowest present hub at or after `from` (wraps); 0xFF if none present.
    uint8_t NextPresentHub(uint8_t from) const;

    daisy::I2CHandle i2c_;
    EncoderHub       hub_[kMaxHubs];
    bool             present_[kMaxHubs] = {false};

    int32_t position_[kMaxEncoders]   = {0}; // running, never clears
    int16_t delta_[kMaxEncoders]      = {0}; // pending since app read; r/clr
    bool    pressed_[kMaxEncoders]    = {false};
    bool    press_edge_[kMaxEncoders] = {false}; // pending; r/clr

    uint8_t  cur_hub_          = 0; // round-robin cursor
    uint32_t poll_interval_us_ = 1000;
    uint32_t t_last_us_        = 0;
};

} // namespace rodeo
