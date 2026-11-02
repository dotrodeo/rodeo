// The SD card's rules, as the firmware actually applies them.
//
// This file is a second copy of what `instruments/rodeo-demo/firmware/src/
// samples.cpp` and `presets.cpp` do, and those stay authoritative. Every rule
// below cites the line that enforces it, so a reviewer can check the pair
// rather than trust this one. Where the firmware is quietly lenient — clamping
// a number, skipping a file — this says so, because the point of the console is
// to tell you now instead of at power-up.
//
// If this drifts from the headers twice, generate it from them instead.

/** Sample pool: `kPoolSamples` 16-bit samples (samples.cpp:17). Frames times
 *  channels, summed over every WAV, must fit. */
export const POOL_SAMPLES = 24 * 1024 * 1024; // 48 MiB as int16
/** `kMaxSounds` — pitched samples and kits together (samples.cpp:12). */
export const MAX_SOUNDS = 64;
/** `kMaxSamples` — every WAV on the card (samples.cpp:13). */
export const MAX_FILES = 512;
/** `Sound::name[32]`, so 31 characters (samples.h:36). */
export const MAX_NAME = 31;
/** `List()` skips a filename of 64 or more characters (samples.cpp:101). */
export const MAX_FILENAME = 63;
/** `kMax` presets (presets.cpp:16). */
export const MAX_PRESETS = 64;
/** A preset is read up to `kFileSize`; the rest is ignored (presets.cpp:17). */
export const MAX_PRESET_BYTES = 2048;

/** A kit's files map to keys from here up, in name order (samples.h:41). */
export const KIT_FIRST_NOTE = 36; // C2
/** A pitched sample plays at its own pitch here (samples.h:42). */
export const ROOT_NOTE = 60; // C4
/** Beyond this a kit's files have no key: `ForNote` indexes note - 36. */
export const MAX_KIT_FILES = 128 - KIT_FIRST_NOTE;

/** Everything is resampled to this by playback speed, not at load (samples.h:14). */
export const RATE = 48000;
/** Measured on a 2 GB card, bring-up test 07 (planning/REV_B_PLAN.md:321). */
export const CARD_READ_BYTES_PER_S = 1.24e6;

/** `IsWav`: ends .wav, case-insensitively, and is not a dotfile (samples.cpp:58). */
export function isWav(name) {
  return name.length > 4 && name.slice(-4).toLowerCase() === '.wav' && !name.startsWith('.');
}

/** `IsTxt`, the same shape for presets (presets.cpp:181). */
export function isTxt(name) {
  return name.length > 4 && name.slice(-4).toLowerCase() === '.txt' && !name.startsWith('.');
}

/**
 * The firmware sorts with `strcasecmp` and an insertion sort (samples.cpp:74).
 * That is byte-by-byte on the lowercased bytes — NOT `localeCompare`, which
 * would put "a" before "B" by locale rules and also ignore punctuation. Kit
 * files map to keys in this order, so getting it wrong moves someone's snare.
 */
export function strcasecmp(a, b) {
  const x = a.toLowerCase();
  const y = b.toLowerCase();
  const n = Math.min(x.length, y.length);
  for (let i = 0; i < n; i++) {
    const d = x.charCodeAt(i) - y.charCodeAt(i);
    if (d !== 0) return d < 0 ? -1 : 1;
  }
  return x.length === y.length ? 0 : x.length < y.length ? -1 : 1;
}

/** Sort a copy the way the firmware will. */
export function cardOrder(names) {
  return [...names].sort(strcasecmp);
}

/** `CopyName`: drop a trailing .wav, then keep 31 characters (samples.cpp:66). */
export function soundName(filename) {
  const dot = filename.lastIndexOf('.');
  const stem =
    dot > 0 && filename.slice(dot).toLowerCase() === '.wav' ? filename.slice(0, dot) : filename;
  return stem.slice(0, MAX_NAME);
}

/**
 * What the firmware's WAV reader accepts (samples.cpp:193-218):
 * PCM (format 1) at 8, 16 or 24 bits, or float (format 3) at 32, with a block
 * align that matches. Note what is NOT accepted: WAVE_FORMAT_EXTENSIBLE
 * (0xFFFE), which plenty of encoders emit for 24-bit or multichannel files and
 * which the fmt check rejects outright. The console always writes format 1.
 */
export function acceptsWavFormat({ format, bits, channels, rate, blockAlign }) {
  const pcm = format === 1 && (bits === 8 || bits === 16 || bits === 24);
  const flt = format === 3 && bits === 32;
  if (!(pcm || flt)) return false;
  if (!channels || !rate) return false;
  return blockAlign === (channels * bits) / 8;
}

/** Channels as loaded: anything wider than stereo keeps its first two (samples.cpp:239). */
export function loadedChannels(channels) {
  return channels >= 2 ? 2 : 1;
}

/** int16 slots a WAV will occupy in the pool. */
export function poolCost(frames, channels) {
  return frames * loadedChannels(channels);
}

/** Playback speed the firmware uses for a file at `rate` (samples.h:31). */
export function playbackSpeed(rate) {
  return rate / RATE;
}

/** MIDI note number to a name, for the kit keyboard. C4 = 60. */
const PITCHES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
export function noteName(note) {
  return `${PITCHES[note % 12]}${Math.floor(note / 12) - 1}`;
}

/**
 * A filename FAT will take and the firmware will not skip. The firmware never
 * sanitises, it just drops what it cannot use, so the console does the renaming
 * and says it did.
 */
export function safeFilename(name) {
  const cleaned = name
    .replace(/[\\/:*?"<>|\x00-\x1f]/g, '-') // illegal on FAT
    .replace(/^\.+/, '') // a dotfile is skipped
    .trim();
  return cleaned.slice(0, MAX_FILENAME) || 'untitled';
}
