#pragma once
//
// The I/O connectors' configuration, checked before any pin is touched.
//
// An app says what each of the ten connectors is for; PlanIo() checks it against
// what the hardware can do (analog only where there is an ADC channel, CV out
// only on the two DAC pins) and works out the order the ADC scans in. A wrong
// configuration fails here, by silkscreen name, instead of as a pin that reads
// nothing. Pure logic: host-tested in common/tests/io_plan_gtest.cpp, used by
// io.h on the Seed.
//
#include <stdint.h>

#include "board.h"

namespace rodeo
{
enum class IoMode : uint8_t
{
    kUnused,
    kDigitalIn,
    kDigitalInPullUp,
    kDigitalInPullDown,
    kDigitalOut,
    kAnalogIn, // through the Seed's ADC: A1-A7, CV1, CV2
    kCvOut,    // the Seed's DAC, 0-3.3 V: CV1, CV2
};

/** What each connector is for, indexed by board::Io. */
struct IoConfig
{
    IoMode mode[board::kNumIo] = {};

    IoConfig& Set(board::Io c, IoMode m)
    {
        mode[c] = m;
        return *this;
    }
};

/** A checked configuration, ready for the hardware. */
struct IoPlan
{
    bool        ok;
    int8_t      bad;  // the connector that failed (board::Io), or -1
    const char* why;  // what's wrong with it

    uint8_t n_adc;                // analog inputs, in scan order
    uint8_t adc_conn[board::kNumIo]; // connector for each scan position
    int8_t  adc_index[board::kNumIo]; // scan position for each connector, or -1
    bool    dac1, dac2;            // DAC channels in use
};

inline IoPlan PlanIo(const IoConfig& cfg)
{
    IoPlan p = {};
    p.ok     = true;
    p.bad    = -1;
    for(int i = 0; i < board::kNumIo; i++)
    {
        const board::IoConnector& c = board::kIo[i];
        p.adc_index[i]              = -1;
        switch(cfg.mode[i])
        {
            case IoMode::kAnalogIn:
                if(c.adc < 0)
                {
                    p.ok  = false;
                    p.bad = static_cast<int8_t>(i);
                    p.why = "analog in needs an ADC channel: D1 is digital only";
                    return p;
                }
                p.adc_index[i]       = static_cast<int8_t>(p.n_adc);
                p.adc_conn[p.n_adc++] = static_cast<uint8_t>(i);
                break;
            case IoMode::kCvOut:
                if(c.dac < 0)
                {
                    p.ok  = false;
                    p.bad = static_cast<int8_t>(i);
                    p.why = "CV out needs a DAC: only CV1 and CV2 have one";
                    return p;
                }
                (c.dac == 1 ? p.dac1 : p.dac2) = true;
                break;
            default: break;
        }
    }
    return p;
}

/** 0..1 to the DAC's 12-bit code, clamped and rounded. */
inline uint16_t DacCode(float v)
{
    if(v <= 0.0f)
        return 0;
    if(v >= 1.0f)
        return 4095;
    return static_cast<uint16_t>(v * 4095.0f + 0.5f);
}
} // namespace rodeo
