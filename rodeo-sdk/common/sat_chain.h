#pragma once
//
// The Seed's side of the satellite chain: discovery, scanning, recovery, and the
// controls an instrument reads.
//
// This is the engine, and it is pure logic: no UART, no clock, no libDaisy. It
// decides what frame to send next and makes sense of what comes back; a
// transport moves the bytes (sat_chain_port.h on the Seed, a simulated ring in
// common/tests/sat_chain_gtest.cpp). The frame format is sat_frame.h, and the
// rules are RODEO_SATELLITE_CHAIN_CONTRACT.md (§ numbers below):
//
//   * wait 50 ms after power for the satellites to boot (§6.1), then DISCOVER;
//   * SCAN every 4 ms (§6.2), output slots out, input slots back;
//   * three bad SCANs in a row: DISCOVER again, up to three times (§5.1);
//   * still nothing: find the break by closing the ring one satellite further
//     each time, and keep scanning the satellites before it (§5.2); retry the
//     whole ring every couple of seconds in case it has been fixed.
//
// Controls are addressed by satellite (chain order, from 0) and slot within it.
// Which control is which parameter is the instrument's business.
//
// One exchange at a time:
//
//   SatChain::Exchange ex;
//   if(chain.Poll(now_us, ex))   // something to send now
//       port.Start(ex);          // ...and later, when it has come back or timed out:
//   chain.Reply(rx, len, now_us);
//
#include <stddef.h>
#include <stdint.h>

#include "sat_frame.h"

namespace rodeo
{
class SatChain
{
  public:
    static constexpr int kMaxSatellites = SAT_MAX_SLOTS; // one slot each, at most

    struct Config
    {
        uint32_t scan_period_us  = SAT_SCAN_PERIOD_MS * 1000u;
        uint32_t power_wait_us   = SAT_FIRST_DISCOVER_MS * 1000u;
        uint32_t retry_period_us = 2000000u; // how often a broken ring is retried whole
        uint8_t  fw_major        = 1;        // the satellite firmware major this build speaks
    };

    enum class State : uint8_t
    {
        kPowerWait, // waiting for the satellites to boot
        kDiscover,  // learning the chain
        kScan,      // the whole ring answers
        kFindBreak, // the ring is broken: looking for where
        kPartial,   // broken: scanning the satellites before the break
        kVersion,   // a satellite runs a firmware major this build doesn't speak
    };

    /** One frame to send, and what to expect back. */
    struct Exchange
    {
        uint8_t  tx[SAT_MAX_FRAME_LEN];
        uint16_t tx_len;
        uint16_t rx_len;     // the reply's exact length; 0: read it from the reply's header
        uint32_t timeout_us; // give up on the reply after this long
    };

    void Init(const Config& cfg, uint32_t now_us);

    /** True when a frame should go out now; fills `ex`. Call often from the
     *  main loop. Nothing is sent while a reply is awaited. */
    bool Poll(uint32_t now_us, Exchange& ex);

    /** The reply to the last exchange: `len` bytes, or fewer (0 included) when
     *  it timed out. */
    void Reply(const uint8_t* rx, size_t len, uint32_t now_us);

    // --- The chain -----------------------------------------------------------------

    State GetState() const { return state_; }

    /** Satellites being scanned: the whole ring, or those before a break. */
    int Satellites() const { return count_; }

    const sat_descriptor& Satellite(int i) const { return desc_[i]; }

    /** A break was found after this many satellites, or -1 if there is none. */
    int BrokenAfter() const { return state_ == State::kPartial ? count_ : -1; }

    /** True once after the chain's layout changes (discovered, grown, broken). */
    bool Changed();

    /** SCAN replies that came back whole, and those that didn't. */
    uint32_t GoodScans() const { return good_scans_; }
    uint32_t BadScans() const { return bad_scans_; }

    // --- Controls (satellite i, slot s) ---------------------------------------------

    /** The slot as last received with a good CRC. */
    sat_slot Input(int i, int s) const;

    /** A POT slot's position, 0..1. */
    float Pot(int i, int s) const;

    /** Encoder detents since the last call, + or -. */
    int EncoderSteps(int i, int s);

    /** Whether a button, or an encoder's push switch, is held now. */
    bool Pressed(int i, int s) const;

    /** Presses since the last call. */
    int Presses(int i, int s);

    /** A SWITCH slot's position index. */
    int SwitchPosition(int i, int s) const;

    /** True if the slot reports a FAULT; *code and *detail say which (§7). */
    bool Fault(int i, int s, uint8_t* code = nullptr, uint8_t* detail = nullptr) const;

    /** A satellite's LED: brightness 0..255 and a SAT_LED_ mode. Sent with
     *  every scan. */
    void SetLed(int i, uint8_t brightness, uint8_t mode = SAT_LED_STEADY);

  private:
    enum class Step : uint8_t
    {
        kNone,
        kDiscover,
        kScan,
        kReopen,    // RING_AUTO to the last closed satellite
        kClose,     // CLOSE_RING_HERE, no reply expected
        kProbe,     // DISCOVER after a close
        kRetryOpen, // in kPartial: reopen the break to try the whole ring
        kRetryScan, // ...then DISCOVER it whole
    };

    void     Discovered(const sat_descriptor* d, uint8_t n);
    void     Command(uint8_t cmd, uint8_t target, uint8_t arg, Exchange& ex, uint16_t rx_len);
    void     Discover(Exchange& ex, Step step);
    void     Scan(Exchange& ex);
    int      Slot(int i, int s) const;
    void     Lost(uint32_t now_us);
    void     StartFindBreak(uint8_t from, bool settle);

    Config   cfg_;
    State    state_ = State::kPowerWait;
    Step     waiting_ = Step::kNone;
    uint32_t t0_ = 0, last_scan_ = 0, last_retry_ = 0;
    bool     changed_ = false;

    sat_descriptor desc_[kMaxSatellites];
    uint8_t        offset_[kMaxSatellites]; // first slot of each satellite in the frame
    int            count_ = 0;
    uint8_t        slots_ = 0;

    sat_slot in_[SAT_MAX_SLOTS];       // last good input per slot
    uint8_t  read_counter_[SAT_MAX_SLOTS]; // what EncoderSteps/Presses last handed out
    uint8_t  read_presses_[SAT_MAX_SLOTS];
    uint8_t  led_[kMaxSatellites][2];

    uint8_t  misses_ = 0, discover_tries_ = 0;
    uint8_t  probe_ = 0;   // the position being closed while finding a break
    uint8_t  closed_ = 0;  // the satellite we closed the ring at, 0 = none
    bool     settle_ = false; // closing at the last good position, not probing
    Step     next_ = Step::kNone; // what kFindBreak sends next
    uint32_t good_scans_ = 0, bad_scans_ = 0;
};
} // namespace rodeo
