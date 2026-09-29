// rodeo-002 — the texture chain: four slots in a fixed order, then the output
// stage.
//
//   [CHARACTER] -> [MOVEMENT] -> [TEXTURE] -> [SPACE] -> tilt EQ -> volume
//                                                        -> soft limiter
//
// What the chain adds on top of the slots is click-free switching:
//   * TYPE CHANGES CROSSFADE. The slot fades out over 10 ms, swaps and resets
//     the effect while silent, then fades back in (unless bypassed).
//   * BYPASS CROSSFADES, and a fully bypassed slot is skipped outright -- it
//     costs no CPU, which is also how the benchmark isolates one effect type.
//   * Space bypasses with TRAILS (its tail rings out; see fx_space.h) and goes
//     idle, costing nothing, once the tail has died. Its type changes fade the
//     wet only, so the dry never dips.
//
// The SOFT LIMITER is the last stage: transparent below 0.9, a smooth knee to
// a hard ceiling of 1.0. The codec clips anything over full scale regardless,
// and some legitimate patches get there -- e.g. max-decay Reverb fed by a hot
// Rectify, which the host fuzz test measured at ~5x. OutputPeak() reports the
// level going INTO the limiter, for a clip indicator.
//
// Controls are set at control rate (once per block, from the UI); audio goes
// through ProcessBlock.

#pragma once

#include <cstddef>

#include "dsp_util.h"
#include "fx_character.h"
#include "fx_movement.h"
#include "fx_space.h"
#include "fx_texture.h"

namespace rodeo002
{
namespace fx
{
class TextureChain
{
  public:
    enum Slot
    {
        kCharacter = 0,
        kMovement,
        kTexture,
        kSpace,
        kNumSlots
    };

    static const char* SlotName(int s)
    {
        static const char* kNames[kNumSlots] = {"Character", "Movement", "Texture", "Space"};
        return (s >= 0 && s < kNumSlots) ? kNames[s] : "?";
    }
    static int NumTypes(int s)
    {
        switch(s)
        {
            case kCharacter: return Character::kNumTypes;
            case kMovement: return Movement::kNumTypes;
            case kTexture: return Texture::kNumTypes;
            default: return Space::kNumTypes;
        }
    }
    /** How many options that slot's shift+turn cycles through. Companion to
     *  NumTypes, and what range-checks a stored preset's option value. */
    static int NumOptions(int s)
    {
        switch(s)
        {
            case kCharacter: return Character::kNumOptions;
            case kMovement: return Movement::kNumOptions;
            case kTexture: return Texture::kNumOptions;
            default: return Space::kNumOptions;
        }
    }
    static const char* TypeName(int s, int t)
    {
        switch(s)
        {
            case kCharacter: return Character::TypeName(t);
            case kMovement: return Movement::TypeName(t);
            case kTexture: return Texture::TypeName(t);
            default: return Space::TypeName(t);
        }
    }

    /** Space's buffers are borrowed (SDRAM on the Seed). Call once at startup. */
    void Init(float sample_rate, const Space::Buffers& space_buffers)
    {
        sample_rate_ = sample_rate;
        character_.Init(sample_rate);
        movement_.Init(sample_rate);
        texture_.Init(sample_rate);
        space_.Init(sample_rate, space_buffers);
        fade_step_ = 1.f / (0.01f * sample_rate); // 10 ms
        for(int s = 0; s < kSpace; s++)
        {
            fade_[s]    = 1.f;
            pending_[s] = -1;
            bypass_[s]  = false;
        }
        space_pending_ = -1;
        for(int c = 0; c < 2; c++)
            split_[c].SetFreq(700.f, sample_rate);
        low_gain_.Init(sample_rate, 0.02f, 1.f);
        high_gain_.Init(sample_rate, 0.02f, 1.f);
        volume_.Init(sample_rate, 0.02f, 1.f);
    }

    // ------------------------------------------------------------ slot controls

