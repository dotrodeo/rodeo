// USB step 1: TinyUSB on a bare Daisy Seed, composite USB-MIDI + CDC serial on the
// Seed's own micro-USB. Replaces libDaisy's USB (see rodeo-sdk/docs/REV_B_PLAN.md,
// "USB: explore first"). No Rodeo board needed.
//
// What it does, once the computer has enumerated it as "Rodeo USB test":
//   * MIDI: echoes every message it receives, and plays middle C (on, off) every
//     second on channel 1, so a MIDI monitor shows traffic both ways.
//   * Serial: prints a banner when a terminal opens the port, echoes what you type,
//     prints each MIDI message received, and answers 's' with counters.
//   * The Seed's LED blinks slowly until the computer configures the device, then
//     fast.
//
// It uses libDaisy's System, not DaisySeed: DaisySeed owns a UsbHandle, which would
// link libDaisy's own OTG_FS interrupt handler and USB stack.
#include "daisy.h"
#include "tusb.h"

using namespace daisy;

static System sys;
static GPIO   led;

static uint32_t midi_in_count  = 0;
static uint32_t midi_out_count = 0;

extern "C"
{
    // TinyUSB owns the OTG_FS interrupt (port 0).
    void OTG_FS_IRQHandler(void) { tusb_int_handler(0, true); }

    uint32_t tusb_time_millis_api(void) { return System::GetNow(); }
}

/** Pins, clock and VBUS for OTG_FS. libDaisy's System::Init already clocks USB from
 *  HSI48. The Seed doesn't route VBUS to PA9, so VBUS sensing is off and TinyUSB
 *  forces the B-session valid. */
static void UsbHardwareInit()
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef g = {};
    g.Pin              = GPIO_PIN_11 | GPIO_PIN_12; // DM, DP
    g.Mode             = GPIO_MODE_AF_PP;
    g.Pull             = GPIO_NOPULL;
    g.Speed            = GPIO_SPEED_FREQ_HIGH;
    g.Alternate        = GPIO_AF10_OTG2_HS; // the FS core's AF on the H750
    HAL_GPIO_Init(GPIOA, &g);

    HAL_PWREx_EnableUSBVoltageDetector();
    __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();

    tud_configure_dwc2_t cfg = CFG_TUD_CONFIGURE_DWC2_DEFAULT;
    cfg.vbus_sensing         = false;
    tud_configure(BOARD_TUD_RHPORT, TUD_CFGID_DWC2, &cfg);
}

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
                 ", MIDI + serial on OTG_FS.\r\n's' = counters.\r\n");
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

    UsbHardwareInit();
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
