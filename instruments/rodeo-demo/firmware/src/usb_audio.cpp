// USB audio for rodeo-demo. See usb_audio.h; the callbacks below are the
// usb_audio example's, without its test sources and diagnostics.
#include "usb_audio.h"

#include <math.h>
#include "tusb.h"
#include "usb_descriptors.h"

namespace
{
constexpr size_t kMaxFrames  = 48; // the audio block: 1 ms at 48 kHz
constexpr size_t kFrameBytes = AUDIO_N_CHANNELS * AUDIO_BYTES_PER_SAMPLE;
// Start playing once 2 ms are queued: half the OUT buffer, the feedback's target level
constexpr uint16_t kPrefill = CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ / 2;

volatile bool spk_streaming = false, mic_streaming = false;
bool          spk_playing   = false;
volatile bool spk_mute      = false;
volatile int16_t spk_volume = 0; // 1/256 dB
volatile float   spk_gain   = 1.0f;
volatile uint32_t underruns = 0, overruns = 0;

uint8_t usb_rx[kMaxFrames * kFrameBytes];
uint8_t usb_tx[kMaxFrames * kFrameBytes];

// One little-endian sample of AUDIO_BITS, packed, to and from a float.
constexpr float kFullScale = (float)(1L << (AUDIO_BITS - 1));

inline float Unpack(const uint8_t* p)
{
    int32_t v = 0;
    for(int b = 0; b < AUDIO_BYTES_PER_SAMPLE; b++)
        v |= (int32_t)((uint32_t)p[b] << (8 * (4 - AUDIO_BYTES_PER_SAMPLE + b)));
    v >>= 8 * (4 - AUDIO_BYTES_PER_SAMPLE); // sign-extend
    return (float)v / kFullScale;
}

inline void Pack(uint8_t* p, float x)
{
    if(x < -1.0f)
        x = -1.0f;
    int32_t v = (int32_t)lrintf(x * kFullScale);
    if(v > (int32_t)kFullScale - 1)
        v = (int32_t)kFullScale - 1;
    for(int b = 0; b < AUDIO_BYTES_PER_SAMPLE; b++)
        p[b] = (uint8_t)(v >> (8 * b));
}
} // namespace

namespace usb_audio
{
static_assert(kChannels == AUDIO_N_CHANNELS, "the descriptor's channel count");

void Receive(float* const* from_computer, size_t frames)
{
    if(frames > kMaxFrames)
        frames = kMaxFrames;
    size_t got = 0;
    if(spk_streaming)
    {
        if(!spk_playing && tud_audio_available() >= kPrefill)
            spk_playing = true;
        if(spk_playing)
        {
            got = tud_audio_read(usb_rx, frames * kFrameBytes);
            if(got < frames * kFrameBytes)
            {
                underruns++;
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
        for(int c = 0; c < kChannels; c++)
            from_computer[c][i]
                = have ? gain * Unpack(&usb_rx[i * kFrameBytes + c * AUDIO_BYTES_PER_SAMPLE]) : 0.f;
    }
}

void Send(const float* const* to_computer, size_t frames)
{
    if(!mic_streaming)
        return;
    if(frames > kMaxFrames)
        frames = kMaxFrames;
    for(size_t i = 0; i < frames; i++)
        for(int c = 0; c < kChannels; c++)
            Pack(&usb_tx[i * kFrameBytes + c * AUDIO_BYTES_PER_SAMPLE], to_computer[c][i]);
    // TinyUSB's IN FIFO overwrites its oldest data when full, so check the room
    // first: drop whole new frames (and count it) rather than corrupt queued ones.
    uint16_t room = tu_fifo_remaining(tud_audio_get_ep_in_ff());
    uint16_t want = (uint16_t)(frames * kFrameBytes);
    if(room < want)
    {
        overruns++;
        want = (uint16_t)(room / kFrameBytes * kFrameBytes);
    }
    if(want)
        tud_audio_write(usb_tx, want);
}

Stats GetStats()
{
    return {spk_streaming, mic_streaming, underruns, overruns};
}
} // namespace usb_audio

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

    void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t* p)
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

    // Feature unit: mute and volume of what the computer plays
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