    /** Change a slot's effect type. Crossfaded: takes effect ~10 ms later. */
    void SetType(int slot, int type)
    {
        if(slot == kSpace)
        {
            if(type == space_.GetType() && space_pending_ < 0)
                return;
            space_pending_ = type;
            space_.SetWetHold(true);
            return;
        }
        if(slot < 0 || slot >= kSpace)
            return;
        if(type == CurrentType(slot) && pending_[slot] < 0)
            return;
        pending_[slot] = type;
    }
    /** The type the slot is (or is about to be) running. */
    int GetType(int slot) const
    {
        if(slot == kSpace)
            return space_pending_ >= 0 ? space_pending_ : space_.GetType();
        return (slot >= 0 && slot < kSpace && pending_[slot] >= 0) ? pending_[slot]
                                                                     : CurrentType(slot);
    }

    void SetBypass(int slot, bool b)
    {
        if(slot == kSpace)
            space_.SetBypass(b);
        else if(slot >= 0 && slot < kSpace)
            bypass_[slot] = b;
    }
    bool GetBypass(int slot) const
    {
        if(slot == kSpace)
            return space_.GetBypass();
        return slot >= 0 && slot < kSpace && bypass_[slot];
    }

    /** The column-4 pot: Drive / Depth / Intensity / Mix. */
    void SetAmount(int slot, float a)
    {
        switch(slot)
        {
            case kCharacter: character_.SetAmount(a); break;
            case kMovement: movement_.SetAmount(a); break;
            case kTexture: texture_.SetAmount(a); break;
            case kSpace: space_.SetMix(a); break;
        }
    }
    /** The column-4 shift pot: Bias / Rate / Tone / Damp. */
    void SetSecondary(int slot, float v)
    {
        switch(slot)
        {
            case kCharacter: character_.SetBias(v); break;
            case kMovement: movement_.SetRate(v); break;
            case kTexture: texture_.SetTone(v); break;
            case kSpace: space_.SetDamp(v); break;
        }
    }
    /** Shift + encoder: low cut / LFO shape / per-type / pitch quantize. */
    void SetOption(int slot, int o)
    {
        switch(slot)
        {
            case kCharacter: character_.SetOption(o); break;
            case kMovement: movement_.SetOption(o); break;
            case kTexture: texture_.SetOption(o); break;
            case kSpace: space_.SetOption(o); break;
        }
    }

    /** Space's own controls (Time, Feedback, Grain, Pitch, Spread, Freeze). */
    Space& space() { return space_; }

    // ------------------------------------------------------------ output stage

    /** 0.5 is flat; the ends tilt +/-6 dB around 700 Hz. */
    void SetTilt(float t)
    {
        const float db = (Clamp(t, 0.f, 1.f) - 0.5f) * 12.f;
        low_gain_.Set(powf(10.f, -db / 40.f));
        high_gain_.Set(powf(10.f, db / 40.f));
    }
    /** Master volume, squared for a more even feel across the pot. */
    void SetVolume(float v)
    {
        v = Clamp(v, 0.f, 1.f);
        volume_.Set(v * v);
    }

    // ------------------------------------------------------------ audio

    void ProcessBlock(float* l, float* r, size_t n)
    {
        // The crossfades need a dry copy; keep it within the scratch buffer.
        while(n > kMaxBlock)
        {
            ProcessBlock(l, r, kMaxBlock);
            l += kMaxBlock;
            r += kMaxBlock;
            n -= kMaxBlock;
        }
        RunSlot(kCharacter, l, r, n);
        RunSlot(kMovement, l, r, n);
        RunSlot(kTexture, l, r, n);
        RunSpace(l, r, n);
        for(size_t i = 0; i < n; i++)
        {
            const float lo = low_gain_.Next(), hi = high_gain_.Next();
            const float vol = volume_.Next();
            const float bl  = split_[0].Lp(l[i]);
            const float br  = split_[1].Lp(r[i]);
            const float ol  = (bl * lo + (l[i] - bl) * hi) * vol;
            const float orr = (br * lo + (r[i] - br) * hi) * vol;
            peak_           = fmaxf(peak_, fmaxf(fabsf(ol), fabsf(orr)));
            l[i]            = Limit(ol);
            r[i]            = Limit(orr);
        }
    }

    /** Peak level into the output limiter since the last call, then reset.
     *  Over 0.9 means the limiter is working -- the OLED's clip indicator. */
    float OutputPeak()
    {
        const float p = peak_;
        peak_         = 0.f;
        return p;
    }

