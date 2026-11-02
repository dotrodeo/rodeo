#pragma once
//
// Passing a frame along the chain, a byte at a time.
//
// A satellite never holds a whole frame: it forwards each byte a fixed number of
// byte times after it arrives (sat_lag_bytes(): the header, the slots it
// consumes, and the byte in flight), so the frame moves along the ring with a
// short delay at each satellite and the output never stops mid-frame. What it
// sends must equal sat_satellite_pass() in sat_frame.h, the reference model,
// byte for byte; tests/stream_gtest.cpp checks exactly that.
//
// Hardware-free: the UART driver calls stream_rx() for each byte received and
// transmits what it returns, and calls stream_reset() when the line has been
// idle for two byte times (contract §2.2).
//
#include "sat_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    // Set once at boot
    sat_descriptor me;

    // Per frame
    bool     active;     // a frame is coming in
    bool     dropped;    // its header was bad: ignore the rest
    uint16_t rx;         // bytes received
    uint16_t tx;         // bytes sent on
    uint16_t in_len;     // known once the header is in
    uint16_t out_len;
    uint8_t  n;          // slots in the frame received
    uint8_t  lag;        // byte times between receiving and sending
    bool     consume;    // a SCAN with at least `me.slots` slots
    uint8_t  hdr_out[SAT_HEADER_LEN];
    uint8_t  ring[32];   // the last 32 bytes received; the lag is at most 17
    sat_slot appended[4]; // what this satellite adds: its inputs or descriptor
    uint8_t  n_appended;

    // Results, valid once stream_done() is true
    sat_slot taken[4];   // output slots consumed from a SCAN
    bool     took;
    sat_slot cmds[4];    // commands in a COMMAND frame addressed to this satellite
    uint8_t  n_cmds;
    uint8_t  type;
    uint8_t  position;   // HOP in + 1
} sat_stream;

/** Start a satellite's stream state: its descriptor (kind, slots, firmware). */
void stream_init(sat_stream* s, sat_descriptor me);

/** Forget any frame in progress (an idle gap, or a timeout). */
void stream_reset(sat_stream* s);

/** One byte received. Writes the byte to transmit now, if any, into out and
 *  returns how many: 0 or 1. Once the frame's last byte has arrived, the rest of
 *  what is owed comes from stream_next(), a byte at a time, so the UART can start
 *  on the first while the others are still being worked out. Working them all out
 *  at once took longer than the idle gap on a CH32V003, and the next satellite
 *  dropped the frame.
 *
 *  `inputs` are this satellite's current input slots (me.slots of them),
 *  captured when the header completes, so a slot changes between frames, never
 *  half-way through one. */
size_t stream_rx(sat_stream* s, uint8_t byte, const sat_slot* inputs, uint8_t* out);

/** The next byte owed once the whole frame has arrived. False while bytes are
 *  still coming in (stream_rx() paces those), or when nothing more is owed. */
bool stream_next(sat_stream* s, uint8_t* b);

/** True once the whole of the current frame has arrived and been sent on. */
bool stream_done(const sat_stream* s);

#ifdef __cplusplus
}
#endif
