#include "kind.h"

int sat_detect_kind(const sat_kind_io* io, uint8_t* detail)
{
    int kind = sat_kind_from_adc(io->read_id(io->ctx), io->full_scale, detail);
    if(kind != 0)
        return kind; // fed from 3V3: done, and the feed pin was never driven
    io->drive_feed(io->ctx, true);
    kind = sat_kind_from_adc(io->read_id(io->ctx), io->full_scale, detail);
    io->drive_feed(io->ctx, false);
    return kind;
}
