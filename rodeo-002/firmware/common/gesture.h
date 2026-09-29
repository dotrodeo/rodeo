// rodeo-002 — the gesture recorder: pot movements, looped.
//
// The Chroma Console's idea, and the reason the panel has no spare controls
// left. It records what the POTS did -- both layers -- and plays it back around
// a loop, so a patch can move by itself while you play over it.
//
// Encoders are deliberately not recorded. They change effect types, engines and
// voice modes: looping discrete jumps like those reads as a malfunction rather
// than a gesture (control map, *Gesture recorder*).
//
// WHAT IT RECORDS IS PARAMETERS, NOT POT POSITIONS. A pot means a different
// parameter depending on whether SHIFT is held, and pickup means a pot's
// position and its parameter's value are often different things. Recording the
// 32 parameter values sidesteps both: playback writes values the panel could
// have written itself.
//
// ONLY LANES THAT MOVED ARE PLAYED BACK. A take that touched one knob owns that
// one knob, and leaves the other 31 free -- otherwise loading a preset under a
// running loop would be undone by the loop.
//
// GRABBING A POT TAKES IT BACK. Move a pot that the loop is driving and the
// loop stops driving it for the rest of the playback, so you can play over a
// gesture without stopping it.
//
// The buffer is handed in (SDRAM on the Seed, ordinary memory in the tests):
// 32 lanes x 16 s at the control rate is ~100 KB, which DTCM does not have.

#pragma once

#include <cstddef>
#include <cstdint>

namespace rodeo002
{
class GestureRecorder
{
  public:
    /** 16 pots x 2 layers. */
    static constexpr int kLanes = 32;
    /** Frames a second. The pots are scanned ~108 times a second, so recording
     *  faster than this would only store the same reading twice. */
    static constexpr int kFrameRate = 100;
    /** 16 seconds, per the control map. */
    static constexpr size_t kMaxFrames = kFrameRate * 16;

    enum class State : uint8_t
    {
        kIdle = 0,
        kArmed,     // waiting for the first pot movement
        kRecording,
        kPlaying
    };

    /** `buf` must hold kLanes * max_frames uint16_t. */
    void Init(uint16_t* buf, size_t max_frames)
    {
        buf_        = buf;
        max_frames_ = max_frames < kMaxFrames ? max_frames : kMaxFrames;
        Clear();
    }

    /** Arm: recording starts on the first pot movement, so there is no rush
     *  between pressing and playing. */
    void Arm()
    {
        Clear();
        state_ = State::kArmed;
    }

    /** Close the loop and start playing it. `trim_frames` drops that many
     *  frames from the end: the press that closes a loop is timestamped when
     *  it goes DOWN, and the caller resolves it on release, so the gap between
     *  the two would otherwise land at the end of every loop. */
    void Close(size_t trim_frames = 0)
    {
        if(state_ != State::kRecording)
            return;
        length_ = frame_ > trim_frames ? frame_ - trim_frames : 0;
        if(length_ == 0)
        {
            Clear();
            return;
        }
        frame_ = 0;
        state_ = State::kPlaying;
        for(int i = 0; i < kLanes; i++)
            grabbed_[i] = false;
    }

    void Clear()
    {
        state_  = State::kIdle;
        frame_  = 0;
        length_ = 0;
        for(int i = 0; i < kLanes; i++)
        {
            in_loop_[i] = false;
            grabbed_[i] = false;
        }
    }

    /** What the one control does next, in this state: arm, close, or clear. */
    void Advance(size_t trim_frames = 0)
    {
        switch(state_)
        {
            case State::kIdle: Arm(); break;
            case State::kArmed: Clear(); break; // armed but never started: cancel
            case State::kRecording: Close(trim_frames); break;
            default: Clear(); break;
        }
    }

    /** One control frame. `values` holds the live value of all 32 parameters and
     *  is written in place during playback; `moved` says which the player just
     *  moved. Call at kFrameRate. */
    void Tick(float* values, const bool* moved)
    {
        switch(state_)
        {
            case State::kIdle: break;

            case State::kArmed:
                for(int i = 0; i < kLanes; i++)
                    if(moved[i])
                    {
                        state_ = State::kRecording;
                        frame_ = 0;
                        break;
                    }
                if(state_ != State::kRecording)
                    break;
                // fall through: the frame that started it is recorded too

            case State::kRecording:
                if(frame_ >= max_frames_)
                {
                    Close(); // 16 seconds is the limit; close rather than drop
                    break;
                }
                for(int i = 0; i < kLanes; i++)
                {
                    buf_[frame_ * kLanes + i] = Quantize(values[i]);
                    if(moved[i])
                        in_loop_[i] = true;
                }
                frame_++;
                break;

            case State::kPlaying:
                if(length_ == 0)
                    break;
                for(int i = 0; i < kLanes; i++)
                {
                    if(moved[i])
                        grabbed_[i] = true; // taken back for the rest of playback
                    if(in_loop_[i] && !grabbed_[i])
                        values[i] = Dequantize(buf_[frame_ * kLanes + i]);
                }
                frame_ = frame_ + 1 >= length_ ? 0 : frame_ + 1;
                break;
        }
    }

    // --- storing a take ---------------------------------------------------
    // The buffer is the app's, so the app writes it to flash and reads it back
    // itself; these say what is worth writing and how to pick a take up again.

    /** Which lanes the loop drives, as a bit per lane. */
    uint32_t LaneMask() const
    {
        uint32_t m = 0;
        for(int i = 0; i < kLanes; i++)
            if(in_loop_[i])
                m |= (1u << i);
        return m;
    }
    const uint16_t* Data() const { return buf_; }
    uint16_t*       MutableData() { return buf_; }

    /** Take up a loop whose frames are already in the buffer, and play it: a
     *  preset that was saved with a gesture starts moving when it loads. */
    void Adopt(size_t frames, uint32_t lane_mask)
    {
        if(frames == 0 || frames > max_frames_)
        {
            Clear();
            return;
        }
        length_ = frames;
        frame_  = 0;
        state_  = State::kPlaying;
        for(int i = 0; i < kLanes; i++)
        {
            in_loop_[i] = (lane_mask & (1u << i)) != 0;
            grabbed_[i] = false;
        }
    }

    State  state() const { return state_; }
    size_t length_frames() const { return length_; }
    /** True once this lane is part of the loop -- it moved during the take. */
    bool in_loop(int lane) const
    {
        return lane >= 0 && lane < kLanes && in_loop_[lane];
    }
    bool grabbed(int lane) const
    {
        return lane >= 0 && lane < kLanes && grabbed_[lane];
    }
    /** 0..1 through the loop, for the screen. */
    float position() const
    {
        return length_ ? static_cast<float>(frame_) / static_cast<float>(length_) : 0.f;
    }

  private:
    // 16 bits a lane: the pots deliver far less than that, and SDRAM is not
    // short, so nothing here can be blamed for a stepped-sounding playback.
    static uint16_t Quantize(float v)
    {
        const float c = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
        return static_cast<uint16_t>(c * 65535.f + 0.5f);
    }
    static float Dequantize(uint16_t v) { return static_cast<float>(v) / 65535.f; }

    uint16_t* buf_        = nullptr;
    size_t    max_frames_ = 0;
    size_t    frame_      = 0;
    size_t    length_     = 0;
    State     state_      = State::kIdle;
    bool      in_loop_[kLanes] = {};
    bool      grabbed_[kLanes] = {};
};

} // namespace rodeo002
