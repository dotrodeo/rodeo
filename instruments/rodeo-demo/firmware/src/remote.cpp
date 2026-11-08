// rodeo-demo over MIDI. See remote.h and planning/RODEO_DEMO_MIDI_PROTOCOL.md.
#include "remote.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "daisy_seed.h"
#include "tusb.h"
#include "disk.h"
#include "panel.h"
#include "params.h"
#include "presets.h"
#include "samples.h"

#ifndef BUILD_ID
#define BUILD_ID "unknown"
#endif

namespace remote
{
namespace
{
constexpr uint8_t kHeader[]   = {0xF0, 0x7D, 0x52, 0x44}; // non-commercial ID, "RD"
constexpr uint8_t kVersion    = 1;
constexpr size_t  kPrefix     = sizeof kHeader + 2;       // and the version, the command
constexpr uint32_t kStreamMs  = 5000; // a stream lasts this long after the app last asked
constexpr uint32_t kPeriodMs  = 50;   // about 20 a second while anything changes
constexpr uint32_t kQuietMs   = 1000; // and stops a second after it last did

enum Cmd : uint8_t
{
    kHello   = 0x01,
    kState   = 0x02,
    kStream  = 0x03,
    kSet     = 0x04,
    kPresets = 0x05,
    kLoad    = 0x06,
    kSounds  = 0x07,
    kReply   = 0x40, // a reply is its command plus this
    kSaid    = 0x43,
    kError   = 0x7F,
};

void (*say)(const char*) = nullptr;

//------------- Out: whole messages, as USB-MIDI packets, in a queue -------------//

constexpr uint32_t kQueuePackets = 4096; // 16 KB: the longest list three times over
uint8_t            queue[kQueuePackets][4] DSY_SDRAM_BSS; // out of DTCM, which holds the stack
uint32_t           q_head = 0, q_tail = 0; // packets in [head, tail)
uint32_t           dropped = 0;

uint32_t QueueFree()
{
    return kQueuePackets - (q_tail - q_head);
}

// Queues a SysEx message of n bytes (F0 .. F7) as packets, all of it or none.
void Queue(const uint8_t* m, size_t n)
{
    uint32_t packets = (n + 2) / 3;
    if(!tud_midi_mounted() || packets > QueueFree())
    {
        dropped++;
        return;
    }
    while(n)
    {
        size_t   k     = n > 3 ? 3 : n;
        uint8_t* pk    = queue[q_tail % kQueuePackets];
        // CIN 0x4: SysEx starts or goes on (3 bytes); 0x5, 0x6, 0x7: it ends with 1, 2, 3
        pk[0]          = n > 3 ? 0x4 : (uint8_t)(0x4 + k);
        pk[1]          = m[0];
        pk[2]          = k > 1 ? m[1] : 0;
        pk[3]          = k > 2 ? m[2] : 0;
        m += k;
        n -= k;
        q_tail++;
    }
}

void Drain()
{
    if(!tud_midi_mounted())
    {
        q_head = q_tail; // gone with the host
        return;
    }
    while(q_head != q_tail && tud_midi_packet_write(queue[q_head % kQueuePackets]))
        q_head++;
}

// A message being built: the header, then text or bytes, then F7
char   msg[8192] DSY_SDRAM_BSS;
size_t msg_len = 0;
bool   msg_full = false;

void Begin(uint8_t cmd)
{
    memcpy(msg, kHeader, sizeof kHeader);
    msg[sizeof kHeader]     = kVersion;
    msg[sizeof kHeader + 1] = (char)cmd;
    msg_len                 = kPrefix;
    msg_full                = false;
}

// printf into the message, ASCII only: SysEx data bytes are 7-bit
void Add(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Add(const char* fmt, ...)
{
    if(msg_full)
        return;
    va_list a;
    va_start(a, fmt);
    size_t room = sizeof msg - msg_len - 1; // and F7
    int    w    = vsnprintf(msg + msg_len, room, fmt, a);
    va_end(a);
    if(w < 0 || (size_t)w >= room)
    {
        msg_full = true;
        return;
    }
    for(int i = 0; i < w; i++)
        if((uint8_t)msg[msg_len + i] >= 0x80)
            msg[msg_len + i] = '?';
    msg_len += w;
}

// A name as a JSON string's contents: no quotes, backslashes or controls
void AddName(const char* s)
{
    char   clean[64];
    size_t n = 0;
    for(; *s && n < sizeof clean - 1; s++)
        if(*s != '"' && *s != '\\' && (uint8_t)*s >= 0x20)
            clean[n++] = *s;
    clean[n] = 0;
    Add("%s", clean);
}

void End()
{
    if(msg_full)
    {
        // Too long for the buffer: say so instead of sending something cut short
        uint8_t cmd = (uint8_t)msg[sizeof kHeader + 1];
        Begin(kError);
        Add("{\"error\":\"reply 0x%02X too long\"}", cmd);
    }
    msg[msg_len++] = (char)0xF7;
    Queue((const uint8_t*)msg, msg_len);
}

void Error(const char* what, int x)
{
    char text[96];
    snprintf(text, sizeof text, what, x);
    Begin(kError);
    Add("{\"error\":\"%s\"}", text);
    End();
}

//------------- What Rodeo says -------------//

void Hello()
{
    Begin(kHello | kReply);
    Add("{\"fw\":\"rodeo-demo\",\"proto\":%d,\"build\":\"%s\",\"voices\":%d,\"params\":[",
        kVersion, BUILD_ID, synth::kVoices);
    for(int i = 0; i < params::Count(); i++)
    {
        const params::Info& info = params::At(i);
        Add("%s{\"id\":%d,\"key\":\"%s\",\"min\":%d,\"max\":%d", i ? "," : "", info.id,
            info.key, info.min, params::Max(info));
        if(info.unit[0])
            Add(",\"unit\":\"%s\"", info.unit);
        if(info.pot)
            Add(",\"pot\":%d", info.pot);
        if(info.names)
        {
            Add(",\"names\":[");
            for(int v = info.min; v <= info.max; v++)
                Add("%s\"%s\"", v > info.min ? "," : "", info.names[v - info.min]);
            Add("]");
        }
        Add("}");
    }
    Add("]}");
    End();
}

// The state, without the CPU figure (which changes all the time); returns its
// hash, for the stream to tell when something changed
uint32_t StateBody(const synth::Params& p)
{
    samples::Stats s    = samples::GetStats();
    int            card = disk::Active() ? 2 : s.card ? 1 : 0;
    Begin(kState | kReply);
    Add("{\"preset\":%d,\"name\":\"", panel::Preset());
    AddName(panel::Preset() >= 0 ? presets::Name(panel::Preset()) : "");
    Add("\",\"p\":{");
    for(int i = 0; i < params::Count(); i++)
    {
        const params::Info& info = params::At(i);
        Add("%s\"%s\":%d", i ? "," : "", info.key, params::Get(info, p));
    }
    Add("},\"hold\":%u,\"voices\":%d,\"card\":%d,\"sounds\":%d,\"loaded\":%d,\"samples\":%d,"
        "\"panel\":%d",
        panel::Held(), synth::Active(), card, s.sounds, s.loaded, s.samples,
        panel::Found() ? 1 : 0);
    uint32_t hash = 2166136261u; // FNV-1a
    for(size_t i = 0; i < msg_len; i++)
        hash = (hash ^ (uint8_t)msg[i]) * 16777619u;
    return hash;
}

void PresetList()
{
    Begin(kPresets | kReply);
    Add("{\"presets\":[");
    for(int i = 0; i < presets::Count(); i++)
    {
        Add("%s\"", i ? "," : "");
        AddName(presets::Name(i));
        Add("\"");
    }
    Add("],\"current\":%d}", panel::Preset());
    End();
}

void SoundList()
{
    Begin(kSounds | kReply);
    Add("{\"sounds\":[");
    for(int i = 0; i < samples::Sounds(); i++)
    {
        const samples::Sound& s = samples::GetSound(i);
        Add("%s{\"name\":\"", i ? "," : "");
        AddName(s.name);
        Add("\",\"kit\":%d,\"ready\":%d}", s.kit ? 1 : 0, s.ready ? 1 : 0);
    }
    Add("]}");
    End();
}

//------------- What the app says -------------//

bool     state_wanted = false; // one state message, from the next Task()
uint32_t stream_until = 0;     // the stream runs until then (System::GetNow())
bool     streaming    = false;
bool     version_said = false;

void Command(const uint8_t* m, size_t n, synth::Params& p)
{
    // m is the whole message, F0 to F7
    if(n < kPrefix + 1 || memcmp(m, kHeader, sizeof kHeader))
        return; // not ours
    if(m[sizeof kHeader] != kVersion)
    {
        if(!version_said)
            Error("protocol version %d; this Rodeo speaks 1", m[sizeof kHeader]);
        version_said = true;
        return;
    }
    uint8_t        cmd  = m[sizeof kHeader + 1];
    const uint8_t* data = m + kPrefix;
    size_t         len  = n - kPrefix - 1; // less F7
    switch(cmd)
    {
        case kHello: Hello(); break;
        case kState: state_wanted = true; break;
        case kStream:
            if(len >= 1 && data[0])
            {
                if(!streaming)
                    state_wanted = true; // straight away, then as things change
                streaming    = true;
                stream_until = daisy::System::GetNow() + kStreamMs;
            }
            else
                streaming = false;
            break;
        case kSet:
            for(size_t i = 0; i + 3 <= len; i += 3)
            {
                const params::Info* info = params::Find(data[i]);
                if(!info)
                {
                    Error("no parameter %d", data[i]);
                    continue;
                }
                int v = info->min + (data[i + 1] | (data[i + 2] << 7));
                if(const char* said = panel::Set(info->id, v, p))
                    say(said);
            }
            break;
        case kPresets: PresetList(); break;
        case kLoad:
            if(len >= 2)
            {
                int i = data[0] | (data[1] << 7);
                if(const char* said = panel::LoadPreset(i, p))
                    say(said);
                else
                    Error("no preset %d", i);
            }
            break;
        case kSounds: SoundList(); break;
        default: Error("unknown command 0x%02X", cmd); break;
    }
}

uint8_t in[256]; // a SysEx message coming in
size_t  in_len  = 0;
bool    in_over = false; // longer than in: dropped
} // namespace

void Init(void (*s)(const char*))
{
    say = s;
}

bool Packet(const uint8_t packet[4], synth::Params& p)
{
    uint8_t cin = packet[0] & 0x0F;
    if(cin < 0x4 || cin > 0x7)
        return false;
    // 0x5 is also a one-byte System Common message: ours only when it ends a SysEx
    if(cin == 0x5 && packet[1] != 0xF7)
        return false;
    size_t k = cin == 0x4 ? 3 : cin - 0x4;
    for(size_t i = 0; i < k; i++)
    {
        uint8_t b = packet[1 + i];
        if(b == 0xF0)
        {
            in_len  = 0;
            in_over = false;
        }
        if(in_len < sizeof in)
            in[in_len++] = b;
        else
            in_over = true;
        if(b == 0xF7)
        {
            if(!in_over && in_len > 0 && in[0] == 0xF0)
                Command(in, in_len, p);
            in_len  = 0;
            in_over = false;
        }
    }
    return true;
}

bool Cc(int cc, int v, synth::Params& p)
{
    float x  = v / 127.f;
    int   id = -1, value = 0;
    // Choices: the 128 values split evenly over them
    auto pick = [&](int n) { return n > 0 ? (v * n) / 128 : 0; };
    switch(cc)
    {
        case 74: id = 0, value = (int)lroundf(x * 1000.f); break;
        case 71: id = 1, value = (int)lroundf(x * 1000.f); break;
        case 79: id = 2, value = (int)lroundf(x * 1000.f); break;
        case 72: id = 3, value = (int)lroundf(5.f * powf(800.f, x)); break; // as pot 4
        case 73: id = 4, value = (int)lroundf(powf(4000.f, x)); break;      // as E3
        case 75: id = 16, value = (int)lroundf(powf(10000.f, x)); break;
        case 94: id = 5, value = (int)lroundf(x * 50.f); break;
        case 91: id = 7, value = (int)lroundf(x * 100.f); break;
        case 102: id = 6, value = pick(5) - 2; break;
        case 103: id = 11, value = pick(synth::kShapes); break;
        case 104: id = 10, value = pick(3); break;
        case 105: id = 12, value = pick(samples::Sounds()); break;
        case 106: id = 8, value = v >= 64; break;
        case 107: id = 9, value = pick(3); break;
        default: return false;
    }
    if(const char* said = panel::Set(id, value, p))
        say(said);
    return true;
}

void Task(const synth::Params& p, int cpu)
{
    static uint32_t last_hash = 0, last_change = 0, last_sent = 0;
    uint32_t        now       = daisy::System::GetNow();
    if(!tud_midi_mounted())
        streaming = false;
    if(streaming && (int32_t)(now - stream_until) > 0)
        streaming = false; // the app stopped asking
    if(state_wanted)
    {
        state_wanted = false;
        last_sent    = now;
        last_change  = now;
        last_hash    = StateBody(p);
        Add(",\"cpu\":%d}", cpu);
        End();
    }
    else if(streaming && now - last_sent >= kPeriodMs)
    {
        last_sent     = now;
        uint32_t hash = StateBody(p);
        if(hash != last_hash)
        {
            last_hash   = hash;
            last_change = now;
        }
        if(now - last_change < kQuietMs)
        {
            Add(",\"cpu\":%d}", cpu);
            End();
        }
    }
    Drain();
}

void Said(const char* s)
{
    if(!streaming)
        return;
    Begin(kSaid);
    Add("{\"said\":\"");
    AddName(s);
    Add("\"}");
    End();
}
} // namespace remote