    /** Skip every fade: apply pending types and bypass states NOW. Only for
     *  configuring while the audio callback isn't running the chain -- the
     *  benchmark uses it between measurements. */
    void SnapToTargets()
    {
        for(int s = 0; s < kSpace; s++)
        {
            if(pending_[s] >= 0)
            {
                ApplyType(s, pending_[s]);
                pending_[s] = -1;
            }
            fade_[s] = bypass_[s] ? 0.f : 1.f;
        }
        if(space_pending_ >= 0)
        {
            space_.SetType(space_pending_);
            space_.Reset();
            space_pending_ = -1;
        }
        space_.SetWetHold(false);
        space_.SnapGains();
    }

  private:
    // Identity below 0.9, then a tanh knee that meets the line with slope 1
    // and flattens to exactly 1.0.
    static float Limit(float x)
    {
        const float a = fabsf(x);
        if(a <= 0.9f)
            return x;
        const float y = 0.9f + 0.1f * FastTanh((a - 0.9f) * 10.f);
        return x < 0.f ? -y : y;
    }

    int CurrentType(int slot) const
    {
        switch(slot)
        {
            case kCharacter: return character_.GetType();
            case kMovement: return movement_.GetType();
            default: return texture_.GetType();
        }
    }

    void ApplyType(int slot, int type)
    {
        switch(slot)
        {
            case kCharacter:
                character_.SetType(type);
                character_.Reset();
                break;
            case kMovement:
                movement_.SetType(type);
                movement_.Reset();
                break;
            default:
                texture_.SetType(type);
                texture_.Reset();
                break;
        }
    }

    void Process(int slot, float* l, float* r, size_t n)
    {
        switch(slot)
        {
            case kCharacter: character_.ProcessBlock(l, r, n); break;
            case kMovement: movement_.ProcessBlock(l, r, n); break;
            default: texture_.ProcessBlock(l, r, n); break;
        }
    }

    // Character / Movement / Texture: crossfade the processed signal against
    // the dry by fade_, which ramps toward 0 while a type change is pending or
    // the slot is bypassed, and toward 1 otherwise.
    void RunSlot(int s, float* l, float* r, size_t n)
    {
        // Fully faded out with a type change pending: swap now, at silence.
        if(fade_[s] <= 0.f && pending_[s] >= 0)
        {
            ApplyType(s, pending_[s]);
            pending_[s] = -1;
        }
        const float target = (pending_[s] >= 0 || bypass_[s]) ? 0.f : 1.f;
        if(fade_[s] <= 0.f && target <= 0.f)
            return; // bypassed and silent: skip entirely

        if(fade_[s] >= 1.f && target >= 1.f)
        {
            Process(s, l, r, n); // fully in: no dry copy needed
            return;
        }

        for(size_t i = 0; i < n; i++)
        {
            dry_l_[i] = l[i];
            dry_r_[i] = r[i];
        }
        Process(s, l, r, n);
        float f = fade_[s];
        for(size_t i = 0; i < n; i++)
        {
            f += target > f ? fade_step_ : -fade_step_;
            f = Clamp(f, 0.f, 1.f);
            l[i] = dry_l_[i] + (l[i] - dry_l_[i]) * f;
            r[i] = dry_r_[i] + (r[i] - dry_r_[i]) * f;
        }
        fade_[s] = f;
    }

    void RunSpace(float* l, float* r, size_t n)
    {
        if(space_pending_ >= 0 && space_.WetIsSilent())
        {
            space_.SetType(space_pending_);
            space_.Reset();
            space_pending_ = -1;
            space_.SetWetHold(false);
        }
        space_.ProcessBlock(l, r, n);
    }

    static constexpr size_t kMaxBlock = 256;

    float        sample_rate_ = 48000.f;
    Character    character_;
    Movement     movement_;
    Texture      texture_;
    Space        space_;
    float        fade_[kSpace]    = {1.f, 1.f, 1.f};
    int          pending_[kSpace] = {-1, -1, -1};
    bool         bypass_[kSpace]  = {false, false, false};
    int          space_pending_   = -1;
    float        fade_step_       = 0.002f;
    float        dry_l_[kMaxBlock], dry_r_[kMaxBlock];
    OnePole      split_[2];
    Smoother     low_gain_, high_gain_, volume_;
    float        peak_ = 0.f;
};

} // namespace fx
} // namespace rodeo002
