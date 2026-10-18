// USB audio test (USB plan, step 2 after the swap): a bare Daisy Seed as a USB audio
// interface on its micro-USB, two-way stereo, 48 kHz, 24-bit, alongside USB-MIDI and
// a serial port, all on TinyUSB. See rodeo-sdk/docs/REV_B_PLAN.md, "USB".
//
//   * Computer -> Seed: whatever the computer plays to "Rodeo" comes out of the Seed's
//     audio outputs. Asynchronous: the Seed's codec clock sets the rate and a feedback
//     endpoint keeps the computer in step.
//   * Seed -> computer: what "Rodeo" records, chosen on the serial port:
//       c  the Seed's audio inputs (default)
//       l  loopback: what the computer is playing to the Seed, returned digitally
//       t  a 1 kHz test tone at -12 dBFS (left) and 1.5 kHz (right)
//   * Serial: 's' prints stream state and counters.
//   * MIDI: echoes what it receives.
//
// DaisySeed brings libDaisy's own USB code with it, including an OTG_FS interrupt
// handler, so the interrupt is pointed at TinyUSB by copying the vector table to RAM
// and replacing that one entry (RouteUsbIrqToTinyUsb). Rev B's port (OTG_HS) needs the
// same, because libDaisy's system.cpp defines that handler for every app.
#include <math.h>
#include "daisy_seed.h"
#include "tusb.h"
#include "usb_descriptors.h"

using namespace daisy;

static DaisySeed hw;

//------------- Interrupt routing -------------//

extern "C" void TinyUsbFsIrq(void)
{
    tusb_int_handler(0, true);
}

// 256 entries covers the H750's 16 + 150 vectors; VTOR needs the table aligned to
// its size rounded up to a power of two. DTCM, so no cache between the CPU's writes
// and the vector fetch.
alignas(1024) static uint32_t ram_vectors[256] __attribute__((section(".dtcmram_bss")));

static void RouteUsbIrqToTinyUsb()
{
    uint32_t const* flash_vectors = (uint32_t const*)SCB->VTOR;
    for(int i = 0; i < 256; i++)
        ram_vectors[i] = flash_vectors[i];
    ram_vectors[16 + OTG_FS_IRQn] = (uint32_t)&TinyUsbFsIrq;
    __disable_irq();
    SCB->VTOR = (uint32_t)ram_vectors;
    __DSB();
    __ISB();
    __enable_irq();
}

static void UsbHardwareInit()
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef g = {};
    g.Pin              = GPIO_PIN_11 | GPIO_PIN_12;
    g.Mode             = GPIO_MODE_AF_PP;
    g.Pull             = GPIO_NOPULL;
    g.Speed            = GPIO_SPEED_FREQ_HIGH;
    g.Alternate        = GPIO_AF10_OTG2_HS;
    HAL_GPIO_Init(GPIOA, &g);

    HAL_PWREx_EnableUSBVoltageDetector();
    __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();

    tud_configure_dwc2_t cfg = CFG_TUD_CONFIGURE_DWC2_DEFAULT;
    cfg.vbus_sensing         = false;
    tud_configure(BOARD_TUD_RHPORT, TUD_CFGID_DWC2, &cfg);

    // Audio (SAI DMA, libDaisy's priority 0) must be able to preempt USB.
    NVIC_SetPriority(OTG_FS_IRQn, 6);
}

//------------- Audio state -------------//

static constexpr size_t kBlock       = 48; // frames per callback: 1 ms at 48 kHz
static constexpr size_t kFrameBytes  = AUDIO_N_CHANNELS * AUDIO_BYTES_PER_SAMPLE;
static constexpr size_t kBlockBytes  = kBlock * kFrameBytes;
// Start playing once 2 ms are queued: half the OUT buffer, the feedback's target level
static constexpr uint16_t kPrefill   = CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ / 2;

enum class MicSource
{
    Codec,
    Loopback,
    Tone
};

static volatile bool      spk_streaming = false;
static volatile bool      mic_streaming = false;
static volatile MicSource mic_source    = MicSource::Codec;
static bool               spk_playing   = false;

static volatile uint32_t spk_underruns = 0; // computer -> Seed: buffer ran dry
static volatile uint32_t mic_overruns  = 0; // Seed -> computer: buffer full, samples dropped
static volatile uint32_t spk_bytes     = 0;
static volatile uint32_t mic_bytes     = 0;
static volatile uint16_t spk_level_min = 0xFFFF, spk_level_max = 0;

