// Rev B bring-up 10: Rodeo as a USB audio interface, four channels each way across
// both codecs, with USB-MIDI and a serial port, on TinyUSB.
//
// usb_device's stereo audio (proven on a bare Seed against macOS) widened to 4x4
// on common/audio_4x4.h: back-panel channels 1/2 are the Seed's codec, 3/4 the
// CS4272, and USB channel n is back-panel channel n both ways.
//
//   * Computer -> Rodeo: what the computer plays to "Rodeo 4x4" channels 1-4 comes
//     out of outputs 1-4. Asynchronous: the codec clock sets the rate and a feedback
//     endpoint keeps the computer in step.
//   * Rodeo -> computer: what "Rodeo 4x4" records, chosen on the serial port:
//       c  inputs 1-4 (default)
//       l  loopback: what the computer is playing, returned digitally
//       t  test tones, 250/500/1000/2000 Hz on channels 1-4 at -12 dBFS (the
//          frequencies audio_4x4 uses, so a swapped channel is obvious)
//   * Serial: 's' prints stream state and counters; 'B' reboots into the Daisy
//     bootloader, for `make program-dfu` over the USB-C.
//   * MIDI: echoes what it receives.
//
//   make                 Rev B's USB-C, through the power board (OTG_HS, pins 36/37)
//   make RODEO_USB=fs    the Seed's own micro-USB
//
// 4x4 at 24-bit is 588-byte packets each way, 89 % of a full-speed frame's
// periodic budget (planning/REV_B_PLAN.md, "Bandwidth fits"), so this is also the
// test of whether the DWC2 core and the computer's driver keep up at that load.
// The checks, in order: it enumerates with four channels each way on macOS,
// Windows and Linux; 't' records the four tones on the right channels; playing to
// channel n with output n patched to input n comes back on channel n ('c'); and a
// long full-duplex loopback stays glitch-free (record with AVAudioEngine or a
// DAW, not ffmpeg: see the plan's test-tool trap).
#include <math.h>
#include "daisy_seed.h"
#include "audio_4x4.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "usb_port.h"
#include "dfu.h"

using namespace daisy;

static DaisySeed hw;

//------------- Audio state -------------//

static constexpr size_t kBlock      = 48; // frames per callback: 1 ms at 48 kHz
static constexpr size_t kFrameBytes = AUDIO_N_CHANNELS * AUDIO_BYTES_PER_SAMPLE;
static constexpr size_t kBlockBytes = kBlock * kFrameBytes;
// Start playing once 2 ms are queued: half the OUT buffer, the feedback's target level
static constexpr uint16_t kPrefill = CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ / 2;
static constexpr float    kToneHz[AUDIO_N_CHANNELS] = {250.f, 500.f, 1000.f, 2000.f};

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

static volatile uint32_t spk_underruns = 0; // computer -> Rodeo: buffer ran dry
static volatile uint32_t mic_overruns  = 0; // Rodeo -> computer: buffer full, samples dropped
static volatile uint32_t spk_bytes     = 0;
static volatile uint32_t mic_bytes     = 0;
static volatile uint16_t spk_level_min = 0xFFFF, spk_level_max = 0;

// Diagnostics
static volatile uint32_t tx_packets = 0, tx_bytes = 0;
static volatile uint16_t tx_pkt_min = 0xFFFF, tx_pkt_max = 0;
static volatile uint16_t mic_fill_max = 0;
static volatile uint32_t callbacks    = 0;
static volatile float    in_peak[AUDIO_N_CHANNELS]; // codec inputs, per report

static volatile bool    spk_mute   = false;
static volatile int16_t spk_volume = 0; // 1/256 dB, master
static volatile float   spk_gain   = 1.0f;

