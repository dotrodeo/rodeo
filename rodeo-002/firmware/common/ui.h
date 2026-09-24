// rodeo-002 — the panel: what the pots and encoders do, and what the screen
// says about it. Pure logic, no hardware: the app feeds it pot readings and
// encoder deltas, and it writes the Patch and a DisplayState.
//
// The rules it implements come from the control map:
//   * SHIFT is MAIN's switch HELD. MAIN's press resolves ON RELEASE, and only
//     if nothing else was touched -- otherwise holding shift for 600 ms while
//     reaching for a pot would fire MAIN's long-press.
//   * PICKUP, on both layers: a pot is inert until it crosses the stored value,
//     so a shift release or a preset load never jumps 16 parameters. The screen
//     shows which way to turn to catch it.
//   * Two shift positions are ENUMS read as stepped pots: filter type and LFO
//     destination.
//   * Master volume is global, is never stored in a preset, and never waits for
//     pickup -- it is the physical output level.
//   * The PRESET BROWSER is modal, and is the instrument's one modal exception:
//     while it is open MAIN's turn browses slots, its tap loads and its
//     long-press saves. That is acceptable only because the browser is on
//     screen the whole time it applies. Anything else touched closes it without
//     loading, as does five seconds of nothing.
//
// The Ui does not touch flash. Loading and saving are REQUESTS the app picks up
// with TakePresetRequest(), so all of this stays testable on the host.

#pragma once

#include <cstdint>

#include "cost_model.h"
#include "patch.h"
#include "preset.h"
#include "texture_chain.h"
#include "voice.h"

namespace rodeo002
{
/** Every parameter a pot can address, in panel order: the main layer first,
 *  then the shift layer. */
enum class ParamId
{
    // main layer, pot 0..15
    kHarmonics = 0, kTimbre, kMorph, kCharacterAmt,
    kCutoff, kResonance, kSpread, kMovementAmt,
    kAttack, kRelease, kVolume, kTextureAmt,
    kSpaceTime, kSpaceFeedback, kSpaceGrain, kSpaceMix,
    // shift layer, same pots
    kEnvToTimbre, kEnvToMorph, kLfoRate, kCharacterBias,
    kFilterType, kFilterEnv, kLfoDest, kMovementRate,
    kDecay, kSustain, kLfoDepth, kTextureTone,
    kGrainPitch, kGrainSpread, kTilt, kSpaceDamp,
    kCount
};

inline const char* ParamName(ParamId p)
{
    static const char* kNames[] = {
        "Harmonics", "Timbre",    "Morph",     "Character", "Cutoff",    "Resonance",
        "Spread",    "Movement",  "Attack",    "Release",   "Volume",    "Texture",
        "Time",      "Feedback",  "Grain",     "Space mix", "Env>Timbre", "Env>Morph",
        "LFO rate",  "Bias",      "Filter",    "Filt env",  "LFO dest",  "Rate",
        "Decay",     "Sustain",   "LFO depth", "Tone",      "Pitch",     "Spread",
        "Tilt",      "Damp"};
    const int i = static_cast<int>(p);
    return (i >= 0 && i < static_cast<int>(ParamId::kCount)) ? kNames[i] : "?";
}

/** Enum parameters are read as stepped pots; 0 means continuous. */
inline int ParamSteps(ParamId p)
{
    if(p == ParamId::kFilterType)
        return static_cast<int>(FilterType::kCount);
    if(p == ParamId::kLfoDest)
        return static_cast<int>(LfoDest::kCount);
    return 0;
}

/** Master volume is global and always live -- never stored, never picked up. */
inline bool ParamIsGlobal(ParamId p) { return p == ParamId::kVolume; }

struct UiInput
{
    float    pot[16]      = {};      // 0..1, panel order
    int16_t  enc_delta[5] = {};      // detents since the last update
    bool     enc_held[5]  = {};      // live switch level
    uint32_t now_ms       = 0;
};

struct DisplayState
{
    const char* engine     = "";
    const char* voice_mode = "";
    int         voices_active = 0;
    int         voices_max    = 0;
    const char* slot_type[fx::TextureChain::kNumSlots] = {"", "", "", ""};
    bool        slot_bypass[fx::TextureChain::kNumSlots] = {false, false, false, false};
    bool        freeze  = false;
    bool        shift   = false;
    // The parameter last touched, for the big readout.
    const char* param      = "";
    float       value      = 0.f;
    bool        show_value = false;
    /** 0 when the pot is in control; -1/+1 means turn down/up to catch it. */
    int         pickup_hint = 0;
    /** A one-shot line for things the panel can't show otherwise. */
    const char* message    = "";
    uint32_t    message_ms = 0;
    /** What releasing MAIN would do right now, once a clean hold has passed the
     *  long-press threshold. Empty when a release would do nothing. */
    const char* pending    = "";
    // --- preset browser, while it is open --------------------------------
    bool        browser_open = false;
    int         browser_slot = 0;
    const char* browser_name = "";
    bool        browser_used = false;
};

/** What the panel is asking the app to do with the preset store. */
enum class PresetAction
{
    kNone = 0,
    kLoad,
    kSave
};

struct PresetRequest
{
    PresetAction action = PresetAction::kNone;
    int          slot   = 0;
};

class Ui
{
  public:
    static constexpr int      kNumPots      = 16;
    static constexpr int      kMainEncoder  = 4; // the isolated one, hub slot 5
    static constexpr uint32_t kLongPressMs  = 600;
    /** The browser closes itself after this long untouched, so the instrument
     *  never sits in a mode nobody meant to be in. */
    static constexpr uint32_t kBrowserIdleMs = 5000;
    static constexpr uint32_t kPopupMs      = 1200;
    static constexpr float    kPickupEps    = 0.01f;
    /** Below this a pot counts as still, for the readout on screen. */
    static constexpr float    kMoveEps      = 0.002f;
    /** How far a pot must travel to count as DELIBERATELY turned: enough to
     *  disqualify a MAIN press as a shift, or to close the browser. Measured
     *  from where the pot was when the press began, so a slow turn still counts
     *  while dither never does -- 3% of travel is about 9 degrees, and on
     *  hardware 0.3% of dither was enough to swallow a save (2026-09-24). */
    static constexpr float    kTurnEps      = 0.03f;

