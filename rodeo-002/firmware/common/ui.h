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
//   * MAIN'S HOLD MEANS SHIFT AND NOTHING ELSE. It used to also resolve, on
//     release, as a long-press that armed the gesture recorder -- told apart
//     from a shift only by whether anything else had been touched, which is an
//     invisible rule at the moment you need it (Derek, 2026-09-24: "that feels
//     confusing and easy to mess up"). Everything else now hangs off MAIN's
//     TAP, through a menu, so the only press rule left is: tap acts, hold
//     shifts.
//   * The MENU is modal, and the screen says so the whole time it is open.
//     Turn to choose, tap to enter; anything else touched closes it, as does
//     five seconds of nothing. Load and Save are separate entries, which is
//     what let the browser's own long-press-to-save go away.
//
// The Ui does not touch flash. Loading and saving are REQUESTS the app picks up
// with TakePresetRequest(), so all of this stays testable on the host.

#pragma once

#include <cstdint>

#include "cost_model.h"
#include "patch.h"
#include "gesture.h"
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
    kCutoff, kResonance, kFilterEnv, kMovementAmt,
    kAttack, kDecay, kSustain, kTextureAmt,
    kSpaceTime, kSpaceFeedback, kSpaceGrain, kSpaceMix,
    // shift layer, same pots
    kEnvToTimbre, kEnvToMorph, kLfoRate, kCharacterBias,
    kFilterType, kLfoDepth, kLfoDest, kMovementRate,
    kSpread, kGlide, kVolume, kTextureTone,
    kGrainPitch, kGrainSpread, kTilt, kSpaceDamp,
    kCount
};