// Diagnostics
static volatile uint32_t tx_packets = 0, tx_bytes = 0;
static volatile uint16_t tx_pkt_min = 0xFFFF, tx_pkt_max = 0;
static volatile uint16_t mic_fill_max = 0;
static volatile uint32_t callbacks = 0;

static volatile bool    spk_mute   = false;
static volatile int16_t spk_volume = 0; // 1/256 dB, master
static volatile float   spk_gain   = 1.0f;

static uint8_t usb_rx[kBlockBytes];
static uint8_t usb_tx[kBlockBytes];
static float   loop_l[kBlock], loop_r[kBlock];
static float   tone_phase_l = 0.f, tone_phase_r = 0.f;

static inline float Unpack24(const uint8_t* p)
{
    int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8;
    return (float)v * (1.0f / 8388608.0f);
}

static inline void Pack24(uint8_t* p, float x)
{
    if(x > 0.99999988f)
        x = 0.99999988f;
    if(x < -1.0f)
        x = -1.0f;
    int32_t v = (int32_t)(x * 8388608.0f);
    p[0]      = (uint8_t)v;
    p[1]      = (uint8_t)(v >> 8);
    p[2]      = (uint8_t)(v >> 16);
}

static void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                          AudioHandle::InterleavingOutputBuffer out,
                          size_t                                size)
{
    size_t const frames = size / 2;
    callbacks++;

    //--- computer -> Seed
    size_t got = 0;
    if(spk_streaming)
    {
        uint16_t level = tud_audio_available();
        if(level < spk_level_min)
            spk_level_min = level;
        if(level > spk_level_max)
            spk_level_max = level;
        if(!spk_playing && level >= kPrefill)
            spk_playing = true;
        if(spk_playing)
        {
            got = tud_audio_read(usb_rx, frames * kFrameBytes);
            spk_bytes += got;
            if(got < frames * kFrameBytes)
            {
                spk_underruns++;
                if(got == 0)
                    spk_playing = false; // queue up again before resuming
            }
        }
    }
    else
    {
        spk_playing = false;
    }

    float const gain = spk_mute ? 0.f : spk_gain;
    for(size_t i = 0; i < frames; i++)
    {
        float l = 0.f, r = 0.f;
        if((i + 1) * kFrameBytes <= got)
        {
            l = Unpack24(&usb_rx[i * kFrameBytes]);
            r = Unpack24(&usb_rx[i * kFrameBytes + 3]);
        }
        loop_l[i]      = l;
        loop_r[i]      = r;
        out[2 * i]     = l * gain;
        out[2 * i + 1] = r * gain;
    }

    //--- Seed -> computer
    if(mic_streaming)
    {
        for(size_t i = 0; i < frames; i++)
        {
            float l, r;
            switch(mic_source)
            {
                case MicSource::Loopback:
                    l = loop_l[i];
                    r = loop_r[i];
                    break;
                case MicSource::Tone:
                    l = 0.25f * sinf(tone_phase_l);
                    r = 0.25f * sinf(tone_phase_r);
                    tone_phase_l += 2.f * (float)M_PI * 1000.f / AUDIO_SAMPLE_RATE;
                    tone_phase_r += 2.f * (float)M_PI * 1500.f / AUDIO_SAMPLE_RATE;
                    if(tone_phase_l > 2.f * (float)M_PI)
                        tone_phase_l -= 2.f * (float)M_PI;
                    if(tone_phase_r > 2.f * (float)M_PI)
                        tone_phase_r -= 2.f * (float)M_PI;
                    break;
                default:
                    l = in[2 * i];
                    r = in[2 * i + 1];
                    break;
            }
            Pack24(&usb_tx[i * kFrameBytes], l);
            Pack24(&usb_tx[i * kFrameBytes + 3], r);
        }
        // TinyUSB's IN FIFO overwrites its oldest data when full, so check the room
        // first: drop whole new frames (and count it) rather than corrupt queued ones.
        tu_fifo_t* ff   = tud_audio_get_ep_in_ff();
        uint16_t   fill = tu_fifo_count(ff);
        if(fill > mic_fill_max)
            mic_fill_max = fill;
        uint16_t room = tu_fifo_remaining(ff);
        uint16_t want = (uint16_t)(frames * kFrameBytes);
        if(room < want)
        {
            mic_overruns++;
            want = (uint16_t)(room / kFrameBytes * kFrameBytes);
        }
        if(want)
            mic_bytes += tud_audio_write(usb_tx, want);
    }
}

//------------- TinyUSB audio callbacks -------------//

