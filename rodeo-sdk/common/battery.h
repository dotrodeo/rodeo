#pragma once
//
// The battery, through the motherboard's MAX17048 fuel gauge on I2C1.
//
//   rodeo::Battery battery;
//   if(!battery.Init())
//       ...;                      // no gauge answered at 0x36
//   rodeo::max17048::Reading r;
//   if(battery.Read(&r))
//       ...;                      // r.percent, r.volts, r.trend
//
// The register meanings and the decoding are in max17048.h, host-tested. Each
// Read() is three short blocking transfers at 100 kHz, about 1.5 ms: call it
// from the main loop a few times a minute, never from the audio callback.
//
// I2C1 is shared with the OLED and the four taps. libDaisy's I2CHandle is one
// handle per peripheral underneath, so this and the OLED driver can both
// Init() it with the same pins and speed.
//
#include "daisy_seed.h"
#include "max17048.h"

namespace rodeo
{
class Battery
{
  public:
    /** Set up I2C1 and check the gauge answers with a MAX17048's version. */
    bool Init();

    /** The latest voltage, state of charge and rate. False if the bus failed. */
    bool Read(max17048::Reading* r);

    /** The VERSION register as read by Init(). */
    uint16_t Version() const { return version_; }

    /** The STATUS register. Clears RI (the power-up flag) once read, so the
     *  first call after a cell is connected is the one that sees it. */
    bool Status(uint16_t* status);

  private:
    bool ReadWord(uint8_t reg, uint16_t* value);
    bool WriteWord(uint8_t reg, uint16_t value);

    daisy::I2CHandle i2c_;
    uint16_t         version_ = 0;
};
} // namespace rodeo
