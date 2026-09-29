// rodeo-002 — candidate voice engines behind one uniform interface.
//
// Every engine in the rodeo-002 concept is driven by the same three macros
// (Harmonics / Timbre / Morph), so the panel never changes meaning. This header
// is that contract in code, and exists mainly so the benchmark can sweep all
// engines through identical harness code.
//
// Only ONE engine is live at a time (engine is a per-patch choice), so each
// voice holds a single raw storage buffer sized to the largest engine and
// placement-news the selected one into it. Dispatch is a switch on the id --
// no virtuals, so the audio loop stays inlinable and the cost measured here is
// the DSP's, not the abstraction's.
//
// All of these ship in the vendored DaisySP (they are the Plaits-derived
// modules Emilie Gillet contributed upstream). Wavetable and Chord are NOT here
// -- they need porting from the MIT-licensed Mutable source and are deferred.
//
// MODAL IS DROPPED (Derek, 2026-09-22): DaisySP's ModalVoice measured 46% CPU
// for ONE voice, so it could not run at all beside an expensive Space type. The
// DSP is still in DaisySP if it is ever worth optimising back in.

#pragma once

#include <new>
#include <cstddef>
#include <cstdint>

#include "daisysp.h"

namespace rodeo002
{
enum class EngineId : uint8_t
{
    kVirtualAnalog = 0,  // VariableShapeOscillator, hard-synced
    kVariableSaw,        // VariableSawOscillator
    kFm2,                // 2-operator FM
    kFormant,            // FormantOscillator
    kVosim,              // VosimOscillator
    kHarmonic,           // HarmonicOscillator<16> (additive)
    kZOsc,               // ZOscillator
    kGrainlet,           // GrainletOscillator
    kParticle,           // Particle (resonant noise grains)
    kString,             // StringVoice (plucked/bowed)
    kKarplus,            // KarplusString, externally excited
    kCount
};

inline const char* EngineName(EngineId id)
{
    switch(id)
    {
        case EngineId::kVirtualAnalog: return "virtual-analog";
        case EngineId::kVariableSaw: return "variable-saw";
        case EngineId::kFm2: return "fm-2op";
        case EngineId::kFormant: return "formant";
        case EngineId::kVosim: return "vosim";
        case EngineId::kHarmonic: return "harmonic-16";
        case EngineId::kZOsc: return "z-osc";
        case EngineId::kGrainlet: return "grainlet";
        case EngineId::kParticle: return "particle";
        case EngineId::kString: return "string";
        case EngineId::kKarplus: return "karplus";
        default: return "?";
    }
}

/** Engines that are struck/plucked rather than continuously oscillating: they
 *  need a trigger and decay on their own, so a sustained benchmark note
 *  under-reports them unless it re-triggers. The harness re-triggers these. */
inline bool EngineIsPercussive(EngineId id)
{
    return id == EngineId::kString || id == EngineId::kKarplus;
}

/** One engine instance. Init() picks which; the macros are control-rate. */
class Engine
{
  public:
    void Init(EngineId id, float sample_rate)
    {
        id_          = id;
        sample_rate_ = sample_rate;
        noise_.Init();

        switch(id)
        {
            case EngineId::kVirtualAnalog:
                (new(storage_) daisysp::VariableShapeOscillator())
                    ->Init(sample_rate);
                break;
            case EngineId::kVariableSaw:
                (new(storage_) daisysp::VariableSawOscillator())
                    ->Init(sample_rate);
                break;
            case EngineId::kFm2:
                (new(storage_) daisysp::Fm2())->Init(sample_rate);
                break;
            case EngineId::kFormant:
                (new(storage_) daisysp::FormantOscillator())->Init(sample_rate);
                break;
            case EngineId::kVosim:
                (new(storage_) daisysp::VosimOscillator())->Init(sample_rate);
                break;
            case EngineId::kHarmonic:
                (new(storage_) daisysp::HarmonicOscillator<16>())
                    ->Init(sample_rate);
                break;
            case EngineId::kZOsc:
                (new(storage_) daisysp::ZOscillator())->Init(sample_rate);
                break;
            case EngineId::kGrainlet:
                (new(storage_) daisysp::GrainletOscillator())
                    ->Init(sample_rate);
                break;
            case EngineId::kParticle:
                (new(storage_) daisysp::Particle())->Init(sample_rate);
                break;
            case EngineId::kString:
                (new(storage_) daisysp::StringVoice())->Init(sample_rate);
                break;
            case EngineId::kKarplus:
                (new(storage_) daisysp::String())->Init(sample_rate);
                break;
            default: break;
        }
    }

