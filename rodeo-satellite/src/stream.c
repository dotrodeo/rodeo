#include "stream.h"

void stream_init(sat_stream* s, sat_descriptor me)
{
    s->me = me;
    stream_reset(s);
}

void stream_reset(sat_stream* s)
{
    s->active     = false;
    s->dropped    = false;
    s->rx         = 0;
    s->tx         = 0;
    s->in_len     = 0;
    s->out_len    = 0;
    s->took       = false;
    s->n_appended = 0;
    s->n_cmds     = 0;
}

bool stream_done(const sat_stream* s)
{
    return s->active && !s->dropped && s->in_len && s->rx == s->in_len && s->tx == s->out_len;
}

// Byte k of what this satellite sends, given what has arrived so far.
static uint8_t out_byte(const sat_stream* s, uint16_t k)
{
    if(k < SAT_HEADER_LEN)
        return s->hdr_out[k];
    uint16_t j       = (uint16_t)(k - SAT_HEADER_LEN);
    uint8_t  n       = s->n;
    uint16_t skip    = s->consume ? (uint16_t)(SAT_SLOT_LEN * s->me.slots) : 0;
    uint16_t forward = (uint16_t)(SAT_SLOT_LEN * n - skip);
    if(j < forward)
    {
        uint16_t i = (uint16_t)(SAT_HEADER_LEN + skip + j); // index into the input
        return s->ring[i & 31];
    }
    j = (uint16_t)(j - forward);
    sat_slot a = s->appended[j / SAT_SLOT_LEN];
    switch(j % SAT_SLOT_LEN)
    {
        case 0: return a.kind;
        case 1: return a.d0;
        case 2: return a.d1;
        default: return a.crc;
    }
}

// The header has arrived: work out what this frame becomes.
static void start_frame(sat_stream* s, const sat_slot* inputs)
{
    uint8_t hdr[SAT_HEADER_LEN] = {s->ring[0], s->ring[1], s->ring[2], s->ring[3]};
    if(!sat_header_ok(hdr))
    {
        s->dropped = true;
        return;
    }
    uint8_t n   = hdr[SAT_H_N];
    s->n        = n;
    s->type     = hdr[SAT_H_TYPE];
    s->position = (uint8_t)(hdr[SAT_H_HOP] + 1);
    s->in_len   = (uint16_t)SAT_FRAME_LEN(n);
    s->consume  = s->type == SAT_SCAN && n >= s->me.slots;
    s->lag      = sat_lag_bytes(s->type, n, s->me.slots);
    sat_header_onward(hdr, s->me.slots, s->hdr_out);

    s->n_appended = 0;
    if(s->consume)
        for(uint8_t i = 0; i < s->me.slots; i++)
            s->appended[s->n_appended++] = inputs[i];
    else if(s->type == SAT_DISCOVER)
        s->appended[s->n_appended++] = sat_descriptor_slot(s->me);
    s->out_len = (uint16_t)SAT_FRAME_LEN(s->hdr_out[SAT_H_N]);
}

size_t stream_rx(sat_stream* s, uint8_t byte, const sat_slot* inputs, uint8_t* out)
{
    if(!s->active)
    {
        if(byte != SAT_SOF)
            return 0; // wait for a frame to start
        stream_reset(s);
        s->active = true;
    }
    if(s->dropped || (s->in_len && s->rx >= s->in_len))
        return 0; // a bad header, or bytes past the end: nothing more to send

    s->ring[s->rx & 31] = byte;
    s->rx++;

    if(s->rx == SAT_HEADER_LEN)
    {
        start_frame(s, inputs);
        if(s->dropped)
            return 0;
    }
    if(!s->in_len)
        return 0;

    // A consumed output slot has fully arrived: keep it.
    if(s->consume && s->rx >= SAT_HEADER_LEN + SAT_SLOT_LEN
       && (s->rx - SAT_HEADER_LEN) % SAT_SLOT_LEN == 0)
    {
        uint16_t slot = (uint16_t)((s->rx - SAT_HEADER_LEN) / SAT_SLOT_LEN - 1);
        if(slot < s->me.slots)
        {
            uint16_t b      = (uint16_t)(SAT_HEADER_LEN + SAT_SLOT_LEN * slot);
            s->taken[slot]  = sat_slot_read((const uint8_t[]){s->ring[b & 31], s->ring[(b + 1) & 31],
                                                              s->ring[(b + 2) & 31], s->ring[(b + 3) & 31]});
            s->took         = slot + 1 == s->me.slots;
        }
    }

    // A command slot has fully arrived: keep it if it's for this satellite.
    if(s->type == SAT_COMMAND && s->rx > SAT_HEADER_LEN
       && (s->rx - SAT_HEADER_LEN) % SAT_SLOT_LEN == 0 && s->n_cmds < 4)
    {
        uint16_t b   = (uint16_t)(s->rx - SAT_SLOT_LEN);
        sat_slot cmd = sat_slot_read((const uint8_t[]){s->ring[b & 31], s->ring[(b + 1) & 31],
                                                      s->ring[(b + 2) & 31], s->ring[(b + 3) & 31]});
        if(sat_command_for(cmd, s->position))
            s->cmds[s->n_cmds++] = cmd;
    }

    size_t made = 0;
    if(s->rx == s->in_len)
    {
        while(s->tx < s->out_len) // the input is complete: send everything owed
            out[made++] = out_byte(s, s->tx++);
    }
    else if(s->rx >= s->lag && s->tx < s->out_len)
    {
        out[made++] = out_byte(s, s->tx++); // one byte out for each byte in
    }
    return made;
}
