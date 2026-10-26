// TinyUSB on the Seed: composite USB-MIDI + CDC serial, replacing libDaisy's USB
// (see planning/REV_B_PLAN.md, "USB: explore first").
//
//   make                 USB step 1: a bare Seed, on its own micro-USB
//   make RODEO_USB=hs    bring-up 08: Rev B's USB-C, through the power board
//                        (OTG_HS on Seed pins 36/37; see common/usb_port.h)
//
// What it does, once the computer has enumerated it as "Rodeo USB test":
//   * MIDI: echoes every message it receives, and plays middle C (on, off) every
//     second on channel 1, so a MIDI monitor shows traffic both ways.
//   * Serial: prints a banner (naming the port) when a terminal opens it, echoes
//     what you type, prints each MIDI message received, and answers 's' with
//     counters.
//   * The Seed's LED blinks slowly until the computer configures the device, then
//     fast.
//
// It uses libDaisy's System, not DaisySeed: DaisySeed owns a UsbHandle, which would
// link libDaisy's own USB stack.
#include "daisy.h"
#include "tusb.h"
#include "usb_port.h"

using namespace daisy;

static System sys;
static GPIO   led;

static uint32_t midi_in_count  = 0;
static uint32_t midi_out_count = 0;

static void CdcPrint(const char* s)
{
    if(!tud_cdc_connected())
        return;
    tud_cdc_write_str(s);
    tud_cdc_write_flush();
}

static void CdcTask()
{
    static bool was_connected = false;
    bool        connected     = tud_cdc_connected();
    if(connected && !was_connected)
        CdcPrint("\r\nRodeo USB test: TinyUSB " TUSB_VERSION_STRING
                 ", MIDI + serial on " RODEO_USB_PORT_NAME
                 ".\r\n's' = counters.\r\n");
    was_connected = connected;

    while(tud_cdc_available())
    {
        char c = (char)tud_cdc_read_char();
        if(c == 's')
        {
            char buf[96];
            snprintf(buf,
                     sizeof(buf),
                     "\r\nMIDI in %lu, out %lu, up %lu ms\r\n",
                     (unsigned long)midi_in_count,
                     (unsigned long)midi_out_count,
                     (unsigned long)System::GetNow());
            CdcPrint(buf);
        }
        else
        {
            tud_cdc_write_char(c);
            tud_cdc_write_flush();
        }
    }
}

static void MidiTask()
{
    // Echo everything that arrives, and report it on the serial port.
    uint8_t packet[4];
    while(tud_midi_available() && tud_midi_packet_read(packet))
    {
        midi_in_count++;
        tud_midi_packet_write(packet);
        char buf[48];
        snprintf(buf,
                 sizeof(buf),
                 "MIDI in: %02X %02X %02X\r\n",
                 packet[1],
                 packet[2],
                 packet[3]);
        CdcPrint(buf);
    }

    // Middle C on, then off, alternating every 500 ms.
    static uint32_t last = 0;
    static bool     on   = false;
    uint32_t        now  = System::GetNow();
    if(tud_mounted() && now - last >= 500)
    {
        last             = now;
        on               = !on;
        uint8_t const msg[3] = {(uint8_t)(on ? 0x90 : 0x80), 60, (uint8_t)(on ? 100 : 0)};
        tud_midi_stream_write(0, msg, 3);
        midi_out_count++;
    }
}

int main(void)
{
    sys.Init();
    led.Init(Pin(PORTC, 7), GPIO::Mode::OUTPUT); // the Seed's user LED

    rodeo::UsbPortInit();
    tusb_rhport_init_t dev_init = {TUSB_ROLE_DEVICE, TUSB_SPEED_AUTO};
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    uint32_t blink = 0;
    for(;;)
    {
        tud_task();
        MidiTask();
        CdcTask();

        uint32_t now    = System::GetNow();
        uint32_t period = tud_mounted() ? 125 : 500;
        if(now - blink >= period)
        {
            blink = now;
            led.Toggle();
        }
    }
}
