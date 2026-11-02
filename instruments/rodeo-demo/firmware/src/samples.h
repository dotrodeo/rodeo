// The sample library: WAV files on the SD card, loaded into the SDRAM.
//
// The card is too slow to play voices from directly (about 1.2 MB/s in the
// motherboard's 1-bit mode, a handful of stereo voices), so every sample is read
// into the Seed's 64 MB SDRAM once, at power-up, and voices play from there.
//
//   /samples/<name>.wav   a pitched sound: one sample across the keys, its own
//                         pitch at middle C (MIDI note 60)
//   /kits/<name>/*.wav    a kit: one sample per key, in name order from MIDI
//                         note 36 (C2) up, each at its own pitch
//
// WAVs may be 8-, 16- or 24-bit PCM or 32-bit float, mono or stereo (the first
// two channels of anything wider), at any rate: they are kept as 16-bit and
// played back at the right speed for 48 kHz, which converts the rate as the
// voice plays rather than at load.
//
// Loading happens a little at a time (Step(), from the main loop), so USB keeps
// being serviced while it runs. Each sound becomes playable as soon as all of it
// is in; the list of sounds is known straight after Mount().
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace samples
{
struct Sample
{
    const int16_t* data;     // interleaved when stereo
    uint32_t       frames;
    uint8_t        channels; // 1 or 2
    float          speed;    // file rate / 48 kHz: frames to step per output frame
};

struct Sound
{
    char     name[32];
    bool     kit;
    uint16_t first, count; // its samples: one for a pitched sound, one per key for a kit
    volatile bool ready;   // every sample loaded (or skipped as unreadable)
};

constexpr int kKitFirstNote = 36;
constexpr int kRootNote     = 60;

/** Mount the card and list the sounds on it. False if there's no card, or no
 *  FAT file system on it; the synth works without one. */
bool Mount();

/** Load the next piece. True while there is more to load. */
bool Step();

/** Let go of the card for disk mode: stop loading and unmount it. What has
 *  loaded stays in the SDRAM and keeps playing. */
void Unmount();

/** Back from disk mode: forget the library, mount the card again and list what
 *  is on it now; Step() loads it. Stop the synth's sample voices first
 *  (synth::DropSamples()): the SDRAM they play from is about to be rewritten. */
bool Remount();

/** The card's FatFS drive number, for disk mode's raw sector access. */
uint8_t Drive();

int          Sounds();
const Sound& GetSound(int i);

/** The sample a key plays in a sound, or null: a kit key with no sample, or a
 *  sound still loading. */
const Sample* ForNote(int sound, int note);

struct Stats
{
    int      sounds, samples, loaded, skipped;
    uint32_t bytes_read, bytes_used, load_ms;
    bool     card, loading;
};
Stats GetStats();
} // namespace samples
