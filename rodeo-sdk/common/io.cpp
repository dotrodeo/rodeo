#include "io.h"

#include "board_daisy.h"

using namespace daisy;

namespace rodeo
{
bool Io::Init(const IoConfig& cfg)
{
    cfg_  = cfg;
    plan_ = PlanIo(cfg);
    if(!plan_.ok)
        return false;

    for(int i = 0; i < board::kNumIo; i++)
    {
        Pin pin = board::ToDaisy(board::kIo[i].pin);
        switch(cfg.mode[i])
        {
            case IoMode::kDigitalIn: gpio_[i].Init(pin, GPIO::Mode::INPUT, GPIO::Pull::NOPULL); break;
            case IoMode::kDigitalInPullUp:
                gpio_[i].Init(pin, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
                break;
            case IoMode::kDigitalInPullDown:
                gpio_[i].Init(pin, GPIO::Mode::INPUT, GPIO::Pull::PULLDOWN);
                break;
            case IoMode::kDigitalOut:
                gpio_[i].Init(pin, GPIO::Mode::OUTPUT);
                gpio_[i].Write(false);
                break;
            default: break; // analog and CV pins are set up by their peripherals
        }
    }

    if(plan_.n_adc)
    {
        for(int k = 0; k < plan_.n_adc; k++)
            adc_cfg_[k].InitSingle(board::ToDaisy(board::kIo[plan_.adc_conn[k]].pin));
        adc_.Init(adc_cfg_, plan_.n_adc);
        adc_.Start();
    }

    if(plan_.dac1 || plan_.dac2)
    {
        DacHandle::Config d;
        d.target_samplerate = 48000; // unused in polling mode
        d.chn               = plan_.dac1 && plan_.dac2 ? DacHandle::Channel::BOTH
                              : plan_.dac1             ? DacHandle::Channel::ONE
                                                       : DacHandle::Channel::TWO;
        d.mode              = DacHandle::Mode::POLLING;
        d.bitdepth          = DacHandle::BitDepth::BITS_12;
        d.buff_state        = DacHandle::BufferState::ENABLED; // drives a cable
        dac_.Init(d);
        if(plan_.dac1)
            dac_.WriteValue(DacHandle::Channel::ONE, 0);
        if(plan_.dac2)
            dac_.WriteValue(DacHandle::Channel::TWO, 0);
    }
    return true;
}

bool Io::Get(board::Io c)
{
    switch(cfg_.mode[c])
    {
        case IoMode::kDigitalIn:
        case IoMode::kDigitalInPullUp:
        case IoMode::kDigitalInPullDown: return gpio_[c].Read();
        case IoMode::kDigitalOut: return out_[c];
        default: return false;
    }
}

void Io::Set(board::Io c, bool high)
{
    if(cfg_.mode[c] != IoMode::kDigitalOut)
        return;
    out_[c] = high;
    gpio_[c].Write(high);
}

float Io::Read(board::Io c) const
{
    int k = plan_.adc_index[c];
    return k < 0 ? 0.0f : adc_.GetFloat(static_cast<uint8_t>(k));
}

void Io::Write(board::Io c, float v)
{
    if(cfg_.mode[c] != IoMode::kCvOut)
        return;
    dac_.WriteValue(board::kIo[c].dac == 1 ? DacHandle::Channel::ONE : DacHandle::Channel::TWO,
                    DacCode(v));
}
} // namespace rodeo