    void Init(const Patch& p, const Globals& g)
    {
        patch_   = p;
        globals_ = g;
        // Nothing is caught until each pot is moved to where the patch says.
        DropPickup();
    }

    /** The bank, for slot names on screen. The app owns it; the Ui only reads
     *  it, and works (nameless) without one. */
    void SetBank(const PresetBank* b) { bank_ = b; }

    /** The pending load/save, cleared by reading it. */
    PresetRequest TakePresetRequest()
    {
        const PresetRequest r = request_;
        request_              = PresetRequest{};
        return r;
    }
    bool BrowserOpen() const { return browser_open_; }

    const Patch&   patch() const { return patch_; }
    const Globals& globals() const { return globals_; }
    Patch&         mutable_patch() { return patch_; }
    Globals&       mutable_globals() { return globals_; }

    /** Call after a preset load or any external patch change: every pot has to
     *  catch up before it takes control again. */
    void DropPickup()
    {
        for(int i = 0; i < kNumPots * 2; i++)
            caught_[i] = false;
        primed_ = false;
    }

    /** One pass, at control rate. Returns true if a discrete choice changed
     *  (engine, slot type, bypass, voice mode) so the caller can re-cap. */
    bool Update(const UiInput& in)
    {
        bool discrete_changed = false;

        // Shift is MAIN held. Note what was touched while it was down, so its
        // press can be told apart from a shift.
        const bool shift = in.enc_held[kMainEncoder];
        if(shift && !shift_)
        {
            press_start_ms_     = in.now_ms;
            touched_while_held_ = false;
            ResetTurnWatch(in); // dither from here on is not a turn
        }
        // A press IS activity: the browser must not time out from under a
        // finger that is holding MAIN down to save.
        if(browser_open_ && shift)
            browser_ms_ = in.now_ms;
        if(shift != shift_)
        {
            shift_ = shift;
            // The pots now address the other layer: they must be caught again.
            for(int i = 0; i < kNumPots * 2; i++)
                caught_[i] = false;
        }

        // --- encoders ---------------------------------------------------
        for(int e = 0; e < 5; e++)
        {
            const int16_t d = in.enc_delta[e];
            if(d != 0)
            {
                if(shift_ && e != kMainEncoder)
                    touched_while_held_ = true;
                if(e != kMainEncoder)
                    CloseBrowser(in.now_ms);
                discrete_changed |= TurnEncoder(e, d, shift_, in.now_ms);
            }
            // Slot encoders: tap = bypass, and SPACE's long-press = freeze.
            if(e != kMainEncoder)
            {
                if(in.enc_held[e] && !enc_held_[e])
                    enc_press_ms_[e] = in.now_ms;
                if(in.enc_held[e] && !enc_long_[e]
                   && in.now_ms - enc_press_ms_[e] >= kLongPressMs)
                {
                    enc_long_[e] = true;
                    if(e == fx::TextureChain::kSpace)
                    {
                        patch_.freeze = !patch_.freeze;
                        Post(patch_.freeze ? "FREEZE" : "unfreeze", in.now_ms);
                    }
                }
                if(in.enc_held[e] && !enc_held_[e])
                    CloseBrowser(in.now_ms);
                if(!in.enc_held[e] && enc_held_[e])
                {
                    if(!enc_long_[e])
                    {
                        patch_.bypass[e] = !patch_.bypass[e];
                        discrete_changed = true;
                        Post(patch_.bypass[e] ? "bypassed" : "on", in.now_ms);
                    }
                    enc_long_[e] = false;
                }
            }
            enc_held_[e] = in.enc_held[e];
        }

        // MAIN's press resolves on release, and only if nothing else moved.
        if(!shift && main_held_)
        {
            const uint32_t held = in.now_ms - press_start_ms_;
            const bool     long_press = held >= kLongPressMs;
            if(!touched_while_held_)
            {
                if(browser_open_)
                {
                    // The one modal exception: in here, long-press is SAVE.
                    request_.action = long_press ? PresetAction::kSave : PresetAction::kLoad;
                    request_.slot   = browser_slot_;
                    browser_open_   = false;
                }
                else if(long_press)
                    Post("Gesture: not yet", in.now_ms);
                else
                {
                    browser_open_      = true;
                    browser_ms_        = in.now_ms;
                    browser_opened_ms_ = in.now_ms;
                    ResetTurnWatch(in);
                }
            }
        }
        main_held_ = shift;

        // --- pots -------------------------------------------------------
        for(int i = 0; i < kNumPots; i++)
            discrete_changed |= UpdatePot(i, in.pot[i], in.now_ms);
        primed_ = true;

        // Expire the popup.
        if(display_.message_ms && in.now_ms - display_.message_ms > kPopupMs)
        {
            display_.message    = "";
            display_.message_ms = 0;
        }
        if(touched_ms_ && in.now_ms - touched_ms_ > kPopupMs)
            display_.show_value = false;

        if(browser_open_ && in.now_ms - browser_ms_ > kBrowserIdleMs)
            browser_open_ = false;

        // A clean hold past the threshold announces itself, so the pending
        // action is never invisible (control map). Without this, a press that
        // had been disqualified looked exactly like one that had not.
        display_.pending = "";
        if(shift_ && !touched_while_held_ && in.now_ms - press_start_ms_ >= kLongPressMs)
            display_.pending = browser_open_ ? "SAVE" : "GESTURE";

        display_.shift        = shift_;
        display_.freeze       = patch_.freeze;
        display_.browser_open = browser_open_;
        display_.browser_slot = browser_slot_;
        display_.browser_name = bank_ ? bank_->slot[browser_slot_].name : "";
        display_.browser_used = bank_ ? bank_->slot[browser_slot_].used != 0 : false;
        return discrete_changed;
    }