extern "C"
{
    bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const* req)
    {
        (void)rhport;
        uint8_t const itf = tu_u16_low(req->wIndex);
        uint8_t const alt = tu_u16_low(req->wValue);
        if(itf == ITF_NUM_AUDIO_SPK)
            spk_streaming = alt != 0;
        if(itf == ITF_NUM_AUDIO_MIC)
        {
            if(alt != 0)
                tud_audio_clear_ep_in_ff();
            mic_streaming = alt != 0;
        }
        return true;
    }

    bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const* req)
    {
        (void)rhport;
        uint8_t const itf = tu_u16_low(req->wIndex);
        if(itf == ITF_NUM_AUDIO_SPK)
            spk_streaming = false;
        if(itf == ITF_NUM_AUDIO_MIC)
            mic_streaming = false;
        return true;
    }

    bool tud_audio_tx_done_isr(uint8_t  rhport,
                               uint16_t n_bytes_sent,
                               uint8_t  func_id,
                               uint8_t  ep_in,
                               uint8_t  cur_alt_setting)
    {
        (void)rhport;
        (void)func_id;
        (void)ep_in;
        (void)cur_alt_setting;
        tx_packets++;
        tx_bytes += n_bytes_sent;
        if(n_bytes_sent < tx_pkt_min)
            tx_pkt_min = n_bytes_sent;
        if(n_bytes_sent > tx_pkt_max)
            tx_pkt_max = n_bytes_sent;
        return true;
    }

    void tud_audio_feedback_params_cb(uint8_t                  func_id,
                                      uint8_t                  alt_itf,
                                      audio_feedback_params_t* p)
    {
        (void)func_id;
        (void)alt_itf;
        // Track the OUT buffer's fill level around half full: the computer is told to
        // send a little more or less so the codec's own clock never starves or floods.
        p->method      = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
        p->sample_freq = AUDIO_SAMPLE_RATE;
    }

    // Endpoint requests: the sampling frequency (one rate, 48 kHz)
    bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const* req, uint8_t* buf)
    {
        (void)rhport;
        (void)buf;
        return TU_U16_HIGH(req->wValue) == AUDIO10_EP_CTRL_SAMPLING_FREQ
               && req->bRequest == AUDIO10_CS_REQ_SET_CUR;
    }

    bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const* req)
    {
        if(TU_U16_HIGH(req->wValue) == AUDIO10_EP_CTRL_SAMPLING_FREQ
           && req->bRequest == AUDIO10_CS_REQ_GET_CUR)
        {
            uint8_t freq[3] = {(uint8_t)(AUDIO_SAMPLE_RATE & 0xFF),
                               (uint8_t)((AUDIO_SAMPLE_RATE >> 8) & 0xFF),
                               (uint8_t)((AUDIO_SAMPLE_RATE >> 16) & 0xFF)};
            return tud_audio_buffer_and_schedule_control_xfer(rhport, req, freq, sizeof(freq));
        }
        return false;
    }

    // Feature unit: master mute and volume, applied to the Seed's outputs
    bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const* req, uint8_t* buf)
    {
        (void)rhport;
        uint8_t const entity = TU_U16_HIGH(req->wIndex);
        uint8_t const ctrl   = TU_U16_HIGH(req->wValue);
        if(entity != UAC1_SPK_FEATURE_UNIT || req->bRequest != AUDIO10_CS_REQ_SET_CUR)
            return false;
        if(ctrl == AUDIO10_FU_CTRL_MUTE)
        {
            spk_mute = buf[0] != 0;
            return true;
        }
        if(ctrl == AUDIO10_FU_CTRL_VOLUME)
        {
            spk_volume = (int16_t)tu_unaligned_read16(buf);
            spk_gain   = powf(10.f, (spk_volume / 256.f) / 20.f);
            return true;
        }
        return false;
    }

    bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const* req)
    {
        uint8_t const entity = TU_U16_HIGH(req->wIndex);
        uint8_t const ctrl   = TU_U16_HIGH(req->wValue);
        if(entity != UAC1_SPK_FEATURE_UNIT)
            return false;
        if(ctrl == AUDIO10_FU_CTRL_MUTE && req->bRequest == AUDIO10_CS_REQ_GET_CUR)
        {
            uint8_t m = spk_mute;
            return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &m, 1);
        }
        if(ctrl == AUDIO10_FU_CTRL_VOLUME)
        {
            int16_t v;
            switch(req->bRequest)
            {
                case AUDIO10_CS_REQ_GET_CUR: v = spk_volume; break;
                case AUDIO10_CS_REQ_GET_MIN: v = -60 * 256; break; // -60 dB
                case AUDIO10_CS_REQ_GET_MAX: v = 0; break;         // 0 dB
                case AUDIO10_CS_REQ_GET_RES: v = 256; break;       // 1 dB steps
                default: return false;
            }
            return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &v, sizeof(v));
        }
        return false;
    }
}