static uint8_t usb_rx[kBlockBytes];
static uint8_t usb_tx[kBlockBytes];
static float   loop[AUDIO_N_CHANNELS][kBlock];
static float   tone_phase[AUDIO_N_CHANNELS];

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

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    frames)
{
    callbacks++;

    //--- computer -> Rodeo
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
        bool const have = (i + 1) * kFrameBytes <= got;
        for(int c = 0; c < AUDIO_N_CHANNELS; c++)
        {
            float x    = have ? Unpack24(&usb_rx[i * kFrameBytes + c * AUDIO_BYTES_PER_SAMPLE]) : 0.f;
            loop[c][i] = x;
            out[c][i]  = x * gain;
            float a    = fabsf(in[c][i]);
            if(a > in_peak[c])
                in_peak[c] = a;
        }
    }

    //--- Rodeo -> computer
    if(mic_streaming)
    {
        for(size_t i = 0; i < frames; i++)
        {
            for(int c = 0; c < AUDIO_N_CHANNELS; c++)
            {
                float x;
                switch(mic_source)
                {
                    case MicSource::Loopback: x = loop[c][i]; break;
                    case MicSource::Tone:
                        x = 0.25f * sinf(2.f * (float)M_PI * tone_phase[c]);
                        tone_phase[c] += kToneHz[c] / AUDIO_SAMPLE_RATE;
                        if(tone_phase[c] >= 1.f)
                            tone_phase[c] -= 1.f;
                        break;
                    default: x = in[c][i]; break;
                }
                Pack24(&usb_tx[i * kFrameBytes + c * AUDIO_BYTES_PER_SAMPLE], x);
            }
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

    // Feature unit: master mute and volume, applied to all four outputs
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
    static const char* const src_name[] = {"inputs 1-4", "loopback", "test tones"};
    char                     buf[640];
    snprintf(buf,
             sizeof(buf),
             "\r\nto Rodeo:   %s, %s, %lu bytes, %lu underruns, buffer %u..%u of %u\r\n"
             "from Rodeo: %s, source %s, %lu bytes, %lu overruns\r\n"
             "inputs 1-4 peak %d %d %d %d /1000; volume %d dB%s, up %lu ms\r\n"
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
             (int)lroundf(in_peak[0] * 1000.f),
             (int)lroundf(in_peak[1] * 1000.f),
             (int)lroundf(in_peak[2] * 1000.f),
             (int)lroundf(in_peak[3] * 1000.f),
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
    tx_pkt_min    = 0xFFFF;
    tx_pkt_max    = 0;
    mic_fill_max  = 0;
    spk_level_min = 0xFFFF; // the ranges are per report
    spk_level_max = 0;
    for(int c = 0; c < AUDIO_N_CHANNELS; c++)
        in_peak[c] = 0.f;
}

static void CdcTask()
{
    static bool was_connected = false;
    bool        connected     = tud_cdc_connected();
    if(connected && !was_connected)
        CdcPrint("\r\nRodeo USB audio test: TinyUSB " TUSB_VERSION_STRING
                 ", UAC1 4x4 48 kHz 24-bit + MIDI + serial on " RODEO_USB_PORT_NAME ".\r\n"
                 "s = stats; recording source: c = inputs, l = loopback, t = test tones;\r\n"
                 "B = reboot into DFU (development)\r\n");
    was_connected = connected;

    while(tud_cdc_available())
    {
        char c = (char)tud_cdc_read_char();
        switch(c)
        {
            case 's': PrintStats(); break;
            case 'B':
                CdcPrint("rebooting into the bootloader (DFU on the USB-C)\r\n");
                System::Delay(50);
                rodeo::StopAudio4x4(hw);
                ResetToDfu();
                break;
            case 'c':
                mic_source = MicSource::Codec;
                CdcPrint("source: inputs 1-4\r\n");
                break;
            case 'l':
                mic_source = MicSource::Loopback;
                CdcPrint("source: loopback\r\n");
                break;
            case 't':
                mic_source = MicSource::Tone;
                CdcPrint("source: test tones\r\n");
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
    rodeo::InitAudio4x4(hw, kBlock);

    rodeo::UsbPortInit();
    tusb_rhport_init_t dev_init = {TUSB_ROLE_DEVICE, TUSB_SPEED_AUTO};
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    rodeo::StartAudio4x4(hw, AudioCallback);

    uint32_t blink = 0;
    for(;;)
    {
        tud_task();
        CdcTask();
        MidiTask();

        uint32_t now    = System::GetNow();
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
