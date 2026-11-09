// The host protocol. See host.h, and docs/guides/host.mdx for the wire format.
#include "host.h"

#if RODEO_HOST
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "version.h"

namespace rodeo
{
namespace
{
constexpr uint8_t  kHeader[]  = {0xF0, 0x7D, 0x52, 0x44}; // non-commercial ID, "RD"
constexpr size_t   kPrefix    = sizeof kHeader + 2;      // and the version, the command
constexpr uint32_t kStreamMs  = 5000; // a stream lasts this long after it was last asked for
constexpr uint32_t kPeriodMs  = 50;   // about 20 a second while anything changes
constexpr uint32_t kQuietMs   = 1000; // the state stops a second after it last changed
constexpr uint32_t kChainMs   = 1000; // the chain goes at least this often (scan counts)
constexpr int      kPotHysteresis = 3; // in 1000: a pot moves on once it's this far

constexpr uint8_t kStreamState = 1, kStreamChain = 2;

uint32_t Fnv(const char* s, size_t n)
{
    uint32_t h = 2166136261u;
    for(size_t i = 0; i < n; i++)
        h = (h ^ (uint8_t)s[i]) * 16777619u;
    return h;
}

const char* StateWord(SatChain::State s)
{
    switch(s)
    {
        case SatChain::State::kPowerWait: return "powerwait";
        case SatChain::State::kDiscover: return "discover";
        case SatChain::State::kScan: return "scan";
        case SatChain::State::kFindBreak: return "findbreak";
        case SatChain::State::kPartial: return "partial";
        case SatChain::State::kVersion: return "version";
    }
    return "?";
}

int Permille(float x)
{
    return (int)(x * 1000.f + 0.5f);
}
} // namespace

//------------- Writing a message -------------//

void HostWriter::Add(const char* fmt, ...)
{
    if(full_ || !buf_)
        return;
    va_list a;
    va_start(a, fmt);
    size_t room = size_ - len_ - 1; // and F7
    int    w    = vsnprintf(buf_ + len_, room, fmt, a);
    va_end(a);
    if(w < 0 || (size_t)w >= room)
    {
        full_ = true;
        return;
    }
    for(int i = 0; i < w; i++)
        if((uint8_t)buf_[len_ + i] >= 0x80)
            buf_[len_ + i] = '?';
    len_ += w;
}

void HostWriter::Name(const char* s)
{
    char   clean[64];
    size_t n = 0;
    for(; s && *s && n < sizeof clean - 1; s++)
        if(*s != '"' && *s != '\\' && (uint8_t)*s >= 0x20)
            clean[n++] = *s;
    clean[n] = 0;
    Add("%s", clean);
}

HostWriter& Host::Begin(uint8_t cmd)
{
    memcpy(w_.buf_, kHeader, sizeof kHeader);
    w_.buf_[sizeof kHeader]     = kProtocol;
    w_.buf_[sizeof kHeader + 1] = (char)cmd;
    w_.len_                     = kPrefix;
    w_.full_                    = false;
    return w_;
}

void Host::End()
{
    if(w_.full_)
    {
        // Too long for the buffer: say so rather than send it cut short
        uint8_t cmd = (uint8_t)w_.buf_[sizeof kHeader + 1];
        Begin(kError);
        w_.Add("{\"error\":\"reply 0x%02X too long\"}", cmd);
    }
    w_.buf_[w_.len_++] = (char)0xF7;
    Queue((const uint8_t*)w_.buf_, w_.len_);
}

void Host::Error(const char* fmt, ...)
{
    char    text[96];
    va_list a;
    va_start(a, fmt);
    vsnprintf(text, sizeof text, fmt, a);
    va_end(a);
    Begin(kError);
    w_.Add("{\"error\":\"");
    w_.Name(text);
    w_.Add("\"}");
    End();
}

//------------- The queue: whole messages, sent as USB-MIDI packets -------------//

void Host::Queue(const uint8_t* m, size_t n)
{
    // All of it or none, so the computer never sees half a message
    if(!connected_ || n > QueueFree())
    {
        dropped_++;
        return;
    }
    for(size_t i = 0; i < n; i++)
        q_[(q_tail_ + i) % q_size_] = m[i];
    q_tail_ += n;
}

// Bytes in the next packet: three, or fewer when the message ends sooner
size_t Host::Chunk() const
{
    size_t k = 0;
    while(k < 3 && q_head_ + k != q_tail_)
        if(At(q_head_ + k++) == 0xF7)
            break;
    return k;
}

bool Host::Peek(uint8_t packet[4]) const
{
    size_t k = Chunk();
    if(k == 0)
        return false;
    bool end = At(q_head_ + k - 1) == 0xF7;
    // Cable 0. CIN 0x4: SysEx starts or goes on; 0x5, 0x6, 0x7: it ends with 1, 2, 3
    packet[0] = end ? (uint8_t)(0x4 + k) : 0x4;
    for(size_t i = 0; i < 3; i++)
        packet[1 + i] = i < k ? At(q_head_ + i) : 0;
    return true;
}

void Host::Pop()
{
    q_head_ += Chunk();
}

void Host::Connected(bool yes)
{
    if(!yes)
    {
        q_head_ = q_tail_; // gone with the computer
        stream_ = 0;
    }
    connected_ = yes;
}

//------------- What Rodeo says -------------//

void Host::Init(const Config& cfg)
{
    cfg_ = cfg;
    // A third for the message, the rest for the queue
    size_t msg = cfg.memory_size / 3;
    w_.buf_    = (char*)cfg.memory;
    w_.size_   = msg;
    q_         = cfg.memory + msg;
    q_size_    = cfg.memory_size - msg;
    q_head_ = q_tail_ = 0;
    sats_             = -1;
}

const HostParam* Host::Find(int id) const
{
    for(int i = 0; i < cfg_.count; i++)
        if(cfg_.params[i].id == id)
            return &cfg_.params[i];
    return nullptr;
}

void Host::Hello()
{
    HostWriter& w = Begin(kHello | kReply);
    w.Add("{\"fw\":\"");
    w.Name(cfg_.firmware);
    w.Add("\",\"build\":\"");
    w.Name(cfg_.build);
    w.Add("\",\"proto\":%d,\"sdk\":\"%s\",\"chain\":%d,\"params\":[", kProtocol,
          kSdkVersionString, cfg_.chain ? 1 : 0);
    for(int i = 0; i < cfg_.count; i++)
    {
        const HostParam& p = cfg_.params[i];
        w.Add("%s{\"id\":%d,\"key\":\"", i ? "," : "", p.id);
        w.Name(p.key);
        w.Add("\",\"min\":%d,\"max\":%d,\"def\":%d", p.min, cfg_.app->Max(p), p.def);
        if(p.unit && p.unit[0])
            w.Add(",\"unit\":\"%s\"", p.unit); // may be a JSON escape: not Name()
        if(p.pot)
            w.Add(",\"pot\":%d", p.pot);
        if(p.names)
        {
            w.Add(",\"names\":[");
            for(int v = p.min; v <= p.max; v++)
            {
                w.Add("%s\"", v > p.min ? "," : "");
                w.Name(p.names[v - p.min]);
                w.Add("\"");
            }
            w.Add("]");
        }
        w.Add("}");
    }
    w.Add("]");
    cfg_.app->Hello(w);
    w.Add("}");
    End();
}

// The state, less the CPU figure, which changes all the time; returns its hash
uint32_t Host::StateBody()
{
    HostWriter& w = Begin(kState | kReply);
    w.Add("{\"p\":{");
    for(int i = 0; i < cfg_.count; i++)
    {
        const HostParam& p = cfg_.params[i];
        w.Add("%s\"", i ? "," : "");
        w.Name(p.key);
        w.Add("\":%d", cfg_.app->Get(p));
    }
    w.Add("},\"hold\":%lu", (unsigned long)cfg_.app->Held());
    cfg_.app->State(w);
    return Fnv(w.buf_, w.len_);
}

// The chain, less its scan counts; returns its hash
uint32_t Host::ChainBody()
{
    const SatChain& c = *cfg_.chain;
    HostWriter&     w = Begin(kChain | kReply);
    w.Add("{\"state\":\"%s\",\"sats\":%d,\"broken\":%d,\"s\":[", StateWord(c.GetState()),
          c.Satellites(), c.BrokenAfter());
    int k = 0;
    for(int i = 0; i < c.Satellites(); i++)
    {
        const sat_descriptor& d       = c.Satellite(i);
        const bool            tracked = i < sats_ && kinds_[i] == d.kind; // Track() saw it
        w.Add("%s{\"kind\":%u,\"fw\":\"%u.%u\",\"in\":[", i ? "," : "", d.kind, d.fw_major,
              d.fw_minor);
        bool encoder = false, fault = false;
        for(int s = 0; s < d.slots; s++)
        {
            const bool here = tracked && k + s < (int)SAT_MAX_SLOTS;
            sat_slot   x    = c.Input(i, s);
            int        v    = 0;
            switch(x.kind)
            {
                case SAT_IN_POT: v = here ? pot_[k + s] : Permille(c.Pot(i, s)); break;
                case SAT_IN_ENCODER:
                    v       = here ? total_[k + s] : 0;
                    encoder = true;
                    break;
                case SAT_IN_BUTTON: v = c.Pressed(i, s); break;
                case SAT_IN_SWITCH: v = c.SwitchPosition(i, s); break;
                case SAT_IN_FAULT: fault = true; break;
                default: break;
            }
            w.Add("%s%d", s ? "," : "", v);
        }
        w.Add("]");
        // Buttons are already in in[]; only an encoder's push switch needs btn[]
        if(encoder)
        {
            w.Add(",\"btn\":[");
            for(int s = 0; s < d.slots; s++)
                w.Add("%s%d", s ? "," : "", c.Pressed(i, s) ? 1 : 0);
            w.Add("]");
        }
        if(fault)
        {
            w.Add(",\"fault\":[");
            for(int s = 0; s < d.slots; s++)
            {
                uint8_t code, detail;
                if(c.Fault(i, s, &code, &detail))
                    w.Add("%s[%u,%u]", s ? "," : "", code, detail);
                else
                    w.Add("%snull", s ? "," : "");
            }
            w.Add("]");
        }
        w.Add("}");
        k += d.slots;
    }
    w.Add("]");
    return Fnv(w.buf_, w.len_);
}

// Encoders as running totals from each slot's raw counter (EncoderSteps() would
// take the steps from the firmware), pots with hysteresis. The totals start
// again at 0 when the chain's layout changes.
void Host::Track()
{
    const SatChain& c    = *cfg_.chain;
    bool            same = c.Satellites() == sats_;
    for(int i = 0; same && i < sats_; i++)
        same = c.Satellite(i).kind == kinds_[i];
    if(!same)
    {
        sats_ = c.Satellites();
        for(int i = 0; i < sats_; i++)
            kinds_[i] = c.Satellite(i).kind;
    }
    int k = 0;
    for(int i = 0; i < sats_; i++)
        for(int s = 0; s < c.Satellite(i).slots && k < (int)SAT_MAX_SLOTS; s++, k++)
        {
            sat_slot x = c.Input(i, s);
            int      p = Permille(c.Pot(i, s));
            if(!same)
            {
                raw_[k]   = x.d0;
                total_[k] = 0;
                pot_[k]   = p;
            }
            else if(x.kind == SAT_IN_ENCODER)
            {
                total_[k] += sat_delta8(x.d0, raw_[k]);
                raw_[k] = x.d0;
            }
            else if(x.kind == SAT_IN_POT
                    && (p >= pot_[k] + kPotHysteresis || p <= pot_[k] - kPotHysteresis
                        || p == 0 || p == 1000))
                pot_[k] = p;
        }
}

void Host::Task(uint32_t now, int cpu)
{
    if(cfg_.chain)
        Track();
    if(stream_ && (int32_t)(now - stream_until_) > 0)
        stream_ = 0; // the computer stopped asking

    if(state_wanted_)
    {
        state_wanted_  = false;
        state_hash_    = StateBody();
        state_sent_    = now;
        state_changed_ = now;
        w_.Add(",\"cpu\":%d}", cpu);
        End();
    }
    else if((stream_ & kStreamState) && now - state_sent_ >= kPeriodMs)
    {
        state_sent_ = now; // looked at, whether or not it goes
        uint32_t h  = StateBody();
        if(h != state_hash_)
        {
            state_hash_    = h;
            state_changed_ = now;
        }
        if(now - state_changed_ < kQuietMs)
        {
            w_.Add(",\"cpu\":%d}", cpu);
            End();
        }
    }

    if(!cfg_.chain)
        chain_wanted_ = false;
    else if(chain_wanted_
            || ((stream_ & kStreamChain) && now - chain_sent_ >= kPeriodMs))
    {
        uint32_t h = ChainBody();
        if(chain_wanted_ || h != chain_hash_ || now - chain_sent_ >= kChainMs)
        {
            w_.Add(",\"good\":%lu,\"bad\":%lu}", (unsigned long)cfg_.chain->GoodScans(),
                   (unsigned long)cfg_.chain->BadScans());
            End();
            chain_hash_   = h;
            chain_sent_   = now;
            chain_wanted_ = false;
        }
    }
}

void Host::Say(const char* s)
{
    if(!stream_)
        return;
    HostWriter& w = Begin(kSaid);
    w.Add("{\"said\":\"");
    w.Name(s);
    w.Add("\"}");
    End();
}

bool Host::DfuRequested()
{
    // Once the reply has gone, or there's no one left to take it
    if(!dfu_ || (connected_ && q_head_ != q_tail_))
        return false;
    dfu_ = false;
    return true;
}

//------------- What the computer says -------------//

void Host::Command(const uint8_t* m, size_t n, uint32_t now)
{
    // m is the whole message, F0 to F7
    if(n < kPrefix + 1 || memcmp(m, kHeader, sizeof kHeader))
        return; // not ours
    if(m[sizeof kHeader] != kProtocol)
    {
        if(!version_said_)
            Error("protocol %d; this Rodeo speaks 2", m[sizeof kHeader]);
        version_said_ = true;
        return;
    }
    uint8_t        cmd  = m[sizeof kHeader + 1];
    const uint8_t* data = m + kPrefix;
    size_t         len  = n - kPrefix - 1; // less F7
    switch(cmd)
    {
        case kHello: Hello(); break;
        case kState: state_wanted_ = true; break;
        case kChain:
            if(cfg_.chain)
                chain_wanted_ = true;
            else
                Error("no satellite chain");
            break;
        case kStream:
        {
            uint8_t bits = len >= 1 ? data[0] & (kStreamState | kStreamChain) : 0;
            if(!cfg_.chain)
                bits &= ~kStreamChain;
            // Straight away what wasn't streaming yet, then as things change
            if((bits & kStreamState) && !(stream_ & kStreamState))
                state_wanted_ = true;
            if((bits & kStreamChain) && !(stream_ & kStreamChain))
                chain_wanted_ = true;
            stream_       = bits;
            stream_until_ = now + kStreamMs;
            break;
        }
        case kSet:
            for(size_t i = 0; i + 3 <= len; i += 3)
            {
                const HostParam* p = Find(data[i]);
                if(!p)
                {
                    Error("no parameter %d", data[i]);
                    continue;
                }
                int v  = p->min + (data[i + 1] | (data[i + 2] << 7));
                int hi = cfg_.app->Max(*p);
                cfg_.app->Set(*p, v > hi ? hi : v);
            }
            break;
        case kDfu:
            Begin(kDfu | kReply).Add("{\"dfu\":1}");
            End();
            dfu_ = true;
            break;
        default:
            if(cmd < kAppFirst || cmd > kAppLast
               || !cfg_.app->Command(cmd, data, len, *this))
                Error("unknown command 0x%02X", cmd);
            break;
    }
}

bool Host::Receive(const uint8_t packet[4], uint32_t now)
{
    uint8_t cin = packet[0] & 0x0F;
    if(cin < 0x4 || cin > 0x7)
        return false;
    // 0x5 is also a one-byte System Common message: SysEx only when it's F7
    if(cin == 0x5 && packet[1] != 0xF7)
        return false;
    size_t k = cin == 0x4 ? 3 : cin - 0x4;
    for(size_t i = 0; i < k; i++)
    {
        uint8_t b = packet[1 + i];
        if(b == 0xF0)
        {
            in_len_  = 0;
            in_over_ = false;
        }
        if(in_len_ < sizeof in_)
            in_[in_len_++] = b;
        else
            in_over_ = true;
        if(b == 0xF7)
        {
            if(!in_over_ && in_len_ > 0 && in_[0] == 0xF0)
                Command(in_, in_len_, now);
            in_len_  = 0;
            in_over_ = false;
        }
    }
    return true;
}
} // namespace rodeo
#endif // RODEO_HOST
