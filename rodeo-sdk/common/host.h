#pragma once
//
// The host protocol: what a Rodeo firmware tells a computer about itself, over
// USB-MIDI SysEx. With it, tools/rodeo-console (and the phone app) shows any
// firmware's parameters and satellite chain, changes the parameters, and
// reboots it for flashing, without knowing anything about that firmware.
//
// The firmware declares its parameters in a table and says how to read and set
// them (HostApp). The Host answers the rest:
//
//   hello    the firmware's name, build, protocol version and parameter table
//   state    every parameter's value, and which pots are held
//   stream   state and chain, sent as they change, while the computer asks
//   set      one or more parameters, 14 bits each
//   chain    the satellites, their kinds and firmware, every control's reading
//   dfu      reboot into the bootloader for the next flash
//
// The wire format is docs/guides/host.mdx. Every message is
//
//   F0 7D 52 44 <version> <command> <payload> F7
//
// under the MIDI spec's non-commercial ID (7D). The protocol is SysEx only: it
// never sends or claims a note, a CC or a program change, so a firmware's own
// MIDI map is untouched. Replies are JSON, ASCII only.
//
// What it costs: about 4 KB of code, the memory the firmware gives it (24 KB
// suggested, best in SDRAM), and no CPU at idle. While the computer streams,
// the state and the chain are formatted in Task(), from the main loop, never
// the audio callback, about 20 times a second while they change.
//
// Build with RODEO_HOST=0 (`make RODEO_HOST=0`, or in the Makefile) and the
// Host compiles to nothing: the calls stay in the firmware and do nothing.
//
// Like SatChain this is pure logic, host-tested in common/tests: bytes in from
// Receive(), packets out through Peek()/Pop(). host_usb.h moves them over
// TinyUSB's MIDI class:
//
//   uint8_t pk[4];
//   while(tud_midi_packet_read(pk))
//       if(!host.Receive(pk, System::GetNow()))
//           MyMidi(pk);                    // everything that isn't the protocol
//   rodeo::HostUsbTask(host, System::GetNow(), cpu_percent);
//   if(host.DfuRequested())
//       ResetToDfu();                      // dfu.h, after stopping the audio
//
#include <stddef.h>
#include <stdint.h>

#include "sat_chain.h"

#ifndef RODEO_HOST
#define RODEO_HOST 1
#endif

namespace rodeo
{
class Host;

/** One parameter a computer may read and set. Values are whole numbers in the
 *  parameter's own units; max - min is at most 16383 (14 bits on the wire). */
struct HostParam
{
    /** The number a set addresses it by, 0..127. Keep it stable across builds:
     *  a saved setting refers to it. */
    uint8_t id;
    /** A short name, lower case ASCII, unique in the table: "cutoff". */
    const char* key;
    int16_t min, max;
    /** What the firmware starts with, and what a preset that leaves the
     *  parameter out goes back to. */
    int16_t def;
    /** "ms", "%", "Hz"; "" for none. ASCII, or a JSON escape ("\\u2030" for ‰). */
    const char* unit;
    /** For a choice, max - min + 1 names; otherwise null. */
    const char* const* names;
    /** The pot it's on, 1..32, if any: its hold bit is pot - 1 (HostApp::Held). */
    uint8_t pot;
};

/** A reply being written: JSON text, kept 7-bit. */
class HostWriter
{
  public:
    /** printf into the message. Anything at 0x80 or above becomes '?'. */
    void Add(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    /** A string's contents, for between quotes: no quotes, backslashes or
     *  control characters, at most 63 bytes. */
    void Name(const char* s);

  private:
    friend class Host;
    char*  buf_  = nullptr;
    size_t size_ = 0, len_ = 0;
    bool   full_ = false;
};

/** What the firmware gives the Host: its parameters' values, and anything of
 *  its own it wants to say or answer. */
class HostApp
{
  public:
    /** The parameter's value now, in its units. */
    virtual int Get(const HostParam& p) = 0;
    /** Set it. v is already within min and Max(p). */
    virtual void Set(const HostParam& p, int v) = 0;
    /** The highest value it can take now, if that changes (a list of sounds on
     *  a card); p.max by default. */
    virtual int Max(const HostParam& p) { return p.max; }
    /** Which pots are held, bit pot - 1: a preset holds a pot's parameter
     *  until the pot is moved through it. 0 for a firmware without presets. */
    virtual uint32_t Held() { return 0; }
    /** More fields for hello and state, each written with a leading comma:
     *  w.Add(",\"voices\":%d", 8). */
    virtual void Hello(HostWriter& w) { (void)w; }
    virtual void State(HostWriter& w) { (void)w; }
    /** A command of the firmware's own, 0x20..0x3E. Reply with host.Begin(cmd
     *  | Host::kReply), write, host.End(). Return false if it isn't one. */
    virtual bool Command(uint8_t cmd, const uint8_t* data, size_t len, Host& host)
    {
        (void)cmd, (void)data, (void)len, (void)host;
        return false;
    }

  protected:
    ~HostApp() = default;
};

class Host
{
  public:
    static constexpr uint8_t kProtocol = 2;
    /** A reply's command is the request's plus this. */
    static constexpr uint8_t kReply = 0x40;
    /** The memory to give it: a third for the message being written, the rest
     *  a queue of messages waiting for the computer. Enough for 64 parameters.
     *  Compiled out, it is 4 bytes, so the firmware's buffer costs nothing. */
    static constexpr size_t kMemory = RODEO_HOST ? 24 * 1024 : 4;

