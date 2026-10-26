#include "audio_4x4.h"

#include "board.h"
#include "board_daisy.h"

using namespace daisy;

namespace rodeo
{
namespace
{
GPIO codec2_reset;

Pin Seed(board::SeedPin p) { return board::ToDaisy(p); }
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
}

void StartAudio4x4(DaisySeed& hw, AudioHandle::AudioCallback cb)
{
    hw.StartAudio(cb);
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
