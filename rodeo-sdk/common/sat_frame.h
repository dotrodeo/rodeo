#pragma once
//
// The satellite chain's wire format: frames, slots, CRC, slot kinds, discovery
// and commands, as both ends of the chain build and parse them.
//
// The contract is hardware/rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md;
// section numbers below (§2.1) point into it. This header is the contract in code,
// and BOTH firmwares include it: the Seed's chain driver in this SDK, and the
// satellite firmware in rodeo-satellite/ (by path). A byte layout lives here once,
// so the two ends cannot drift, and common/tests/sat_frame_gtest.cpp exercises it
// from both sides, down to a simulated chain of satellites.
//
// Plain C99 with static inline functions, because the satellites are built with
// ch32v003fun, a C toolchain, on a part with 16 KB of flash and 2 KB of RAM. No
// allocation, no tables, nothing that isn't used costs anything. C++ includes it
// unchanged. Pure logic: no hardware, no clock, no libDaisy.
//
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// --- Line and timing (§1.2, §6) ----------------------------------------------

#define SAT_BAUD 1000000u              // 8N1, so a byte takes 10 us
#define SAT_IDLE_GAP_BYTES 2u          // a gap this long starts (or aborts) a frame
#define SAT_SATELLITE_BOOT_MS 20u      // a satellite passes frames this soon after power
#define SAT_FIRST_DISCOVER_MS 50u      // the Seed waits this long after power
#define SAT_SCAN_PERIOD_MS 4u          // default: 250 scans a second
#define SAT_RING_LOST_AFTER 3u         // missed or wrong SCAN replies in a row

// --- Frame layout (§2.1) -------------------------------------------------------

#define SAT_SOF 0xA5u // protocol v1
#define SAT_HEADER_LEN 4u
#define SAT_SLOT_LEN 4u
#define SAT_MAX_SLOTS 64u
#define SAT_FRAME_LEN(n) (SAT_HEADER_LEN + SAT_SLOT_LEN * (n))
#define SAT_MAX_FRAME_LEN SAT_FRAME_LEN(SAT_MAX_SLOTS) // 260 bytes

/** Header byte offsets. */
enum
{
    SAT_H_SOF  = 0,
    SAT_H_TYPE = 1,
    SAT_H_N    = 2,
    SAT_H_HOP  = 3,
};

/** Frame types (header byte 1). */
enum
{
    SAT_SCAN     = 0x01,
    SAT_DISCOVER = 0x02,
    SAT_COMMAND  = 0x03,
};

/** One slot: a kind, two data bytes, and a CRC over the first three. */
typedef struct
{
    uint8_t kind;
    uint8_t d0;
    uint8_t d1;
    uint8_t crc;
} sat_slot;

// --- Slot kinds (§3, §4, §5) ---------------------------------------------------

/** Input slots, satellite to Seed. They report state, not events. */
enum
{
    SAT_IN_EMPTY   = 0x00,
    SAT_IN_POT     = 0x01, // position 0..65535, little-endian
    SAT_IN_BUTTON  = 0x02, // D0 bit 0 pressed; D1 press counter (wraps at 256)
    SAT_IN_ENCODER = 0x03, // D0 detent counter; D1 bit 0 pressed, bits 4-7 press counter
    SAT_IN_SWITCH  = 0x04, // D0 position index
    SAT_IN_FAULT   = 0x7F, // D0 fault code, D1 detail
};

/** Output slots, Seed to satellite. */
enum
{
    SAT_OUT_NONE = 0x00, // keep the current output state
    SAT_OUT_LED  = 0x10, // D0 brightness 0-255; D1 a SAT_LED_ mode
};

enum
{
    SAT_LED_STEADY    = 0,
    SAT_LED_SLOW_BLINK = 1, // 1 Hz
    SAT_LED_FAST_BLINK = 2, // 4 Hz
};

/** The one slot kind DISCOVER carries. */
#define SAT_DESCRIPTOR 0x80u

/** Command slots, in a COMMAND frame. Position 0 addresses every satellite. */
enum
{
    SAT_CMD_CLOSE_RING_HERE = 0x40, // D0 target position
    SAT_CMD_RING_AUTO       = 0x41, // D0 target position, or 0
    SAT_CMD_RESET           = 0x42, // D0 target or 0; D1 must be SAT_RESET_GUARD
    SAT_CMD_IDENTIFY        = 0x43, // D0 target position; D1 seconds to blink
};
#define SAT_RESET_GUARD 0x5Au

