#pragma once
//
// The ten I/O connectors on the Seed: digital in and out, analog in through the
// Seed's ADC, and CV out from its DAC on CV1/CV2.
//
//   rodeo::Io io;
//   io.Init(rodeo::IoConfig()
//               .Set(board::kIoA1, rodeo::IoMode::kAnalogIn)
//               .Set(board::kIoD1, rodeo::IoMode::kDigitalInPullUp)
//               .Set(board::kIoCv1, rodeo::IoMode::kCvOut));
//
//   float knob = io.Read(board::kIoA1); // 0..1, latest scan, never blocks
//   bool  gate = io.Get(board::kIoD1);
//   io.Write(board::kIoCv1, 0.5f);      // half of 3.3 V
//
// The configuration is checked first (io_plan.h); Init() returns false and
// Plan() says why if it asks for something a connector can't do. The ADC scans
// every analog input in the background on DMA. A Rev B connector has 1 kOhm in
// series with the pin and 3V3_A on its third pin, so a 10 k pot plugs straight
// into any analog one.
//
// Not here yet: interrupt-driven gate inputs. When they come, A3/A6 and A7/CV1
// share an interrupt line (board::SharesExti), and only one of each pair can
// use it.
//
#include "daisy_seed.h"
#include "io_plan.h"

namespace rodeo
{
class Io
{
  public:
    /** Set up every connector the configuration uses. False if it can't be done. */
    bool Init(const IoConfig& cfg);

    const IoPlan& Plan() const { return plan_; }

    /** A digital input's level, or a digital output's last setting. */
    bool Get(board::Io c);

    /** A digital output. Ignored on any other connector. */
    void Set(board::Io c, bool high);

    /** An analog input, 0..1. 0 on any other connector. */
    float Read(board::Io c) const;

    /** CV out, 0..1 of 3.3 V. Ignored unless the connector is CV out. */
    void Write(board::Io c, float v);

  private:
    IoConfig                cfg_;
    IoPlan                  plan_ = {};
    daisy::GPIO             gpio_[board::kNumIo];
    bool                    out_[board::kNumIo] = {};
    daisy::AdcChannelConfig adc_cfg_[board::kNumIo];
    daisy::AdcHandle        adc_;
    daisy::DacHandle        dac_;
};
} // namespace rodeo