    /** Fills in the parts of the display only the app knows. */
    void Refresh(const VoiceManager& voices)
    {
        display_.engine        = EngineName(patch_.engine);
        display_.voice_mode    = VoiceModeName(patch_.voice_mode);
        display_.voices_active = voices.ActiveVoices();
        display_.voices_max    = voices.GetMaxVoices();
        for(int s = 0; s < fx::TextureChain::kNumSlots; s++)
        {
            display_.slot_type[s]   = fx::TextureChain::TypeName(s, patch_.type[s]);
            display_.slot_bypass[s] = patch_.bypass[s];
        }
    }

    const DisplayState& display() const { return display_; }

  private:
    /** Which parameter a pot addresses on the current layer. */
    static ParamId PotParam(int pot, bool shift)
    {
        return static_cast<ParamId>(pot + (shift ? kNumPots : 0));
    }

    void ResetTurnWatch(const UiInput& in)
    {
        for(int i = 0; i < kNumPots; i++)
            watch_lo_[i] = watch_hi_[i] = in.pot[i];
    }

    void CloseBrowser(uint32_t now)
    {
        if(browser_open_ && now != browser_opened_ms_)
            browser_open_ = false;
    }

    void Post(const char* msg, uint32_t now)
    {
        display_.message    = msg;
        display_.message_ms = now ? now : 1;
    }

