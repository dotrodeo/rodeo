#include "sat_chain.h"

namespace rodeo
{
namespace
{
// A reply that hasn't come back in this long never will. DISCOVER and
// COMMAND replies are timed for the longest ring the frame allows; a SCAN's for
// the ring as discovered (sat_timeout_us(), §6.2).
const uint32_t kLongestRingTimeout = sat_timeout_us(SAT_MAX_SLOTS, SAT_MAX_SLOTS);
} // namespace

void SatChain::Init(const Config& cfg, uint32_t now_us)
{
    cfg_     = cfg;
    state_   = State::kPowerWait;
    waiting_ = Step::kNone;
    t0_      = now_us;
    count_   = 0;
    slots_   = 0;
    closed_  = 0;
    changed_ = false;
    misses_ = discover_tries_ = 0;
    good_scans_ = bad_scans_ = 0;
    for(int i = 0; i < kMaxSatellites; i++)
    {
        led_[i][0] = led_[i][1] = 0;
        rgb_set_[i]             = 0;
        for(int s = 0; s < 4; s++)
            rgb_[i][s] = 0;
    }
    for(int k = 0; k < kMaxSatellites; k++)
        in_[k] = sat_slot_make(SAT_IN_EMPTY, 0, 0);
}

bool SatChain::Changed()
{
    bool c   = changed_;
    changed_ = false;
    return c;
}

// --- Building exchanges -------------------------------------------------------------

void SatChain::Discover(Exchange& ex, Step step)
{
    ex.tx_len     = static_cast<uint16_t>(sat_build_discover(ex.tx));
    ex.rx_len     = 0; // a DISCOVER grows by a slot per satellite: read N from the reply
    ex.timeout_us = kLongestRingTimeout;
    waiting_      = step;
}

void SatChain::Command(uint8_t cmd, uint8_t target, uint8_t arg, Exchange& ex, uint16_t rx_len)
{
    sat_slot c    = sat_command(cmd, target, arg);
    ex.tx_len     = static_cast<uint16_t>(sat_build_command(ex.tx, &c, 1));
    ex.rx_len     = rx_len;
    ex.timeout_us = kLongestRingTimeout;
}

void SatChain::Scan(Exchange& ex)
{
    sat_slot out[SAT_MAX_SLOTS];
    for(int i = 0; i < count_; i++)
        for(int s = 0; s < desc_[i].slots; s++)
            out[offset_[i] + s] = (rgb_set_[i] >> s) & 1 ? sat_rgb(rgb_[i][s])
                                  : s == 0            ? sat_led(led_[i][0], led_[i][1])
                                                      : sat_none();
    ex.tx_len     = static_cast<uint16_t>(sat_build_scan(ex.tx, out, slots_));
    ex.rx_len     = static_cast<uint16_t>(SAT_FRAME_LEN(slots_));
    ex.timeout_us = sat_timeout_us(slots_, static_cast<uint8_t>(count_));
    waiting_      = Step::kScan;
}

bool SatChain::Poll(uint32_t now_us, Exchange& ex)
{
    if(waiting_ != Step::kNone)
        return false;
    switch(state_)
    {
        case State::kPowerWait:
            if(now_us - t0_ < cfg_.power_wait_us)
                return false;
            state_          = State::kDiscover;
            discover_tries_ = 0;
            Discover(ex, Step::kDiscover);
            return true;

        case State::kDiscover: Discover(ex, Step::kDiscover); return true;

        case State::kVersion:
            if(now_us - last_retry_ < cfg_.retry_period_us)
                return false;
            last_retry_ = now_us;
            Discover(ex, Step::kDiscover);
            return true;

        case State::kFindBreak:
            if(next_ == Step::kReopen)
            {
                // Open the ring where we closed it, so the next close goes further.
                Command(SAT_CMD_RING_AUTO, closed_, 0, ex, SAT_FRAME_LEN(1));
                waiting_ = Step::kReopen;
            }
            else if(next_ == Step::kClose)
            {
                // Nothing comes back unless this satellite is alive and closes the
                // ring, and it closes only once the frame has passed it: wait it out.
                Command(SAT_CMD_CLOSE_RING_HERE, probe_, 0, ex, SAT_FRAME_LEN(1));
                waiting_ = Step::kClose;
            }
            else
                Discover(ex, Step::kProbe);
            return true;

        case State::kPartial:
            if(next_ == Step::kRetryScan)
            {
                next_ = Step::kNone;
                Discover(ex, Step::kRetryScan);
                return true;
            }
            if(now_us - last_retry_ >= cfg_.retry_period_us)
            {
                // Try the whole ring again: reopen the break, then DISCOVER.
                last_retry_ = now_us;
                if(closed_)
                {
                    Command(SAT_CMD_RING_AUTO, closed_, 0, ex, SAT_FRAME_LEN(1));
                    waiting_ = Step::kRetryOpen;
                }
                else
                    Discover(ex, Step::kRetryScan);
                return true;
            }
            if(count_ == 0)
                return false; // nothing reachable to scan; just keep retrying
            // fall through: scan the satellites before the break
        case State::kScan:
            if(now_us - last_scan_ < cfg_.scan_period_us)
                return false;
            last_scan_ = now_us;
            Scan(ex);
            return true;
    }
    return false;
}

// --- Replies --------------------------------------------------------------------------

void SatChain::Discovered(const sat_descriptor* d, uint8_t n)
{
    bool same = n == count_;
    for(int i = 0; same && i < n; i++)
        same = d[i].kind == desc_[i].kind && d[i].slots == desc_[i].slots;
    if(!same)
        changed_ = true;

    uint8_t slot = 0;
    for(int i = 0; i < n; i++)
    {
        desc_[i]   = d[i];
        offset_[i] = slot;
        slot       = static_cast<uint8_t>(slot + d[i].slots);
    }
    count_ = n;
    slots_ = slot;
    if(!same)
        for(int k = 0; k < kMaxSatellites; k++)
            in_[k] = sat_slot_make(SAT_IN_EMPTY, 0, 0); // fresh: baselines reset on first scan
    misses_ = 0;
}

void SatChain::StartFindBreak(uint8_t from, bool settle)
{
    state_  = State::kFindBreak;
    probe_  = from;
    settle_ = settle;
    next_   = (closed_ && closed_ != probe_) ? Step::kReopen : Step::kClose;
}

void SatChain::Lost(uint32_t now_us)
{
    (void)now_us;
    state_          = State::kDiscover;
    discover_tries_ = 0;
}

void SatChain::Reply(const uint8_t* rx, size_t len, uint32_t now_us)
{
    Step step = waiting_;
    waiting_  = Step::kNone;

    sat_descriptor d[kMaxSatellites];
    uint8_t        n = 0;

    switch(step)
    {
        case Step::kNone: return;

        case Step::kDiscover:
        {
            sat_result r = sat_read_discover(rx, len, cfg_.fw_major, d, kMaxSatellites, &n);
            if(r == SAT_OK)
            {
                Discovered(d, n);
                state_     = State::kScan;
                closed_    = 0;
                last_scan_ = now_us - cfg_.scan_period_us; // scan straight away
            }
            else if(r == SAT_ERR_VERSION)
            {
                state_      = State::kVersion;
                last_retry_ = now_us;
            }
            else if(++discover_tries_ >= 3)
                StartFindBreak(1, false); // §5.1: DISCOVER failed three times
            return;
        }

        case Step::kScan:
        {
            if(sat_check_scan(rx, len, slots_, static_cast<uint8_t>(count_)) != SAT_OK)
            {
                bad_scans_++;
                if(++misses_ >= SAT_RING_LOST_AFTER)
                {
                    if(state_ == State::kPartial && closed_)
                        StartFindBreak(closed_, true); // re-close where we know it works
                    else
                        Lost(now_us);
                }
                return;
            }
            good_scans_++;
            misses_ = 0;
            for(int k = 0; k < slots_; k++)
            {
                sat_slot s = sat_slot_read(rx + SAT_HEADER_LEN + SAT_SLOT_LEN * k);
                if(!sat_slot_ok(s))
                    continue; // one bad slot keeps its last good value
                if(in_[k].kind == SAT_IN_EMPTY || in_[k].kind != s.kind)
                {
                    // First word from this slot: nothing has moved yet.
                    read_counter_[k] = s.d0;
                    read_presses_[k] = s.kind == SAT_IN_ENCODER ? static_cast<uint8_t>(s.d1 >> 4) : s.d1;
                }
                in_[k] = s;
            }
            return;
        }

        case Step::kReopen: // the reopened satellite answered or not; close the next
            closed_ = 0;
            next_   = Step::kClose;
            return;

        case Step::kClose:
            closed_ = probe_;
            next_   = Step::kProbe;
            return;

        case Step::kProbe:
        {
            sat_result r  = sat_read_discover(rx, len, cfg_.fw_major, d, kMaxSatellites, &n);
            bool       up = r == SAT_OK && n == probe_;
            if(settle_)
            {
                // Closed at the last satellite known to work: scan up to it.
                if(up)
                {
                    Discovered(d, n);
                    state_      = State::kPartial;
                    last_scan_  = now_us - cfg_.scan_period_us;
                    last_retry_ = now_us;
                    changed_    = true;
                }
                else // it's gone too: look again from the start
                    StartFindBreak(1, false);
                return;
            }
            if(up && probe_ < kMaxSatellites)
            {
                StartFindBreak(static_cast<uint8_t>(probe_ + 1), false); // reaches here: try one further
                return;
            }
            // Satellite `probe_` doesn't answer: the break is just before it.
            uint8_t last_good = static_cast<uint8_t>(probe_ - 1);
            if(last_good == 0)
            {
                Discovered(d, 0); // nothing reachable at all
                state_      = State::kPartial;
                closed_     = 0;
                last_retry_ = now_us;
                changed_    = true;
                return;
            }
            StartFindBreak(last_good, true);
            return;
        }

        case Step::kRetryOpen: // reopened: Poll() sends the whole-ring DISCOVER next
            next_ = Step::kRetryScan;
            return;

        case Step::kRetryScan:
        {
            sat_result r = sat_read_discover(rx, len, cfg_.fw_major, d, kMaxSatellites, &n);
            if(r == SAT_OK && (n > count_ || closed_ == 0))
            {
                Discovered(d, n); // fixed
                state_     = State::kScan;
                closed_    = 0;
                last_scan_ = now_us - cfg_.scan_period_us;
                changed_   = true;
            }
            else if(closed_)
                StartFindBreak(closed_, true); // still broken: close it again where it was
            else
                last_retry_ = now_us; // still nothing reachable
            return;
        }
    }
}

// --- Controls ---------------------------------------------------------------------

int SatChain::Slot(int i, int s) const
{
    if(i < 0 || i >= count_ || s < 0 || s >= desc_[i].slots)
        return -1;
    return offset_[i] + s;
}

sat_slot SatChain::Input(int i, int s) const
{
    int k = Slot(i, s);
    return k < 0 ? sat_slot_make(SAT_IN_EMPTY, 0, 0) : in_[k];
}

float SatChain::Pot(int i, int s) const
{
    sat_slot x = Input(i, s);
    return x.kind == SAT_IN_POT ? sat_pot_position(x) / 65535.0f : 0.0f;
}

int SatChain::EncoderSteps(int i, int s)
{
    int k = Slot(i, s);
    if(k < 0 || in_[k].kind != SAT_IN_ENCODER)
        return 0;
    int d = sat_delta8(in_[k].d0, read_counter_[k]);
    read_counter_[k] = in_[k].d0;
    return d;
}

bool SatChain::Pressed(int i, int s) const
{
    sat_slot x = Input(i, s);
    if(x.kind == SAT_IN_BUTTON)
        return x.d0 & 1;
    if(x.kind == SAT_IN_ENCODER)
        return x.d1 & 1;
    return false;
}

int SatChain::Presses(int i, int s)
{
    int k = Slot(i, s);
    if(k < 0)
        return 0;
    if(in_[k].kind == SAT_IN_BUTTON)
    {
        int d            = sat_delta8(in_[k].d1, read_presses_[k]);
        read_presses_[k] = in_[k].d1;
        return d;
    }
    if(in_[k].kind == SAT_IN_ENCODER)
    {
        uint8_t now      = static_cast<uint8_t>(in_[k].d1 >> 4);
        int     d        = sat_delta4(now, read_presses_[k]);
        read_presses_[k] = now;
        return d;
    }
    return 0;
}

int SatChain::SwitchPosition(int i, int s) const
{
    sat_slot x = Input(i, s);
    return x.kind == SAT_IN_SWITCH ? x.d0 : 0;
}

bool SatChain::Fault(int i, int s, uint8_t* code, uint8_t* detail) const
{
    sat_slot x = Input(i, s);
    if(x.kind != SAT_IN_FAULT)
        return false;
    if(code)
        *code = x.d0;
    if(detail)
        *detail = x.d1;
    return true;
}

void SatChain::SetLed(int i, uint8_t brightness, uint8_t mode)
{
    if(i < 0 || i >= kMaxSatellites)
        return;
    led_[i][0] = brightness;
    led_[i][1] = mode;
}

void SatChain::SetRgb(int i, int s, uint8_t r, uint8_t g, uint8_t b)
{
    if(i < 0 || i >= kMaxSatellites || s < 0 || s >= 4)
        return;
    rgb_[i][s] = sat_rgb565(r, g, b);
    rgb_set_[i] |= static_cast<uint8_t>(1u << s);
}
} // namespace rodeo