/** Fault codes, D0 of a FAULT slot (§7). */
enum
{
    SAT_FAULT_ID_BETWEEN_BANDS = 0x01, // D1: the reading as kind x 10
    SAT_FAULT_KIND_UNSUPPORTED = 0x02, // D1: the kind
    SAT_FAULT_SENSOR           = 0x03,
};

// --- Board kinds and the ID resistor (§1.6) -------------------------------------

enum
{
    SAT_KIND_TEST_FIXTURE = 0,
    SAT_KIND_POT          = 1,
    SAT_KIND_BUTTON       = 2,
    SAT_KIND_ENCODER      = 3,
    SAT_KIND_SWITCH       = 4,
    SAT_KIND_POT_BANK_4   = 5,
    SAT_KIND_ENC_BANK_4   = 6,
    SAT_KIND_NO_ID        = 15, // no ID resistor fitted: sends EMPTY
};
#define SAT_NUM_KINDS 16u

/** How far a reading may sit from a kind number and still count as that kind.
 *  1 % resistors land within 0.1 (§1.6); a reading further out than this is a
 *  fault, not a guess. */
#define SAT_KIND_TOLERANCE_X100 30 // 0.30 of a kind step

/** Slots a board kind owns: 4 for the banks, 1 for everything else. */
static inline uint8_t sat_kind_slots(uint8_t kind)
{
    return (kind == SAT_KIND_POT_BANK_4 || kind == SAT_KIND_ENC_BANK_4) ? 4 : 1;
}

/** The input slot kind a board kind reports. */
static inline uint8_t sat_kind_input(uint8_t kind)
{
    switch(kind)
    {
        case SAT_KIND_POT:
        case SAT_KIND_POT_BANK_4: return SAT_IN_POT;
        case SAT_KIND_BUTTON: return SAT_IN_BUTTON;
        case SAT_KIND_ENCODER:
        case SAT_KIND_ENC_BANK_4: return SAT_IN_ENCODER;
        case SAT_KIND_SWITCH: return SAT_IN_SWITCH;
        default: return SAT_IN_EMPTY;
    }
}

/** Whether this firmware knows the board kind. */
static inline bool sat_kind_supported(uint8_t kind)
{
    return kind <= SAT_KIND_ENC_BANK_4 || kind == SAT_KIND_NO_ID;
}

/** Board kind from the ID divider's ADC reading: kind = round(15 x reading /
 *  full scale). Returns the kind, or -1 if the reading sits between bands; then
 *  *tenths (if given) holds the reading as kind x 10, the FAULT slot's detail. */
static inline int sat_kind_from_adc(uint16_t reading, uint16_t full_scale, uint8_t* tenths)
{
    uint32_t x100 = (1500u * reading + full_scale / 2) / full_scale; // kind x 100
    uint32_t kind = (x100 + 50) / 100;
    int32_t  off  = (int32_t)x100 - (int32_t)(kind * 100);
    if(tenths)
        *tenths = (uint8_t)((x100 + 5) / 10);
    if(kind > 15 || off > SAT_KIND_TOLERANCE_X100 || off < -SAT_KIND_TOLERANCE_X100)
        return -1;
    return (int)kind;
}

// --- CRC and slots (§2.1) ---------------------------------------------------------

