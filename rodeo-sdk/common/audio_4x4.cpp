#include "audio_4x4.h"

#include "board.h"
#include "board_daisy.h"

using namespace daisy;

namespace rodeo
{
namespace
{
GPIO codec2_reset;
AudioHandle::AudioCallback callback;

Pin Seed(board::SeedPin p) { return board::ToDaisy(p); }

// Rounds a sample to 16 bits. libDaisy scales by 2^23 and truncates, so k/32768
// arrives as exactly k*256 and the low byte goes out as zeros. -32768 is left
// out because libDaisy clamps -1.0 to just above it.
float To16(float x)
{
    float k = x * 32768.f;
    k       = k >= 32767.f ? 32767.f : k <= -32767.f ? -32767.f : k;
    return (float)(int32_t)(k + (k >= 0.f ? 0.5f : -0.5f)) * (1.f / 32768.f);
}

// On Rev B1 the CS4272's SDIN runs alongside its SDOUT, and the DAC's data
// corrupts the ADC's: inputs 3/4 took 500-1,500 bad samples per channel every
// 20 s while outputs 3/4 played 24-bit data, and 0-10 with 16-bit data (see
// hardware/docs/revisions.md). Sending outputs 3/4 as 16 bits keeps most of
// SDIN's low byte quiet and costs nothing audible (a -96 dB floor). Drop this
// once a motherboard with the routing fixed is the only one around.
void Callback(AudioHandle::InputBuffer  in,
              AudioHandle::OutputBuffer out,
              size_t                    size)
{
    callback(in, out, size);
    for(size_t i = 0; i < size; i++)
    {
        out[2][i] = To16(out[2][i]);
        out[3][i] = To16(out[3][i]);
    }
}
} // namespace

void InitAudio4x4(DaisySeed& hw, size_t blocksize)
{
    codec2_reset.Init(Seed(board::pin::kCodec2Reset), GPIO::Mode::OUTPUT);
    codec2_reset.Write(false);

    // SAI1, the Seed's codec, as DaisySeed::ConfigureAudio set it up. The codec
    // itself (WM8731 registers, AK4556 reset, PCM3060 de-emphasis) was handled
    // by hw.Init() and isn't touched again.
    SaiHandle::Config sai1;
    sai1.periph          = SaiHandle::Config::Peripheral::SAI_1;
    sai1.sr              = SaiHandle::Config::SampleRate::SAI_48KHZ;
    sai1.bit_depth       = SaiHandle::Config::BitDepth::SAI_24BIT;
    sai1.a_sync          = SaiHandle::Config::Sync::MASTER;
    sai1.b_sync          = SaiHandle::Config::Sync::SLAVE;
    sai1.pin_config.fs   = Pin(PORTE, 4);
    sai1.pin_config.mclk = Pin(PORTE, 2);
    sai1.pin_config.sck  = Pin(PORTE, 5);
    sai1.pin_config.sa   = Pin(PORTE, 6);
    sai1.pin_config.sb   = Pin(PORTE, 3);
    const bool seed_1_1  = hw.CheckBoardVersion() == DaisySeed::BoardVersion::DAISY_SEED_1_1;
    sai1.a_dir = seed_1_1 ? SaiHandle::Config::Direction::RECEIVE
                          : SaiHandle::Config::Direction::TRANSMIT;
    sai1.b_dir = seed_1_1 ? SaiHandle::Config::Direction::TRANSMIT
                          : SaiHandle::Config::Direction::RECEIVE;

    // SAI2, the CS4272. Block B is the clock master and receives from the
    // codec's ADC; block A follows it and sends to the DAC.
    SaiHandle::Config sai2;
    sai2.periph          = SaiHandle::Config::Peripheral::SAI_2;
    sai2.sr              = SaiHandle::Config::SampleRate::SAI_48KHZ;
    sai2.bit_depth       = SaiHandle::Config::BitDepth::SAI_24BIT;
    sai2.a_sync          = SaiHandle::Config::Sync::SLAVE;
    sai2.b_sync          = SaiHandle::Config::Sync::MASTER;
    sai2.a_dir           = SaiHandle::Config::Direction::TRANSMIT;
    sai2.b_dir           = SaiHandle::Config::Direction::RECEIVE;
    sai2.pin_config.fs   = Seed(board::pin::kCodec2Lrck);
    sai2.pin_config.mclk = Seed(board::pin::kCodec2Mclk);
    sai2.pin_config.sck  = Seed(board::pin::kCodec2Sclk);
    sai2.pin_config.sa   = Seed(board::pin::kCodec2Sdin);
    sai2.pin_config.sb   = Seed(board::pin::kCodec2Sdout);

    SaiHandle sai[2];
    sai[0].Init(sai1);
    sai[1].Init(sai2);

    AudioHandle::Config cfg;
    cfg.blocksize  = blocksize;
    cfg.samplerate = SaiHandle::Config::SampleRate::SAI_48KHZ;
    cfg.postgain   = 1.f;
    hw.audio_handle.Init(cfg, sai[0], sai[1]);

    // libDaisy pulls every SAI pin up, SD_B included, and SD_B is the CS4272's
    // SDOUT: the pin it reads as it leaves reset to choose master or slave. Its
    // 47k pull-down (R19) against the STM32's ~40k pull-up sits near the
    // threshold, so the codec came up as master about half the time, its
    // clocks fighting SAI2's and its data a few bits off or all noise.
    const board::SeedPin sdout = board::pin::kCodec2Sdout;
    GPIO_TypeDef* port = reinterpret_cast<GPIO_TypeDef*>(
        GPIOA_BASE + (sdout.port - 'A') * (GPIOB_BASE - GPIOA_BASE));
    port->PUPDR &= ~(3u << (sdout.num * 2));
}

void StartAudio4x4(DaisySeed& hw, AudioHandle::AudioCallback cb)
{
    callback = cb;
    hw.StartAudio(Callback);
    // The clocks start with the DMA. The CS4272 wants them stable before RST
    // rises; a millisecond is ~48 LRCK periods.
    System::Delay(1);
    codec2_reset.Write(true);
}

void StopAudio4x4(DaisySeed& hw)
{
    codec2_reset.Write(false);
    hw.StopAudio();
}
} // namespace rodeo
