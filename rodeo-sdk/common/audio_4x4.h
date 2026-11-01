#pragma once
//
// Rev B's four audio ins and four outs: the Seed's own codec on SAI1 and the
// CS4272 on SAI2, both in one callback.
//
//   rodeo::InitAudio4x4(hw);          // after hw.Init()
//   rodeo::StartAudio4x4(hw, Callback);
//
//   void Callback(daisy::AudioHandle::InputBuffer in,
//                 daisy::AudioHandle::OutputBuffer out, size_t size)
//   {
//       // in[0..3] and out[0..3] are back-panel channels 1-4: 0/1 the Seed's
//       // codec, 2/3 the CS4272.
//   }
//
// This is Daisy Patch's dual-codec setup with Rev B's pins. SAI1 is rebuilt the
// way DaisySeed::Init configured it for the Seed it found (the directions differ
// on a Seed 1.1), so the Seed's codec keeps working whichever module is
// fitted. Both run at 48 kHz, 24-bit left-justified, which the CS4272 is
// strapped for, with the Seed as clock master.
//
// The CS4272 must stay in reset until SAI2's MCLK and LRCK are running, and
// they only start with the audio, so StartAudio4x4() releases its reset
// (board::pin::kCodec2Reset) after starting it, and StopAudio4x4() puts it
// back. Its first few milliseconds out of reset are silence while it
// calibrates.
//
// Outputs 3/4 (out[2], out[3]) are rounded to 16 bits after the callback
// returns: on Rev B1 the CS4272's DAC data leaks into its ADC data, much less
// so with the low byte zero. Swapping the callback with
// hw.ChangeAudioCallback() skips that, so stop and start again instead.
//
#include "daisy_seed.h"

namespace rodeo
{
/** Set up both codecs for four channels in and out. Call after hw.Init() and
 *  before StartAudio4x4(); holds the CS4272 in reset. */
void InitAudio4x4(daisy::DaisySeed& hw, size_t blocksize = 48);

/** Start the audio with a four-channel callback, then release the CS4272. */
void StartAudio4x4(daisy::DaisySeed& hw, daisy::AudioHandle::AudioCallback cb);

/** Hold the CS4272 in reset, then stop the audio. */
void StopAudio4x4(daisy::DaisySeed& hw);
} // namespace rodeo