    bool TurnEncoder(int e, int16_t delta, bool shift, uint32_t now)
    {
        if(e == kMainEncoder)
        {
            if(browser_open_ && !shift)
            {
                // Browsing, not choosing an engine.
                const int n   = PresetBank::kNumSlots;
                browser_slot_ = (browser_slot_ + (delta > 0 ? 1 : -1) + n) % n;
                browser_ms_   = now;
                return false;
            }
            if(shift)
            {
                // Push-turn: voice mode. (Holding MAIN and turning MAIN.)
                touched_while_held_ = true;
                int m = static_cast<int>(patch_.voice_mode) + (delta > 0 ? 1 : -1);
                const int n = static_cast<int>(VoiceMode::kCount);
                patch_.voice_mode = static_cast<VoiceMode>(((m % n) + n) % n);
                Post(VoiceModeName(patch_.voice_mode), now);
                return true;
            }
            int eng     = static_cast<int>(patch_.engine) + (delta > 0 ? 1 : -1);
            const int n = static_cast<int>(EngineId::kCount);
            patch_.engine = static_cast<EngineId>(((eng % n) + n) % n);
            Post(EngineName(patch_.engine), now);
            return true;
        }
        if(shift)
        {
            // Shift + turn: the slot's discrete option.
            const int n = OptionCount(e);
            int       o = patch_.option[e] + (delta > 0 ? 1 : -1);
            patch_.option[e] = ((o % n) + n) % n;
            Post("option", now);
            return false;
        }
        const int n = fx::TextureChain::NumTypes(e);
        int       t = patch_.type[e] + (delta > 0 ? 1 : -1);
        patch_.type[e] = ((t % n) + n) % n;
        Post(fx::TextureChain::TypeName(e, patch_.type[e]), now);
        return true;
    }

    static int OptionCount(int slot)
    {
        switch(slot)
        {
            case fx::TextureChain::kCharacter: return fx::Character::kNumOptions;
            case fx::TextureChain::kMovement: return fx::Movement::kNumOptions;
            case fx::TextureChain::kTexture: return fx::Texture::kNumOptions;
            default: return fx::Space::kNumOptions;
        }
    }