//------------- Serial and MIDI -------------//

static void CdcPrint(const char* s)
{
    if(!tud_cdc_connected())
        return;
    tud_cdc_write_str(s);
    tud_cdc_write_flush();
}

static void PrintStats()
{
    static const char* const src_name[] = {"codec in", "loopback", "test tone"};
    char                     buf[512];
    snprintf(buf,
             sizeof(buf),
             "\r\nto Seed:   %s, %s, %lu bytes, %lu underruns, buffer %u..%u of %u\r\n"
             "from Seed: %s, source %s, %lu bytes, %lu overruns\r\n"
             "volume %d dB%s, up %lu ms\r\n"
             "IN packets %lu (%lu bytes, %u..%u each), IN fifo max %u of %u; audio callbacks %lu\r\n",
             spk_streaming ? "streaming" : "idle",
             spk_playing ? "playing" : "filling",
             (unsigned long)spk_bytes,
             (unsigned long)spk_underruns,
             (unsigned)(spk_level_min == 0xFFFF ? 0 : spk_level_min),
             (unsigned)spk_level_max,
             (unsigned)CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ,
             mic_streaming ? "streaming" : "idle",
             src_name[(int)mic_source],
             (unsigned long)mic_bytes,
             (unsigned long)mic_overruns,
             spk_volume / 256,
             spk_mute ? " (muted)" : "",
             (unsigned long)System::GetNow(),
             (unsigned long)tx_packets,
             (unsigned long)tx_bytes,
             (unsigned)(tx_pkt_min == 0xFFFF ? 0 : tx_pkt_min),
             (unsigned)tx_pkt_max,
             (unsigned)mic_fill_max,
             (unsigned)CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ,
             (unsigned long)callbacks);
    CdcPrint(buf);
    tx_pkt_min = 0xFFFF;
    tx_pkt_max = 0;
    mic_fill_max = 0;
    spk_level_min = 0xFFFF; // the range is per report
    spk_level_max = 0;
}

static void CdcTask()
{
    static bool was_connected = false;
    bool        connected     = tud_cdc_connected();
    if(connected && !was_connected)
        CdcPrint("\r\nRodeo USB audio test: TinyUSB " TUSB_VERSION_STRING
                 ", UAC1 2x2 48 kHz 24-bit + MIDI + serial.\r\n"
                 "s = stats; recording source: c = codec in, l = loopback, t = test tone;\r\n"
                 "B = reboot into DFU (development)\r\n");
    was_connected = connected;

    while(tud_cdc_available())
    {
        char c = (char)tud_cdc_read_char();
        switch(c)
        {
            case 's': PrintStats(); break;
            case 'B':
                // Straight into the STM32's own DFU bootloader, for reflashing without
                // the buttons (development only)
                CdcPrint("rebooting into the DFU bootloader\r\n");
                System::Delay(50);
                System::ResetToBootloader(System::BootloaderMode::STM);
                break;
            case 'c':
                mic_source = MicSource::Codec;
                CdcPrint("source: codec in\r\n");
                break;
            case 'l':
                mic_source = MicSource::Loopback;
                CdcPrint("source: loopback\r\n");
                break;
            case 't':
                mic_source = MicSource::Tone;
                CdcPrint("source: test tone\r\n");
                break;
            default: break;
        }
    }
}

static void MidiTask()
{
    uint8_t packet[4];
    while(tud_midi_available() && tud_midi_packet_read(packet))
        tud_midi_packet_write(packet);
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(kBlock);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    RouteUsbIrqToTinyUsb();
    UsbHardwareInit();
    tusb_rhport_init_t dev_init = {TUSB_ROLE_DEVICE, TUSB_SPEED_AUTO};
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    hw.StartAudio(AudioCallback);

    uint32_t blink = 0;
    for(;;)
    {
        tud_task();
        CdcTask();
        MidiTask();

        uint32_t now = System::GetNow();
        uint32_t period = spk_streaming || mic_streaming ? 60 : (tud_mounted() ? 250 : 500);
        if(now - blink >= period)
        {
            blink = now;
            static bool led = false;
            led             = !led;
            hw.SetLed(led);
        }
    }
}