    /** Control-rate parameter update. `harmonics`, `timbre`, `morph` are 0..1
     *  and mean whatever is idiomatic for the engine -- that per-engine
     *  interpretation is the whole point of the macro scheme. */
    void SetParams(float freq, float harmonics, float timbre, float morph)
    {
        freq_ = freq;
        switch(id_)
        {
            case EngineId::kVirtualAnalog:
            {
                auto* o = As<daisysp::VariableShapeOscillator>();
                o->SetFreq(freq);
                o->SetSync(true);
                o->SetSyncFreq(freq * (1.f + 3.f * harmonics));
                o->SetPW(0.5f + 0.45f * (timbre - 0.5f));
                o->SetWaveshape(morph);
                break;
            }
            case EngineId::kVariableSaw:
            {
                auto* o = As<daisysp::VariableSawOscillator>();
                o->SetFreq(freq);
                o->SetPW(0.5f + 0.45f * (timbre - 0.5f));
                o->SetWaveshape(morph);
                break;
            }
            case EngineId::kFm2:
            {
                auto* o = As<daisysp::Fm2>();
                o->SetFrequency(freq);
                o->SetRatio(1.f + 7.f * harmonics);
                o->SetIndex(timbre);
                break;
            }
            case EngineId::kFormant:
            {
                auto* o = As<daisysp::FormantOscillator>();
                o->SetCarrierFreq(freq);
                o->SetFormantFreq(freq * (1.f + 7.f * harmonics));
                o->SetPhaseShift(morph);
                break;
            }
            case EngineId::kVosim:
            {
                auto* o = As<daisysp::VosimOscillator>();
                o->SetFreq(freq);
                o->SetForm1Freq(freq * (1.f + 7.f * harmonics));
                o->SetForm2Freq(freq * (1.f + 11.f * timbre));
                o->SetShape(morph);
                break;
            }
            case EngineId::kHarmonic:
            {
                auto* o = As<daisysp::HarmonicOscillator<16>>();
                o->SetFreq(freq);
                // Tilt the partials with `timbre`: bright = slower rolloff.
                float amp = 1.f;
                for(int i = 0; i < 16; i++)
                {
                    o->SetSingleAmp(amp, i);
                    amp *= 0.35f + 0.6f * timbre;
                }
                break;
            }
            case EngineId::kZOsc:
            {
                auto* o = As<daisysp::ZOscillator>();
                o->SetFreq(freq);
                o->SetFormantFreq(freq * (1.f + 7.f * harmonics));
                o->SetShape(timbre);
                o->SetMode(morph);
                break;
            }
            case EngineId::kGrainlet:
            {
                auto* o = As<daisysp::GrainletOscillator>();
                o->SetFreq(freq);
                o->SetFormantFreq(freq * (1.f + 7.f * harmonics));
                o->SetShape(timbre);
                o->SetBleed(morph);
                break;
            }
            case EngineId::kParticle:
            {
                auto* o = As<daisysp::Particle>();
                o->SetFreq(freq);
                o->SetResonance(0.5f + 0.49f * harmonics);
                o->SetDensity(timbre);
                o->SetSpread(morph);
                o->SetGain(1.f);
                break;
            }
            case EngineId::kString:
            {
                auto* o = As<daisysp::StringVoice>();
                o->SetFreq(freq);
                o->SetStructure(harmonics);
                o->SetBrightness(timbre);
                o->SetDamping(morph);
                break;
            }
            case EngineId::kKarplus:
            {
                auto* o = As<daisysp::String>();
                o->SetFreq(freq);
                o->SetBrightness(timbre);
                o->SetDamping(0.3f + 0.69f * morph);
                o->SetNonLinearity(harmonics * 0.5f);
                break;
            }
            default: break;
        }
    }

    /** Strike the percussive engines; harmless on the rest. */
    void Trig()
    {
        switch(id_)
        {
            case EngineId::kString: As<daisysp::StringVoice>()->Trig(); break;
            case EngineId::kKarplus:
                // KarplusString takes an external exciter -- burst some noise
                // into it for ~3 ms, the way Plaits' string engine does.
                exciter_samples_ = static_cast<int>(sample_rate_ * 0.003f);
                break;
            default: break;
        }
    }

    float Process()
    {
        switch(id_)
        {
            case EngineId::kVirtualAnalog:
                return As<daisysp::VariableShapeOscillator>()->Process();
            case EngineId::kVariableSaw:
                return As<daisysp::VariableSawOscillator>()->Process();
            case EngineId::kFm2: return As<daisysp::Fm2>()->Process();
            case EngineId::kFormant:
                return As<daisysp::FormantOscillator>()->Process();
            case EngineId::kVosim:
                return As<daisysp::VosimOscillator>()->Process();
            case EngineId::kHarmonic:
                return As<daisysp::HarmonicOscillator<16>>()->Process();
            case EngineId::kZOsc: return As<daisysp::ZOscillator>()->Process();
            case EngineId::kGrainlet:
                return As<daisysp::GrainletOscillator>()->Process();
            case EngineId::kParticle: return As<daisysp::Particle>()->Process();
            case EngineId::kString: return As<daisysp::StringVoice>()->Process();
            case EngineId::kKarplus:
            {
                float in = 0.f;
                if(exciter_samples_ > 0)
                {
                    in = noise_.Process() * 0.5f;
                    exciter_samples_--;
                }
                return As<daisysp::String>()->Process(in);
            }
            default: return 0.f;
        }
    }

  private:
    // Never instantiated -- declared only so sizeof/alignof cover every engine.
    union Storage
    {
        daisysp::VariableShapeOscillator va;
        daisysp::VariableSawOscillator   saw;
        daisysp::Fm2                     fm;
        daisysp::FormantOscillator       formant;
        daisysp::VosimOscillator         vosim;
        daisysp::HarmonicOscillator<16>  harmonic;
        daisysp::ZOscillator             zosc;
        daisysp::GrainletOscillator      grainlet;
        daisysp::Particle                particle;
        daisysp::StringVoice             string;
        daisysp::String                  karplus;
    };

    template <typename T>
    T* As()
    {
        return reinterpret_cast<T*>(storage_);
    }

    alignas(alignof(Storage)) unsigned char storage_[sizeof(Storage)];
    daisysp::WhiteNoise noise_;
    EngineId            id_              = EngineId::kVirtualAnalog;
    float               sample_rate_     = 48000.f;
    float               freq_            = 110.f;
    int                 exciter_samples_ = 0;
};

} // namespace rodeo002