inline const char* ParamName(ParamId p)
{
    static const char* kNames[] = {
        "Harmonics", "Timbre",    "Morph",     "Character", "Cutoff",    "Resonance",
        "Filt env",  "Movement",  "Attack",    "Decay",     "Sustain",   "Texture",
        "Time",      "Feedback",  "Grain",     "Space mix", "Env>Timbre", "Env>Morph",
        "LFO rate",  "Bias",      "Filter",    "LFO depth", "LFO dest",  "Rate",
        "Spread",    "Glide",     "Volume",    "Tone",      "Pitch",     "Spread",
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

/** Master volume is GLOBAL -- it is the output level, so it is never stored in
 *  a preset and a preset load must not move it.
 *
 *  It is not exempt from pickup, though it used to be. That was right while it
 *  sat on the main layer, where the knob's position was always the volume. On
 *  the shift layer the same knob's position belongs to Sustain, so taking it
 *  live the instant SHIFT went down jumped the output to wherever Sustain
 *  happened to be sitting (Derek, 2026-09-24). Now it waits to be caught like
 *  everything else, and the UP/DN hint says which way. */
inline bool ParamIsGlobal(ParamId p) { return p == ParamId::kVolume; }

struct UiInput
{
    float    pot[16]      = {};      // 0..1, panel order
    int16_t  enc_delta[5] = {};      // detents since the last update
    bool     enc_held[5]  = {};      // live switch level
    uint32_t now_ms       = 0;
};

/** Where the panel's attention is. Normal is playing; the rest are modal and
 *  are on screen for as long as they apply. */
enum class UiMode
{
    kNormal = 0,
    kMenu,
    kLoad,
    kSave,
    kPatchPage,
    kGlobalPage,
    kName,
    kResetPage
};

/** The menu, in the order MAIN turns through it. */
enum class MenuItem
{
    kLoad = 0,
    kSave,
    kGesture,
    kPatch,
    kGlobal,
    kFactoryReset,
    kClose,
    kCount
};

inline const char* MenuItemName(MenuItem m)
{
    switch(m)
    {
        case MenuItem::kLoad: return "Load preset";
        case MenuItem::kSave: return "Save preset";
        case MenuItem::kGesture: return "Gesture";
        case MenuItem::kPatch: return "Patch settings";
        case MenuItem::kGlobal: return "Global settings";
        case MenuItem::kFactoryReset: return "Factory reset";
        default: return "Close";
    }
}

/** The Patch page: the set-once-per-patch values that have no pot of their own.
 *  They live in `Patch`, so they travel with a preset like everything else. */
enum class PatchSetting
{
    kKeytrack = 0,
    kVelToFilter,
    kBendRange,
    kLfoShape,
    kModDest,
    kCount
};

/** The Global page: never stored in a preset, by the same rule that keeps the
 *  MIDI channel and the output level out of one -- a preset that carried the
 *  MIDI channel could leave the synth deaf to the keyboard. */
enum class GlobalSetting
{
    kMidiChannel = 0,
    kMidiOutChannel,
    kCount
};

/** What the big readout is showing. A value is not always a number: a stepped
 *  pot is a choice and should say which one, and the envelope is a shape. */
enum class Readout
{
    kNone = 0,  // nothing recent: the app puts the scope here
    kBar,       // a continuous value
    kChoice,    // a stepped pot: name the step, do not draw a meter
    kEnvelope   // an ADSR segment: draw the envelope it belongs to
};

struct DisplayState
{
    const char* engine      = "";
    const char* voice_mode  = "";
    /** Three letters, for the header, where the preset's name has the room. */
    const char* voice_short = "";
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
    Readout     readout    = Readout::kNone;
    /** kChoice: which step the pot is on, by name. */
    const char* choice     = "";
    /** kEnvelope: the whole shape, and which segment is moving (0=A 1=D 2=S 3=R). */
    float       env[4]     = {0.f, 0.f, 0.f, 0.f};
    int         env_moving = -1;
    // --- the preset this patch came from ---------------------------------
    const char* preset_name  = "--";
    bool        preset_dirty = false;
    /** 0 when the pot is in control; -1/+1 means turn down/up to catch it. */
    int         pickup_hint = 0;
    /** A one-shot line for things the panel can't show otherwise. */
    const char* message    = "";
    uint32_t    message_ms = 0;
    /** A SECOND one-shot line, for something the instrument did on its own
     *  rather than something you did. It has its own slot because the two
     *  arrive together: turning onto a physical-model engine posts the engine's
     *  name AND the fact that the voice mode had to move, and with one slot the
     *  second overwrote the first -- so you could not see which engine you had
     *  turned onto (Derek, 2026-09-25). Drawn away from the message. */
    const char* notice     = "";
    uint32_t    notice_ms  = 0;
    /** What releasing MAIN would do right now, once a clean hold has passed the
     *  long-press threshold. Empty when a release would do nothing. */
    const char* pending    = "";
    // --- the modal layer -------------------------------------------------
    UiMode      mode         = UiMode::kNormal;
    const char* menu_item    = "";
    int         menu_index   = 0;
    int         browser_slot = 0;
    const char* browser_name = "";
    bool        browser_used = false;
    /** "", "ARM", "REC" or "LOOP" -- what the gesture recorder is doing. */
    const char* gesture      = "";
    float       gesture_pos  = 0.f;
    /** kResetPage: whether the answer has been turned to YES. */
    bool        reset_armed = false;
    /** kName: the name being spelled, and which character is being changed. */
    const char* name_text   = "";
    int         name_cursor = 0;
    // --- settings pages --------------------------------------------------
    const char* page_item    = "";
    const char* page_value   = "";
    int         page_index   = 0;
    int         page_count   = 0;
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
    /** For a save: what to call it. Points at the Ui's own buffer, which is
     *  valid until the panel is touched again -- read it when you take the
     *  request, which is the pass it was made on. */
    const char*  name   = nullptr;
};

/** The alphabet a name is spelled from. No lower case: at 6 pixels a character
 *  on a 128-pixel line, the difference between l and I is not worth the extra
 *  turning. */
inline const char* NameAlphabet()
{
    return " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+.";
}

class Ui
{
  public:
    static constexpr int      kNumPots      = 16;
    static constexpr int      kMainEncoder  = 4; // the isolated one, hub slot 5
    static constexpr uint32_t kLongPressMs  = 600;

    static constexpr uint32_t kPopupMs      = 1200;
    static constexpr float    kPickupEps    = 0.01f;
    /** Below this a pot counts as still. Measured against the value the pot
     *  last WROTE, not the previous sample, which is the difference between
     *  ignoring dither and accumulating it: a pot resting within pickup range
     *  of its stored value would otherwise catch, then write its own noise into
     *  the patch for ever -- enough to mark a preset modified with nobody
     *  touching the panel. A slow turn still lands, because the reference only
     *  moves when a write happens. */
    static constexpr float    kMoveEps      = 0.006f;
    /** A pot at rest must stay past kMoveEps for this long before it counts as
     *  moved. The ADCs deliver a channel ~108 times a second, so a single bad
     *  reading persists across many passes of this loop and no amount of
     *  counting passes would catch it -- it takes time. Two conversions' worth
     *  is enough to tell a glitch from a hand, and 20 ms before a knob responds
     *  is not something a hand notices (Derek, 2026-09-24: "little ghost UI
     *  parameter changes"). */
    static constexpr uint32_t kMoveConfirmMs = 20;
    /** Once a pot IS moving it keeps responding immediately for this long, so a
     *  turn is not re-confirmed step by step and does not feel stepped. */
    static constexpr uint32_t kMovingMs = 250;
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

    /** Remember what was loaded, so the screen can name it and say whether the
     *  patch has drifted from it. The app calls this after a load AND after a
     *  save, since a save makes the patch the preset. */
    void NotePresetLoaded(const StoredPreset& s)
    {
        loaded_       = s;
        have_loaded_  = true;
    }

    /** True when the live patch no longer matches what was loaded. Compared
     *  through the stored form, so it asks exactly the question a save would
     *  answer -- no field can drift out of the comparison without also drifting
     *  out of the file format. */
    bool PresetDirty() const
    {
        if(!have_loaded_)
            return false;
        StoredPreset now;
        PatchToPreset(patch_, loaded_.name, now);
        now.used            = loaded_.used;
        const uint8_t* a    = reinterpret_cast<const uint8_t*>(&now);
        const uint8_t* b    = reinterpret_cast<const uint8_t*>(&loaded_);
        for(size_t i = 0; i < sizeof(StoredPreset); i++)
            if(a[i] != b[i])
                return true;
        return false;
    }
    const char* PresetName() const { return have_loaded_ ? loaded_.name : "--"; }

    /** The pending load/save, cleared by reading it. */
    PresetRequest TakePresetRequest()
    {
        const PresetRequest r = request_;
        request_              = PresetRequest{};
        return r;
    }
    UiMode Mode() const { return mode_; }
    /** True once, when a factory reset has been asked for and confirmed. */
    bool TakeFactoryReset()
    {
        const bool r   = factory_reset_;
        factory_reset_ = false;
        return r;
    }
    bool   BrowserOpen() const { return mode_ == UiMode::kLoad || mode_ == UiMode::kSave; }

    /** The recorder is owned by the app (its buffer is far too big for DTCM);
     *  the Ui drives it, because the Ui is what knows the parameters. */
    void SetGesture(GestureRecorder* g) { gesture_ = g; }

    /** A parameter's current value, by the same id the pots address it with.
     *  For the CC layer, which has to send what the panel just did. */
    float Value(ParamId p) const { return ReadParam(p); }

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
        if(mode_ != UiMode::kNormal && shift)
            mode_ms_ = in.now_ms;
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
                    CloseMode(in.now_ms);
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
                    CloseMode(in.now_ms);
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

        // MAIN's press resolves on release, and only a TAP does anything: a
        // hold is shift, always, in every mode. The "nothing else touched"
        // guard stays, but now it only suppresses an accidental menu rather
        // than deciding between two different actions.
        if(!shift && main_held_)
        {
            const uint32_t held = in.now_ms - press_start_ms_;
            if(!touched_while_held_ && held < kLongPressMs)
                Tap(in, held);
        }
        main_held_ = shift;

        // --- pots -------------------------------------------------------
        for(int i = 0; i < kNumPots; i++)
            discrete_changed |= UpdatePot(i, in.pot[i], in.now_ms);
        primed_ = true;

        // --- the gesture recorder ----------------------------------------
        // Driven from here because this is what knows the parameters, at a
        // fixed rate rather than the loop's: the main loop runs far faster than
        // the pots are scanned, and a gesture recorded at loop speed would play
        // back at whatever speed the loop happened to be running at.
        static_assert(GestureRecorder::kLanes == kNumPots * 2,
                      "a lane per parameter, both layers");
        if(gesture_ && in.now_ms - gesture_ms_ >= 1000u / GestureRecorder::kFrameRate)
        {
            gesture_ms_ = in.now_ms;
            float vals[GestureRecorder::kLanes];
            for(int i = 0; i < GestureRecorder::kLanes; i++)
                vals[i] = ReadParam(static_cast<ParamId>(i));
            gesture_->Tick(vals, moved_lane_);
            if(gesture_->state() == GestureRecorder::State::kPlaying)
                for(int i = 0; i < GestureRecorder::kLanes; i++)
                    if(gesture_->in_loop(i) && !gesture_->grabbed(i))
                        WriteParam(static_cast<ParamId>(i), vals[i]);
            for(int i = 0; i < GestureRecorder::kLanes; i++)
                moved_lane_[i] = false;
        }

        // Expire the popup.
        if(display_.message_ms && in.now_ms - display_.message_ms > kPopupMs)
        {
            display_.message    = "";
            display_.message_ms = 0;
        }
        if(display_.notice_ms && in.now_ms - display_.notice_ms > kPopupMs)
        {
            display_.notice    = "";
            display_.notice_ms = 0;
        }
        if(touched_ms_ && in.now_ms - touched_ms_ > kPopupMs)
        {
            display_.show_value = false;
            display_.readout    = Readout::kNone;
        }

        // NO IDLE TIMEOUT. It closed the menu while Derek was still deciding
        // (2026-09-25), which is the one moment a menu has to stay put. Nothing
        // is lost by leaving it open: touching any other control still closes
        // it, so it can only persist while the panel is untouched, and the menu
        // carries a Close entry for getting out without touching anything.

        // A clean hold past the threshold announces itself, so the pending
        // action is never invisible (control map). Without this, a press that
        // had been disqualified looked exactly like one that had not.
        // Nothing resolves on a long press any more, so there is nothing
        // pending to announce: holding MAIN is shift and only shift.
        display_.pending = "";

        display_.shift        = shift_;
        display_.freeze       = patch_.freeze;
        display_.mode         = mode_;
        display_.menu_index   = menu_index_;
        display_.menu_item    = MenuItemName(static_cast<MenuItem>(menu_index_));
        display_.gesture      = GestureLabel();
        display_.gesture_pos  = gesture_ ? gesture_->position() : 0.f;
        display_.browser_slot = browser_slot_;
        display_.reset_armed  = reset_armed_;
        display_.name_text    = name_buf_;
        display_.name_cursor  = name_cursor_;
        if(OnPage())
        {
            FormatPageValue(page_index_);
            display_.page_item  = PageItemName(page_index_);
            display_.page_value = page_value_;
            display_.page_index = page_index_;
            display_.page_count = PageCount();
        }
        display_.browser_name = bank_ ? bank_->slot[browser_slot_].name : "";
        display_.browser_used = bank_ ? bank_->slot[browser_slot_].used != 0 : false;
        display_.preset_name  = PresetName();
        display_.preset_dirty = PresetDirty();
        return discrete_changed;
    }

    /** Fills in the parts of the display only the app knows. */
    void Refresh(const VoiceManager& voices)
    {
        display_.engine        = EngineName(patch_.engine);
        display_.voice_mode    = VoiceModeName(patch_.voice_mode);
        display_.voice_short   = VoiceModeShort(patch_.voice_mode);
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

    /** A tap on MAIN. What it does is whatever the screen is showing. */
    void Tap(const UiInput& in, uint32_t held)
    {
        // While a take is recording, a tap closes the loop and nothing else --
        // that is the one timing-critical action the instrument has. The end is
        // trimmed back to the moment the press went DOWN, so a loop does not
        // end late by however long the finger stayed there.
        if(mode_ == UiMode::kNormal && gesture_
           && gesture_->state() == GestureRecorder::State::kRecording)
        {
            gesture_->Close(held * GestureRecorder::kFrameRate / 1000u);
            Post("loop closed", in.now_ms);
            return;
        }

        switch(mode_)
        {
            case UiMode::kNormal: EnterMode(UiMode::kMenu, in); break;

            case UiMode::kMenu:
                switch(static_cast<MenuItem>(menu_index_))
                {
                    case MenuItem::kLoad: EnterMode(UiMode::kLoad, in); break;
                    case MenuItem::kSave: EnterMode(UiMode::kSave, in); break;
                    case MenuItem::kPatch:
                        page_index_ = 0;
                        EnterMode(UiMode::kPatchPage, in);
                        break;
                    case MenuItem::kGlobal:
                        page_index_ = 0;
                        EnterMode(UiMode::kGlobalPage, in);
                        break;
                    case MenuItem::kFactoryReset:
                        reset_armed_ = false; // always starts on NO
                        EnterMode(UiMode::kResetPage, in);
                        break;
                    case MenuItem::kClose: EnterMode(UiMode::kNormal, in); break;
                    default:
                        if(gesture_)
                        {
                            gesture_->Advance();
                            Post(GestureLabel(), in.now_ms);
                        }
                        EnterMode(UiMode::kNormal, in);
                        break;
                }
                break;

            case UiMode::kLoad:
                request_.action = PresetAction::kLoad;
                request_.slot   = browser_slot_;
                request_.name   = nullptr;
                EnterMode(UiMode::kNormal, in);
                break;

            // Saving names the slot first. A preset you cannot tell apart from
            // the next one is only half saved.
            case UiMode::kSave:
                StartNaming();
                EnterMode(UiMode::kName, in);
                break;

            case UiMode::kName:
                TrimName();
                request_.action = PresetAction::kSave;
                request_.slot   = browser_slot_;
                request_.name   = name_buf_;
                EnterMode(UiMode::kNormal, in);
                break;

            // The one page where a tap DOES something, because the change has
            // deliberately not happened yet. It fires only if the answer has
            // been turned to YES; a tap on NO is the way out.
            case UiMode::kResetPage:
                if(reset_armed_)
                    factory_reset_ = true;
                reset_armed_ = false;
                EnterMode(UiMode::kNormal, in);
                break;

            // Every other page has nothing to confirm -- the change has already
            // happened -- so a tap is simply the way out.
            default: EnterMode(UiMode::kNormal, in); break;
        }
    }

    static constexpr int kNameLast = StoredPreset::kNameLen - 2;

    /** Start from what the slot is called, so renaming is an edit rather than
     *  spelling it out again. An empty slot starts from its number. */
    void StartNaming()
    {
        const char* from = nullptr;
        if(bank_ && bank_->slot[browser_slot_].used)
            from = bank_->slot[browser_slot_].name;
        int i = 0;
        if(from)
            for(; i <= kNameLast && from[i]; i++)
                name_buf_[i] = Upper(from[i]);
        else
        {
            char* d = name_buf_;
            Copy(d, "USER ");
            Int(d, browser_slot_ + 1);
            i = static_cast<int>(d - name_buf_);
        }
        for(; i <= kNameLast; i++)
            name_buf_[i] = ' ';
        name_buf_[kNameLast + 1] = '\0';
        name_cursor_             = 0;
    }

    /** Trailing spaces are not part of a name. */
    void TrimName()
    {
        for(int i = kNameLast; i >= 0 && name_buf_[i] == ' '; i--)
            name_buf_[i] = '\0';
        if(name_buf_[0] == '\0')
        {
            char* d = name_buf_;
            Copy(d, "USER ");
            Int(d, browser_slot_ + 1);
        }
    }

    static char Upper(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

    bool OnPage() const
    {
        return mode_ == UiMode::kPatchPage || mode_ == UiMode::kGlobalPage;
    }
    int PageCount() const
    {
        return mode_ == UiMode::kPatchPage ? static_cast<int>(PatchSetting::kCount)
                                           : static_cast<int>(GlobalSetting::kCount);
    }

    const char* PageItemName(int i) const
    {
        if(mode_ == UiMode::kGlobalPage)
            return static_cast<GlobalSetting>(i) == GlobalSetting::kMidiChannel
                       ? "MIDI in ch"
                       : "MIDI out ch";
        switch(static_cast<PatchSetting>(i))
        {
            case PatchSetting::kKeytrack: return "Key track";
            case PatchSetting::kVelToFilter: return "Vel>filter";
            case PatchSetting::kBendRange: return "Bend range";
            case PatchSetting::kLfoShape: return "LFO shape";
            default: return "Mod wheel";
        }
    }

    /** Fills page_value_. Formatted by hand: these are the only numbers the
     *  panel ever spells out, and pulling in a formatter for four of them is
     *  not a trade worth making on a 128 KB budget. */
    void FormatPageValue(int i)
    {
        char* d = page_value_;
        if(mode_ == UiMode::kGlobalPage)
        {
            const bool in_ch = static_cast<GlobalSetting>(i) == GlobalSetting::kMidiChannel;
            const int  v     = in_ch ? globals_.midi_channel : globals_.midi_out_channel;
            if(in_ch && v == 0)
            {
                Copy(d, "Omni"); // in only: out has to pick a channel
                return;
            }
            Int(d, in_ch ? v : (v < 1 ? 1 : v));
            return;
        }
        switch(static_cast<PatchSetting>(i))
        {
            case PatchSetting::kKeytrack:
            {
                // Signed: 0.5 is none, either side tracks the keyboard.
                const int pct = static_cast<int>((patch_.keytrack - 0.5f) * 200.f
                                                 + (patch_.keytrack >= 0.5f ? 0.5f : -0.5f));
                if(pct > 0)
                    *d++ = '+';
                Int(d, pct);
                Copy(d, "%");
                break;
            }
            case PatchSetting::kVelToFilter:
                Int(d, static_cast<int>(patch_.vel_to_filter * 100.f + 0.5f));
                Copy(d, "%");
                break;
            case PatchSetting::kBendRange:
                Int(d, patch_.bend_range);
                Copy(d, patch_.bend_range == 1 ? " semitone" : " semitones");
                break;
            case PatchSetting::kLfoShape:
                Copy(d, fx::Lfo::ShapeName(patch_.lfo_shape));
                break;
            default: Copy(d, ModDestName(patch_.mod_dest)); break;
        }
    }

    void AdjustPage(int i, int dir)
    {
        if(mode_ == UiMode::kGlobalPage)
        {
            if(static_cast<GlobalSetting>(i) == GlobalSetting::kMidiChannel)
            {
                const int c           = globals_.midi_channel + dir;
                globals_.midi_channel = c < 0 ? 0 : (c > 16 ? 16 : c);
            }
            else
            {
                // Out has no Omni: something has to be on the wire.
                const int c               = globals_.midi_out_channel + dir;
                globals_.midi_out_channel = c < 1 ? 1 : (c > 16 ? 16 : c);
            }
            return;
        }
        switch(static_cast<PatchSetting>(i))
        {
            case PatchSetting::kKeytrack:
                patch_.keytrack = Clamp01(patch_.keytrack + 0.02f * dir);
                break;
            case PatchSetting::kVelToFilter:
                patch_.vel_to_filter = Clamp01(patch_.vel_to_filter + 0.02f * dir);
                break;
            case PatchSetting::kBendRange:
            {
                const int b       = patch_.bend_range + dir;
                patch_.bend_range = b < 0 ? 0 : (b > 24 ? 24 : b);
                break;
            }
            case PatchSetting::kLfoShape:
            {
                const int n      = fx::Lfo::kNumShapes;
                patch_.lfo_shape = ((patch_.lfo_shape + dir) % n + n) % n;
                break;
            }
            default:
            {
                const int n = static_cast<int>(ModDest::kCount);
                patch_.mod_dest
                    = static_cast<ModDest>(((static_cast<int>(patch_.mod_dest) + dir) % n + n) % n);
                break;
            }
        }
    }

    static float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
    static void  Copy(char*& d, const char* s)
    {
        while(*s)
            *d++ = *s++;
        *d = '\0';
    }
    static void Int(char*& d, int v)
    {
        if(v < 0)
        {
            *d++ = '-';
            v    = -v;
        }
        char tmp[8];
        int  n = 0;
        do
        {
            tmp[n++] = static_cast<char>('0' + v % 10);
            v /= 10;
        } while(v && n < 7);
        while(n)
            *d++ = tmp[--n];
        *d = '\0';
    }

    void EnterMode(UiMode m, const UiInput& in)
    {
        mode_           = m;
        mode_ms_        = in.now_ms;
        mode_opened_ms_ = in.now_ms;
        ResetTurnWatch(in);
    }

    const char* GestureLabel() const
    {
        if(!gesture_)
            return "";
        switch(gesture_->state())
        {
            case GestureRecorder::State::kArmed: return "ARM";
            case GestureRecorder::State::kRecording: return "REC";
            case GestureRecorder::State::kPlaying: return "LOOP";
            default: return "";
        }
    }

    /** Anything that is not MAIN closes the modal layer, without acting. */
    void CloseMode(uint32_t now)
    {
        if(mode_ != UiMode::kNormal && now != mode_opened_ms_)
            mode_ = UiMode::kNormal;
    }

    /** The physical models cannot carry eight voices, so they are Mono or
     *  Unison only (concept doc, *Voice modes*). Turning onto one in Poly moves
     *  the mode rather than letting the engine sound broken, says so, and puts
     *  the mode back when a polyphonic engine is chosen again -- the engine
     *  order never changes and nothing is hidden, the instrument just says what
     *  it did. */
    static bool EngineIsMonophonic(EngineId e)
    {
        return e == EngineId::kString || e == EngineId::kKarplus;
    }

    void ConstrainVoiceMode(uint32_t now)
    {
        if(EngineIsMonophonic(patch_.engine))
        {
            if(patch_.voice_mode == VoiceMode::kPoly)
            {
                poly_before_       = patch_.voice_mode;
                had_poly_before_   = true;
                patch_.voice_mode  = VoiceMode::kMono;
                PostNotice("MONO: THIS ENGINE", now);
            }
        }
        else if(had_poly_before_)
        {
            had_poly_before_  = false;
            patch_.voice_mode = poly_before_;
            PostNotice("POLY RESTORED", now);
        }
    }

    void Post(const char* msg, uint32_t now)
    {
        display_.message    = msg;
        display_.message_ms = now ? now : 1;
    }

    /** For what the instrument decided, as opposed to what you asked for. */
    void PostNotice(const char* msg, uint32_t now)
    {
        display_.notice    = msg;
        display_.notice_ms = now ? now : 1;
    }

    bool TurnEncoder(int e, int16_t delta, bool shift, uint32_t now)
    {
        if(e == kMainEncoder)
        {
            if(mode_ == UiMode::kMenu && !shift)
            {
                const int n  = static_cast<int>(MenuItem::kCount);
                menu_index_  = (menu_index_ + (delta > 0 ? 1 : -1) + n) % n;
                mode_ms_     = now;
                return false;
            }
            if(mode_ == UiMode::kName)
            {
                // Turn spells, hold and turn moves along the name: the same
                // two axes a settings page uses.
                if(shift)
                {
                    touched_while_held_ = true;
                    const int c  = name_cursor_ + (delta > 0 ? 1 : -1);
                    name_cursor_ = c < 0 ? 0 : (c > kNameLast ? kNameLast : c);
                }
                else
                {
                    const char* a = NameAlphabet();
                    int         n = 0;
                    while(a[n])
                        n++;
                    int at = 0;
                    for(int i = 0; i < n; i++)
                        if(a[i] == name_buf_[name_cursor_])
                            at = i;
                    at                       = ((at + (delta > 0 ? 1 : -1)) % n + n) % n;
                    name_buf_[name_cursor_]  = a[at];
                }
                mode_ms_ = now;
                return false;
            }
            if(mode_ == UiMode::kResetPage)
            {
                // Turning is the whole confirmation: the answer has to be moved
                // off NO before a tap means anything, which is a deliberate act
                // rather than a tap that landed on the wrong entry. It cannot
                // be a hold -- a hold is shift.
                reset_armed_ = !reset_armed_;
                mode_ms_     = now;
                return false;
            }
            if(OnPage())
            {
                // Turn to choose the line, HOLD and turn to change it -- the
                // same push-turn that picks the voice mode in normal use, so
                // the page needs no edit mode of its own.
                if(shift)
                {
                    touched_while_held_ = true;
                    AdjustPage(page_index_, delta > 0 ? 1 : -1);
                }
                else
                {
                    const int n = PageCount();
                    page_index_ = (page_index_ + (delta > 0 ? 1 : -1) + n) % n;
                }
                mode_ms_ = now;
                return true;
            }
            if(BrowserOpen() && !shift)
            {
                // Browsing slots, not choosing an engine.
                const int n   = PresetBank::kNumSlots;
                browser_slot_ = (browser_slot_ + (delta > 0 ? 1 : -1) + n) % n;
                mode_ms_      = now;
                return false;
            }
            if(shift)
            {
                // Push-turn: voice mode. (Holding MAIN and turning MAIN.)
                touched_while_held_ = true;
                int m = static_cast<int>(patch_.voice_mode) + (delta > 0 ? 1 : -1);
                const int n = static_cast<int>(VoiceMode::kCount);
                patch_.voice_mode = static_cast<VoiceMode>(((m % n) + n) % n);
                if(EngineIsMonophonic(patch_.engine) && patch_.voice_mode == VoiceMode::kPoly)
                {
                    // Skip past Poly rather than offering it and ignoring it.
                    patch_.voice_mode = delta > 0 ? VoiceMode::kMono : VoiceMode::kUnison;
                    had_poly_before_  = false;
                }
                Post(VoiceModeName(patch_.voice_mode), now);
                return true;
            }
            int eng     = static_cast<int>(patch_.engine) + (delta > 0 ? 1 : -1);
            const int n = static_cast<int>(EngineId::kCount);
            patch_.engine = static_cast<EngineId>(((eng % n) + n) % n);
            Post(EngineName(patch_.engine), now);
            ConstrainVoiceMode(now);
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
        if(turned && mode_ != UiMode::kNormal && now != mode_opened_ms_)
            mode_ = UiMode::kNormal;

        if(!caught_[index])
        {
            const bool crossed
                = (last - stored) * (value - stored) <= 0.f && fabsf(value - last) > 1e-6f;
            if(crossed || fabsf(value - stored) < kPickupEps)
            {
                caught_[index] = true;
                // The parameter keeps the value it already had: catching a pot
                // must not, by itself, change the sound.
                written_[index] = stored;
            }
            else
            {
                last_pot_[pot] = value;
                if(moved) // show which way to turn to catch it
                    NoteTouch(p, stored, value < stored ? 1 : -1, now);
                return false;
            }
        }

        last_pot_[pot] = value;
        if(Confirmed(pot, fabsf(value - written_[index]) > kMoveEps, now))
        {
            written_[index] = value;
            // What the PLAYER moved, which is what arms a take, and what takes
            // a parameter back from a loop that is driving it.
            moved_lane_[index] = true;
            WriteParam(p, value);
            NoteTouch(p, value, 0, now);
        }
        return false;
    }

    /** Has this pot been away from its written value long enough to believe?
     *  A pot already in motion is believed at once; one at rest has to hold the
     *  deviation for kMoveConfirmMs, which is what a stray conversion cannot. */
    bool Confirmed(int pot, bool away, uint32_t now)
    {
        if(!away)
        {
            move_since_[pot] = 0;
            return false;
        }
        if(moving_until_[pot] && now < moving_until_[pot])
        {
            moving_until_[pot] = now + kMovingMs;
            return true;
        }
        if(move_since_[pot] == 0)
        {
            move_since_[pot] = now ? now : 1;
            return false;
        }
        if(now - move_since_[pot] < kMoveConfirmMs)
            return false;
        move_since_[pot]   = 0;
        moving_until_[pot] = now + kMovingMs;
        return true;
    }

    void NoteTouch(ParamId p, float value, int hint, uint32_t now)
    {
        display_.param       = ParamName(p);
        display_.value       = value;
        display_.pickup_hint = hint;
        display_.show_value  = true;
        display_.readout     = ReadoutFor(p);
        display_.choice      = ChoiceName(p);
        if(display_.readout == Readout::kEnvelope)
        {
            display_.env[0]     = patch_.attack;
            display_.env[1]     = patch_.decay;
            display_.env[2]     = patch_.sustain;
            display_.env[3]     = patch_.decay; // ADS: release follows decay
            display_.env_moving = EnvSegment(p);
        }
        else
            display_.env_moving = -1;
        touched_ms_ = now ? now : 1;
    }

    /** A stepped pot is a choice, not a quantity: showing it as a bar says
     *  nothing about which destination you landed on. An envelope segment is
     *  part of a shape, so the shape is what to draw. */
    static Readout ReadoutFor(ParamId p)
    {
        if(ParamSteps(p) > 0)
            return Readout::kChoice;
        return EnvSegment(p) >= 0 ? Readout::kEnvelope : Readout::kBar;
    }

    /** Which part of the envelope picture a pot is moving. There is no release
     *  segment of its own: this is an ADS envelope, Moog style, where Decay
     *  sets the release time as well (Derek, 2026-09-24). Moving Decay
     *  therefore moves two segments of the drawing, which is exactly what it
     *  does to the sound. */
    static int EnvSegment(ParamId p)
    {
        switch(p)
        {
            case ParamId::kAttack: return 0;
            case ParamId::kDecay: return 1;
            case ParamId::kSustain: return 2;
            default: return -1;
        }
    }

    const char* ChoiceName(ParamId p) const
    {
        switch(p)
        {
            case ParamId::kFilterType: return FilterTypeName(patch_.filter);
            case ParamId::kLfoDest: return LfoDestName(patch_.lfo_dest);
            default: return "";
        }
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
            case ParamId::kFilterEnv: return x.filter_env;
            case ParamId::kMovementAmt: return x.amount[fx::TextureChain::kMovement];
            case ParamId::kAttack: return x.attack;
            case ParamId::kDecay: return x.decay;
            case ParamId::kSustain: return x.sustain;
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
            case ParamId::kLfoDepth: return x.lfo_depth;
            case ParamId::kLfoDest:
                return StepToPot(static_cast<int>(x.lfo_dest), static_cast<int>(LfoDest::kCount));
            case ParamId::kMovementRate: return x.secondary[fx::TextureChain::kMovement];
            case ParamId::kSpread: return x.spread;
            case ParamId::kGlide: return x.glide;
            case ParamId::kVolume: return globals_.volume;
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
            // ADS, Moog style: one knob for the fall time, whether the note is
            // still held or not. The patch keeps both fields in step so that
            // what a preset stores is what the panel can express.
            case ParamId::kDecay:
                x.decay = v;
                x.release = v;
                break;
            case ParamId::kGlide: x.glide = v; break;
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
    /** The value each parameter last took from its pot: the reference the
     *  movement deadband is measured against. */
    float written_[kNumPots * 2] = {};
    StoredPreset loaded_ = {};
    bool         have_loaded_ = false;
    uint32_t move_since_[kNumPots] = {}, moving_until_[kNumPots] = {};
    float watch_lo_[kNumPots] = {}, watch_hi_[kNumPots] = {};
    const PresetBank* bank_ = nullptr;
    PresetRequest     request_;
    GestureRecorder* gesture_ = nullptr;
    uint32_t gesture_ms_ = 0;
    bool     moved_lane_[kNumPots * 2] = {};
    UiMode   mode_ = UiMode::kNormal;
    int      menu_index_ = 0;
    int      page_index_ = 0;
    bool     reset_armed_ = false;
    bool     factory_reset_ = false;
    VoiceMode poly_before_ = VoiceMode::kPoly;
    bool      had_poly_before_ = false;
    char     page_value_[20] = {};
    char     name_buf_[StoredPreset::kNameLen] = {};
    int      name_cursor_ = 0;
    int      browser_slot_ = 0;
    uint32_t mode_ms_ = 0, mode_opened_ms_ = 0;
    bool     shift_ = false;
    bool         main_held_            = false;
    bool         touched_while_held_   = false;
    bool         primed_               = false;
};

} // namespace rodeo002