    enum Cmd : uint8_t
    {
        kHello  = 0x01,
        kState  = 0x02,
        kStream = 0x03, // payload: 1 state, 2 chain, 3 both; 0 stops
        kSet    = 0x04, // payload: id, value - min as two 7-bit bytes; repeated
        kChain  = 0x08,
        kDfu    = 0x09,
        kAppFirst = 0x20, // 0x20..0x3E are the firmware's (HostApp::Command)
        kAppLast  = 0x3E,
        // Rodeo to computer only
        kSaid  = kStream | 0x40, // what just changed, in words, while streaming
        kError = 0x7F,           // what went wrong with a request
    };

    struct Config
    {
        const char*      firmware = "rodeo";   // "rodeo-demo"
        const char*      build    = "unknown"; // a commit or a version
        const HostParam* params   = nullptr;
        int              count    = 0;
        HostApp*         app      = nullptr; // required
        /** The satellite chain, for the chain report; null if there is none. */
        const SatChain* chain = nullptr;
        /** kMemory bytes, or what fits. SDRAM suits it (DSY_SDRAM_BSS): under
         *  the bootloader, internal RAM is the stack's and the code's. */
        uint8_t* memory      = nullptr;
        size_t   memory_size = 0;
    };

    void Init(const Config& cfg);

    /** A USB-MIDI event packet. True if it was SysEx (CIN 4..7), which is the
     *  Host's whether or not it was meant for this protocol; false for
     *  anything else, which is the firmware's. */
    bool Receive(const uint8_t packet[4], uint32_t now_ms);

    /** From the main loop: tracks the chain, and queues the state and chain
     *  while the computer is streaming them. */
    void Task(uint32_t now_ms, int cpu_percent);

    /** Whether a computer is there to read. Without one, nothing is queued and
     *  a stream stops. */
    void Connected(bool yes);

    /** The next packet to send, if any. Pop() it once it's been taken. */
    bool Peek(uint8_t packet[4]) const;
    void Pop();

    /** What just changed, in words ("cutoff: 41 %"), to a computer that is
     *  streaming. */
    void Say(const char* s);

    /** True once, after the computer asked for the bootloader and the reply
     *  has gone. The firmware stops what it must and calls ResetToDfu(). */
    bool DfuRequested();

    bool Streaming() const { return stream_ != 0; }
    /** Messages dropped because the queue was full or no one was there. */
    uint32_t Dropped() const { return dropped_; }
    const HostParam* Find(int id) const;

    // For HostApp::Command's replies.
    HostWriter& Begin(uint8_t cmd);
    void        End();
    void        Error(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

  private:
    void     Command(const uint8_t* m, size_t n, uint32_t now_ms);
    void     Hello();
    uint32_t StateBody();
    uint32_t ChainBody();
    void     Track();
    void     Queue(const uint8_t* m, size_t n);
    size_t   QueueFree() const { return q_size_ - (q_tail_ - q_head_); }
    uint8_t  At(uint32_t i) const { return q_[i % q_size_]; }
    size_t   Chunk() const;

    Config     cfg_;
    HostWriter w_;
    uint8_t*   q_      = nullptr;
    size_t     q_size_ = 0;
    uint32_t   q_head_ = 0, q_tail_ = 0;
    uint32_t   dropped_ = 0;
    bool       connected_ = false;

    uint8_t  in_[256];
    size_t   in_len_  = 0;
    bool     in_over_ = false;
    bool     version_said_ = false;

    uint8_t  stream_ = 0;       // kStream's bits
    uint32_t stream_until_ = 0; // it lapses then, unless asked again
    bool     state_wanted_ = false, chain_wanted_ = false;
    uint32_t state_hash_ = 0, state_sent_ = 0, state_changed_ = 0;
    uint32_t chain_hash_ = 0, chain_sent_ = 0;
    bool     dfu_ = false;

    // The chain report's view of the controls: encoder totals and pots with
    // hysteresis, so a resting pot doesn't keep the stream busy.
    int     sats_ = -1;
    uint8_t kinds_[SatChain::kMaxSatellites];
    uint8_t raw_[SAT_MAX_SLOTS];
    int     total_[SAT_MAX_SLOTS];
    int     pot_[SAT_MAX_SLOTS];
};

#if !RODEO_HOST
// Compiled out: the same calls, doing nothing. host.cpp is empty.
inline void HostWriter::Add(const char*, ...) {}
inline void HostWriter::Name(const char*) {}
inline void Host::Init(const Config&) {}
inline bool Host::Receive(const uint8_t[4], uint32_t) { return false; }
inline void Host::Task(uint32_t, int) {}
inline void Host::Connected(bool) {}
inline bool Host::Peek(uint8_t[4]) const { return false; }
inline void Host::Pop() {}
inline void Host::Say(const char*) {}
inline bool Host::DfuRequested() { return false; }
inline const HostParam* Host::Find(int) const { return nullptr; }
inline HostWriter& Host::Begin(uint8_t) { return w_; }
inline void Host::End() {}
inline void Host::Error(const char*, ...) {}
#endif
} // namespace rodeo
