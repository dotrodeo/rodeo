// The I/O connectors on a bare Daisy Seed, checking themselves.
//
// Two jumpers turn the Seed into its own test rig (Seed header pin numbers):
//
//   pin 30 (CV1) -> pin 29 (CV2)   CV1 is the DAC out, CV2 reads it on the ADC
//   pin 23 (A2)  -> pin 8  (D1)    A2 is a digital out, D1 reads it back
//
// At start-up it sweeps CV1 and checks CV2 follows, then toggles A2 and checks
// D1 follows, and prints PASS or FAIL for each. After that it keeps printing a
// pot's position on A1 (pin 22: wiper there, ends to 3V3 and ground), D1, and
// CV2, every quarter second. Without the jumpers the tests fail, which is the
// check that they test something.
//
// Status on the Seed's USB serial; 'B' reboots into DFU for the next flash.
#include <math.h>

#include "daisy_seed.h"
#include "io.h"

using namespace daisy;

DaisySeed            hw;
rodeo::Io            io;
static volatile bool to_bootloader = false;

static void UsbRx(uint8_t* buf, uint32_t* len)
{
    for(uint32_t i = 0; i < *len; i++)
        if(buf[i] == 'B')
            to_bootloader = true;
}

// libDaisy's PrintLine takes floats only with the FLT macros; print milli-units.
static int Milli(float v) { return static_cast<int>(lroundf(v * 1000.0f)); }

static bool DacSweep()
{
    bool ok = true;
    for(float v : {0.1f, 0.3f, 0.5f, 0.7f, 0.9f})
    {
        io.Write(board::kIoCv1, v);
        System::Delay(20); // let the ADC's background scan catch up
        float got  = io.Read(board::kIoCv2);
        bool  pass = fabsf(got - v) < 0.03f;
        ok         = ok && pass;
        hw.PrintLine("  CV1 out %4d/1000 -> CV2 in %4d/1000  %s", Milli(v), Milli(got),
                     pass ? "ok" : "OFF");
    }
    io.Write(board::kIoCv1, 0.0f);
    return ok;
}

static bool DigitalLoop()
{
    bool ok = true;
    for(bool level : {true, false, true, false})
    {
        io.Set(board::kIoA2, level);
        System::Delay(1);
        bool got = io.Get(board::kIoD1);
        ok       = ok && got == level;
        hw.PrintLine("  A2 out %d -> D1 in %d  %s", level, got, got == level ? "ok" : "OFF");
    }
    return ok;
}

int main(void)
{
    hw.Init();
    hw.StartLog(true); // wait for a terminal, so the self-test is seen
    hw.usb_handle.SetReceiveCallback(UsbRx, UsbHandle::FS_INTERNAL);

    bool ready = io.Init(rodeo::IoConfig()
                             .Set(board::kIoA1, rodeo::IoMode::kAnalogIn)
                             .Set(board::kIoA2, rodeo::IoMode::kDigitalOut)
                             .Set(board::kIoD1, rodeo::IoMode::kDigitalInPullDown)
                             .Set(board::kIoCv1, rodeo::IoMode::kCvOut)
                             .Set(board::kIoCv2, rodeo::IoMode::kAnalogIn));
    if(!ready)
    {
        hw.PrintLine("I/O configuration refused: %s", io.Plan().why);
        for(;;) {}
    }

    System::Delay(50);
    hw.PrintLine("Rodeo I/O connectors self-test");
    hw.PrintLine("DAC to ADC (jumper pin 30 to pin 29):");
    bool dac = DacSweep();
    hw.PrintLine("Digital out to in (jumper pin 23 to pin 8):");
    bool dig = DigitalLoop();
    hw.PrintLine("DAC/ADC %s, digital %s", dac ? "PASS" : "FAIL", dig ? "PASS" : "FAIL");

    uint32_t last = 0;
    for(;;)
    {
        if(to_bootloader)
            System::ResetToBootloader(System::BootloaderMode::STM);
        hw.SetLed(dac && dig);
        uint32_t now = System::GetNow();
        if(now - last < 250)
            continue;
        last = now;
        hw.PrintLine("A1 %4d/1000  D1 %d  CV2 %4d/1000", Milli(io.Read(board::kIoA1)),
                     io.Get(board::kIoD1), Milli(io.Read(board::kIoCv2)));
    }
}