    /** Pickup: a pot takes control once it reaches the stored value.
     *
     *  Only a pot that is actually MOVING touches the display or counts as
     *  "touched while shift was held" -- a pot resting at its stored value
     *  would otherwise claim the readout every pass and make every shift look
     *  like a deliberate press. */
    bool UpdatePot(int pot, float value, uint32_t now)
    {
        const ParamId p      = PotParam(pot, shift_);
        const int     index  = static_cast<int>(p);
        const float   stored = ReadParam(p);
        const float   last   = primed_ ? last_pot_[pot] : value;
        const bool    moved  = primed_ && fabsf(value - last) > kMoveEps;

        // Deliberate movement is measured from where the pot was when the
        // press began, not pass to pass: dither stays inside a narrow band for
        // ever, while even a slow turn widens it.
        if(value < watch_lo_[pot])
            watch_lo_[pot] = value;
        if(value > watch_hi_[pot])
            watch_hi_[pot] = value;
        const bool turned = (watch_hi_[pot] - watch_lo_[pot]) > kTurnEps;
        if(turned && shift_)
            touched_while_held_ = true;
        // A turned pot closes the browser -- but not in the same pass that
        // opened it, since the hand that released MAIN may still be on a knob.
        if(turned && browser_open_ && now != browser_opened_ms_)
            browser_open_ = false;

        if(ParamIsGlobal(p))
        {
            // Master volume never waits for pickup.
            WriteParam(p, value);
            last_pot_[pot] = value;
            if(moved)
                NoteTouch(p, value, 0, now);
            return false;
        }

        if(!caught_[index])
        {
            const bool crossed
                = (last - stored) * (value - stored) <= 0.f && fabsf(value - last) > 1e-6f;
            if(crossed || fabsf(value - stored) < kPickupEps)
                caught_[index] = true;
            else
            {
                last_pot_[pot] = value;
                if(moved) // show which way to turn to catch it
                    NoteTouch(p, stored, value < stored ? 1 : -1, now);
                return false;
            }
        }

        last_pot_[pot] = value;
        WriteParam(p, value);
        if(moved)
            NoteTouch(p, value, 0, now);
        return false;
    }

    void NoteTouch(ParamId p, float value, int hint, uint32_t now)
    {
        display_.param       = ParamName(p);
        display_.value       = value;
        display_.pickup_hint = hint;
        display_.show_value  = true;
        touched_ms_          = now ? now : 1;
    }

    float ReadParam(ParamId p) const
    {
        const Patch& x = patch_;
        switch(p)
        {
            case ParamId::kHarmonics: return x.harmonics;
            case ParamId::kTimbre: return x.timbre;
            case ParamId::kMorph: return x.morph;
            case ParamId::kCharacterAmt: return x.amount[fx::TextureChain::kCharacter];
            case ParamId::kCutoff: return x.cutoff;
            case ParamId::kResonance: return x.resonance;
            case ParamId::kSpread: return x.spread;
            case ParamId::kMovementAmt: return x.amount[fx::TextureChain::kMovement];
            case ParamId::kAttack: return x.attack;
            case ParamId::kRelease: return x.release;
            case ParamId::kVolume: return globals_.volume;
            case ParamId::kTextureAmt: return x.amount[fx::TextureChain::kTexture];
            case ParamId::kSpaceTime: return x.space_time;
            case ParamId::kSpaceFeedback: return x.space_feedback;
            case ParamId::kSpaceGrain: return x.space_grain;
            case ParamId::kSpaceMix: return x.amount[fx::TextureChain::kSpace];
            case ParamId::kEnvToTimbre: return x.env_to_timbre;
            case ParamId::kEnvToMorph: return x.env_to_morph;
            case ParamId::kLfoRate: return x.lfo_rate;
            case ParamId::kCharacterBias: return x.secondary[fx::TextureChain::kCharacter];
            case ParamId::kFilterType:
                return StepToPot(static_cast<int>(x.filter), static_cast<int>(FilterType::kCount));
            case ParamId::kFilterEnv: return x.filter_env;
            case ParamId::kLfoDest:
                return StepToPot(static_cast<int>(x.lfo_dest), static_cast<int>(LfoDest::kCount));
            case ParamId::kMovementRate: return x.secondary[fx::TextureChain::kMovement];
            case ParamId::kDecay: return x.decay;
            case ParamId::kSustain: return x.sustain;
            case ParamId::kLfoDepth: return x.lfo_depth;
            case ParamId::kTextureTone: return x.secondary[fx::TextureChain::kTexture];
            case ParamId::kGrainPitch: return x.grain_pitch;
            case ParamId::kGrainSpread: return x.grain_spread;
            case ParamId::kTilt: return x.tilt;
            default: return x.secondary[fx::TextureChain::kSpace];
        }
    }