/** CRC-8/SMBUS: polynomial 0x07, init 0, no reflection, no final XOR. */
static inline uint8_t sat_crc8(const uint8_t* p, size_t n)
{
    uint8_t crc = 0;
    while(n--)
    {
        crc ^= *p++;
        for(int i = 0; i < 8; i++)
            crc = (uint8_t)((crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1);
    }
    return crc;
}

static inline sat_slot sat_slot_make(uint8_t kind, uint8_t d0, uint8_t d1)
{
    sat_slot s;
    s.kind = kind;
    s.d0   = d0;
    s.d1   = d1;
    s.crc  = sat_crc8(&s.kind, 3);
    return s;
}

static inline bool sat_slot_ok(sat_slot s)
{
    return sat_crc8(&s.kind, 3) == s.crc;
}

static inline sat_slot sat_slot_read(const uint8_t* p)
{
    sat_slot s;
    s.kind = p[0];
    s.d0   = p[1];
    s.d1   = p[2];
    s.crc  = p[3];
    return s;
}

static inline void sat_slot_write(uint8_t* p, sat_slot s)
{
    p[0] = s.kind;
    p[1] = s.d0;
    p[2] = s.d1;
    p[3] = s.crc;
}

// --- Input slots ------------------------------------------------------------------

static inline sat_slot sat_pot(uint16_t position)
{
    return sat_slot_make(SAT_IN_POT, (uint8_t)position, (uint8_t)(position >> 8));
}

static inline uint16_t sat_pot_position(sat_slot s)
{
    return (uint16_t)(s.d0 | (s.d1 << 8));
}

static inline sat_slot sat_button(bool pressed, uint8_t presses)
{
    return sat_slot_make(SAT_IN_BUTTON, pressed ? 1 : 0, presses);
}

static inline sat_slot sat_encoder(uint8_t detents, bool pressed, uint8_t presses)
{
    return sat_slot_make(SAT_IN_ENCODER, detents,
                         (uint8_t)((pressed ? 1 : 0) | ((presses & 0x0F) << 4)));
}

static inline sat_slot sat_switch(uint8_t position)
{
    return sat_slot_make(SAT_IN_SWITCH, position, 0);
}

static inline sat_slot sat_fault(uint8_t code, uint8_t detail)
{
    return sat_slot_make(SAT_IN_FAULT, code, detail);
}

/** How far an 8-bit counter moved, from the last good value to this one:
 *  -128..127, so a counter that wrapped still gives the right direction. */
static inline int sat_delta8(uint8_t now, uint8_t before)
{
    return (int)(int8_t)(uint8_t)(now - before);
}

/** The same for the encoder's 4-bit press counter: 0..15 presses since. */
static inline int sat_delta4(uint8_t now, uint8_t before)
{
    return (now - before) & 0x0F;
}

// --- Output slots -----------------------------------------------------------------

static inline sat_slot sat_led(uint8_t brightness, uint8_t mode)
{
    return sat_slot_make(SAT_OUT_LED, brightness, mode);
}

static inline sat_slot sat_none(void)
{
    return sat_slot_make(SAT_OUT_NONE, 0, 0);
}

// --- Discovery (§4) -----------------------------------------------------------------

typedef struct
{
    uint8_t kind;      // board kind, 0..15
    uint8_t slots;     // slots owned, 1..4
    bool    last;      // PRESENT high: nothing downstream
    uint8_t fw_major;  // 0..15
    uint8_t fw_minor;  // 0..15
} sat_descriptor;

static inline sat_slot sat_descriptor_slot(sat_descriptor d)
{
    return sat_slot_make(SAT_DESCRIPTOR,
                         (uint8_t)((d.kind & 0x0F) | (((d.slots - 1) & 0x03) << 4)
                                   | (d.last ? 0x40 : 0)),
                         (uint8_t)(((d.fw_major & 0x0F) << 4) | (d.fw_minor & 0x0F)));
}

static inline sat_descriptor sat_descriptor_read(sat_slot s)
{
    sat_descriptor d;
    d.kind     = s.d0 & 0x0F;
    d.slots    = (uint8_t)(((s.d0 >> 4) & 0x03) + 1);
    d.last     = (s.d0 & 0x40) != 0;
    d.fw_major = s.d1 >> 4;
    d.fw_minor = s.d1 & 0x0F;
    return d;
}

// --- Commands (§5) --------------------------------------------------------------------

static inline sat_slot sat_command(uint8_t command, uint8_t target, uint8_t arg)
{
    return sat_slot_make(command, target, arg);
}

/** Whether the satellite at `position` should act on this command slot. */
static inline bool sat_command_for(sat_slot s, uint8_t position)
{
    if(!sat_slot_ok(s) || s.kind < SAT_CMD_CLOSE_RING_HERE || s.kind > SAT_CMD_IDENTIFY)
        return false;
    if(s.kind == SAT_CMD_RESET && s.d1 != SAT_RESET_GUARD)
        return false;
    bool all = s.d0 == 0
               && (s.kind == SAT_CMD_RING_AUTO || s.kind == SAT_CMD_RESET);
    return all || s.d0 == position;
}

// --- Headers (§2.1, §2.2) ---------------------------------------------------------------

static inline void sat_header_write(uint8_t* p, uint8_t type, uint8_t n, uint8_t hop)
{
    p[SAT_H_SOF]  = SAT_SOF;
    p[SAT_H_TYPE] = type;
    p[SAT_H_N]    = n;
    p[SAT_H_HOP]  = hop;
}

/** A header a receiver accepts: v1 SOF, a known type, at most 64 slots. */
static inline bool sat_header_ok(const uint8_t* p)
{
    return p[SAT_H_SOF] == SAT_SOF
           && (p[SAT_H_TYPE] == SAT_SCAN || p[SAT_H_TYPE] == SAT_DISCOVER
               || p[SAT_H_TYPE] == SAT_COMMAND)
           && p[SAT_H_N] <= SAT_MAX_SLOTS;
}

// --- The satellite's side (§2.3) ------------------------------------------------------

/** The header a satellite sends on, given the one it received and the slots it
 *  owns. SCAN keeps N (it consumes s slots and appends s), DISCOVER adds the
 *  descriptor, COMMAND forwards everything; every type adds 1 to HOP. A SCAN
 *  with fewer than s slots is forwarded unchanged apart from HOP. */
static inline void sat_header_onward(const uint8_t* in, uint8_t slots_owned, uint8_t* out)
{
    out[SAT_H_SOF]  = in[SAT_H_SOF];
    out[SAT_H_TYPE] = in[SAT_H_TYPE];
    out[SAT_H_N]    = (uint8_t)(in[SAT_H_N] + (in[SAT_H_TYPE] == SAT_DISCOVER ? 1 : 0));
    out[SAT_H_HOP]  = (uint8_t)(in[SAT_H_HOP] + 1);
    (void)slots_owned;
}

/** What one satellite does with one whole frame: the reference model of §2.3.
 *
 *  The satellite firmware streams a frame through with a 4-byte lag rather than
 *  buffering it, but what it sends must equal this, byte for byte. Host tests
 *  chain calls to this to simulate a whole ring.
 *
 *  in/in_len    the frame received; out gets the frame to send on, of the
 *               returned length (0: the frame is dropped)
 *  me           this satellite's descriptor (its kind and slots owned)
 *  inputs       its current input slots, `me.slots` of them
 *  taken        if not NULL, receives the output slots consumed from a SCAN
 *               (`me.slots` of them); *took says whether there were any
 *  position     if not NULL, receives this satellite's position, HOP_in + 1 */
static inline size_t sat_satellite_pass(const uint8_t* in, size_t in_len, sat_descriptor me,
                                        const sat_slot* inputs, sat_slot* taken, bool* took,
                                        uint8_t* position, uint8_t* out)
{
    if(took)
        *took = false;
    if(in_len < SAT_HEADER_LEN || !sat_header_ok(in))
        return 0;
    uint8_t n = in[SAT_H_N];
    if(in_len != SAT_FRAME_LEN(n))
        return 0;
    if(position)
        *position = (uint8_t)(in[SAT_H_HOP] + 1);

    sat_header_onward(in, me.slots, out);
    const uint8_t* slots = in + SAT_HEADER_LEN;
    uint8_t*       o     = out + SAT_HEADER_LEN;

    if(in[SAT_H_TYPE] == SAT_SCAN && n >= me.slots)
    {
        for(uint8_t i = 0; i < me.slots; i++)
            if(taken)
                taken[i] = sat_slot_read(slots + SAT_SLOT_LEN * i);
        if(took)
            *took = true;
        size_t rest = SAT_SLOT_LEN * (size_t)(n - me.slots);
        for(size_t i = 0; i < rest; i++)
            o[i] = slots[SAT_SLOT_LEN * me.slots + i];
        for(uint8_t i = 0; i < me.slots; i++)
            sat_slot_write(o + rest + SAT_SLOT_LEN * i, inputs[i]);
        return SAT_FRAME_LEN(n);
    }
    for(size_t i = 0; i < SAT_SLOT_LEN * (size_t)n; i++)
        o[i] = slots[i];
    if(in[SAT_H_TYPE] == SAT_DISCOVER)
    {
        sat_slot_write(o + SAT_SLOT_LEN * n, sat_descriptor_slot(me));
        return SAT_FRAME_LEN(n + 1);
    }
    return SAT_FRAME_LEN(n);
}

// --- The Seed's side (§4, §5, §6) ------------------------------------------------------

/** Build a DISCOVER frame; returns its length. */
static inline size_t sat_build_discover(uint8_t* out)
{
    sat_header_write(out, SAT_DISCOVER, 0, 0);
    return SAT_HEADER_LEN;
}

/** Build a SCAN frame from n output slots in chain order; returns its length. */
static inline size_t sat_build_scan(uint8_t* out, const sat_slot* outputs, uint8_t n)
{
    sat_header_write(out, SAT_SCAN, n, 0);
    for(uint8_t i = 0; i < n; i++)
        sat_slot_write(out + SAT_HEADER_LEN + SAT_SLOT_LEN * i, outputs[i]);
    return SAT_FRAME_LEN(n);
}

/** Build a COMMAND frame from n command slots; returns its length. */
static inline size_t sat_build_command(uint8_t* out, const sat_slot* commands, uint8_t n)
{
    sat_header_write(out, SAT_COMMAND, n, 0);
    for(uint8_t i = 0; i < n; i++)
        sat_slot_write(out + SAT_HEADER_LEN + SAT_SLOT_LEN * i, commands[i]);
    return SAT_FRAME_LEN(n);
}

/** Why a reply was refused. */
typedef enum
{
    SAT_OK = 0,
    SAT_ERR_SHORT,         // fewer bytes than the header says, or no header
    SAT_ERR_HEADER,        // bad SOF, type or N, or not the type sent
    SAT_ERR_HOP,           // HOP isn't what the chain says it should be
    SAT_ERR_CRC,           // a slot failed its CRC
    SAT_ERR_NOT_DESCRIPTOR, // a DISCOVER slot that isn't a descriptor
    SAT_ERR_LAST,          // the "last" bit isn't on the last satellite, and only there
    SAT_ERR_VERSION,       // a satellite's major version isn't the one supported
    SAT_ERR_TOO_MANY,      // more satellites than the caller has room for
} sat_result;

/** Read a DISCOVER reply into descriptors, in chain order (§4).
 *
 *  Checks that HOP equals the number of descriptors, every CRC is good, exactly
 *  the last satellite has the "last" bit, and every satellite's major firmware
 *  version is `fw_major`. *count gets the number of satellites. An empty chain
 *  (nothing plugged in, the ring closed at the motherboard) returns SAT_OK with
 *  *count 0. */
static inline sat_result sat_read_discover(const uint8_t* in, size_t len, uint8_t fw_major,
                                           sat_descriptor* out, uint8_t max, uint8_t* count)
{
    *count = 0;
    if(len < SAT_HEADER_LEN)
        return SAT_ERR_SHORT;
    if(!sat_header_ok(in) || in[SAT_H_TYPE] != SAT_DISCOVER)
        return SAT_ERR_HEADER;
    uint8_t n = in[SAT_H_N];
    if(len < SAT_FRAME_LEN(n))
        return SAT_ERR_SHORT;
    if(in[SAT_H_HOP] != n)
        return SAT_ERR_HOP;
    if(n > max)
        return SAT_ERR_TOO_MANY;
    for(uint8_t i = 0; i < n; i++)
    {
        sat_slot s = sat_slot_read(in + SAT_HEADER_LEN + SAT_SLOT_LEN * i);
        if(!sat_slot_ok(s))
            return SAT_ERR_CRC;
        if(s.kind != SAT_DESCRIPTOR)
            return SAT_ERR_NOT_DESCRIPTOR;
        out[i] = sat_descriptor_read(s);
        if(out[i].last != (i == n - 1))
            return SAT_ERR_LAST;
        if(out[i].fw_major != fw_major)
            return SAT_ERR_VERSION;
    }
    *count = n;
    return SAT_OK;
}

/** Check a SCAN reply's header against the chain the Seed expects: the same N
 *  it sent, and HOP equal to the number of satellites. Slot CRCs are checked one
 *  by one by the caller, so one bad slot doesn't cost the whole frame. */
static inline sat_result sat_check_scan(const uint8_t* in, size_t len, uint8_t n,
                                        uint8_t satellites)
{
    if(len < SAT_HEADER_LEN)
        return SAT_ERR_SHORT;
    if(!sat_header_ok(in) || in[SAT_H_TYPE] != SAT_SCAN || in[SAT_H_N] != n)
        return SAT_ERR_HEADER;
    if(len < SAT_FRAME_LEN(n))
        return SAT_ERR_SHORT;
    if(in[SAT_H_HOP] != satellites)
        return SAT_ERR_HOP;
    return SAT_OK;
}

/** Expected ring time in microseconds (§6.2): every byte at 10 us, plus 40 us
 *  per satellite. The timeout is twice this, and at least 1 ms. */
static inline uint32_t sat_ring_time_us(uint8_t n, uint8_t satellites)
{
    return SAT_FRAME_LEN((uint32_t)n) * 10u + satellites * 40u;
}

static inline uint32_t sat_timeout_us(uint8_t n, uint8_t satellites)
{
    uint32_t t = 2u * sat_ring_time_us(n, satellites);
    return t < 1000u ? 1000u : t;
}