    void WriteParam(ParamId p, float v)
    {
        Patch& x = patch_;
        switch(p)
        {
            case ParamId::kHarmonics: x.harmonics = v; break;
            case ParamId::kTimbre: x.timbre = v; break;
            case ParamId::kMorph: x.morph = v; break;
            case ParamId::kCharacterAmt: x.amount[fx::TextureChain::kCharacter] = v; break;
            case ParamId::kCutoff: x.cutoff = v; break;
            case ParamId::kResonance: x.resonance = v; break;
            case ParamId::kSpread: x.spread = v; break;
            case ParamId::kMovementAmt: x.amount[fx::TextureChain::kMovement] = v; break;
            case ParamId::kAttack: x.attack = v; break;
            case ParamId::kRelease: x.release = v; break;
            case ParamId::kVolume: globals_.volume = v; break;
            case ParamId::kTextureAmt: x.amount[fx::TextureChain::kTexture] = v; break;
            case ParamId::kSpaceTime: x.space_time = v; break;
            case ParamId::kSpaceFeedback: x.space_feedback = v; break;
            case ParamId::kSpaceGrain: x.space_grain = v; break;
            case ParamId::kSpaceMix: x.amount[fx::TextureChain::kSpace] = v; break;
            case ParamId::kEnvToTimbre: x.env_to_timbre = v; break;
            case ParamId::kEnvToMorph: x.env_to_morph = v; break;
            case ParamId::kLfoRate: x.lfo_rate = v; break;
            case ParamId::kCharacterBias: x.secondary[fx::TextureChain::kCharacter] = v; break;
            case ParamId::kFilterType:
                x.filter = static_cast<FilterType>(
                    PotToStep(v, static_cast<int>(FilterType::kCount)));
                break;
            case ParamId::kFilterEnv: x.filter_env = v; break;
            case ParamId::kLfoDest:
                x.lfo_dest
                    = static_cast<LfoDest>(PotToStep(v, static_cast<int>(LfoDest::kCount)));
                break;
            case ParamId::kMovementRate: x.secondary[fx::TextureChain::kMovement] = v; break;
            case ParamId::kDecay: x.decay = v; break;
            case ParamId::kSustain: x.sustain = v; break;
            case ParamId::kLfoDepth: x.lfo_depth = v; break;
            case ParamId::kTextureTone: x.secondary[fx::TextureChain::kTexture] = v; break;
            case ParamId::kGrainPitch: x.grain_pitch = v; break;
            case ParamId::kGrainSpread: x.grain_spread = v; break;
            case ParamId::kTilt: x.tilt = v; break;
            default: x.secondary[fx::TextureChain::kSpace] = v; break;
        }
    }

    static int PotToStep(float v, int n)
    {
        const int s = static_cast<int>(v * static_cast<float>(n));
        return s < 0 ? 0 : (s >= n ? n - 1 : s);
    }
    static float StepToPot(int s, int n)
    {
        return (static_cast<float>(s) + 0.5f) / static_cast<float>(n);
    }

    Patch        patch_;
    Globals      globals_;
    DisplayState display_;
    bool         caught_[kNumPots * 2] = {};
    float        last_pot_[kNumPots]   = {};
    bool         enc_held_[5]          = {};
    bool         enc_long_[5]          = {};
    uint32_t     enc_press_ms_[5]      = {};
    uint32_t     press_start_ms_       = 0;
    uint32_t     touched_ms_           = 0;
    float watch_lo_[kNumPots] = {}, watch_hi_[kNumPots] = {};
    const PresetBank* bank_ = nullptr;
    PresetRequest     request_;
    bool     browser_open_ = false;
    int      browser_slot_ = 0;
    uint32_t browser_ms_ = 0, browser_opened_ms_ = 0;
    bool     shift_ = false;
    bool         main_held_            = false;
    bool         touched_while_held_   = false;
    bool         primed_               = false;
};

} // namespace rodeo002
