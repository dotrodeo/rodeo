// node --test tools/rodeo-console/
//
// These test that the console agrees with the firmware, not that the console
// agrees with itself. Every case below was read off
// instruments/rodeo-demo/firmware/src/{samples,presets}.cpp; when one fails
// after a firmware change, the firmware is right.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  KIT_FIRST_NOTE,
  MAX_FILENAME,
  MAX_FILES,
  MAX_NAME,
  MAX_PRESETS,
  MAX_SOUNDS,
  POOL_SAMPLES,
  ROOT_NOTE,
  acceptsWavFormat,
  cardOrder,
  isWav,
  loadedChannels,
  noteName,
  playbackSpeed,
  poolCost,
  safeFilename,
  soundName,
  strcasecmp,
} from './js/card.js';
import { KEYS, formatPreset, parsePreset, squash } from './js/preset.js';
import { buildLibrary, poolStats, validate } from './js/model.js';

const wav = (name, over = {}) => ({
  name,
  frames: 48000,
  channels: 1,
  rate: 48000,
  bytes: 96044,
  accepted: true,
  ...over,
});

// --------------------------------------------------------------- card rules

test('isWav matches the firmware, dotfiles and all', () => {
  assert.equal(isWav('kick.wav'), true);
  assert.equal(isWav('KICK.WAV'), true);
  assert.equal(isWav('kick.Wav'), true);
  assert.equal(isWav('.wav'), false, 'length must exceed 4');
  assert.equal(isWav('._kick.wav'), false, 'macOS resource forks are dotfiles');
  assert.equal(isWav('kick.wave'), false);
  assert.equal(isWav('kick'), false);
});

test('sorting is strcasecmp, not localeCompare', () => {
  assert.deepEqual(cardOrder(['b.wav', 'A.wav']), ['A.wav', 'b.wav']);
  assert.deepEqual(cardOrder(['10.wav', '2.wav']), ['10.wav', '2.wav'], 'byte order, not numeric');
  assert.ok(strcasecmp('_x', 'ax') < 0, '_ (0x5f) sorts before a (0x61) once lowercased');
  assert.ok(strcasecmp('9x', '_x') < 0, 'digits (0x39) before _');
  assert.ok(strcasecmp('kick', 'kick2') < 0, 'prefix sorts first');

  // The case that actually separates the two orderings. localeCompare sorts
  // "e-acute" as if it were "e", so it lands before "z"; a byte comparison puts
  // it after, its code unit being 0xe9. Swapping cardOrder for localeCompare
  // fails only this line -- every ASCII case above agrees under both, which is
  // why the first version of this test caught nothing.
  assert.deepEqual(cardOrder(['\u00e9.wav', 'z.wav']), ['z.wav', '\u00e9.wav']);
});

test('two files differing only in case have no defined order', () => {
  // strcasecmp returns 0, so the insertion sort leaves them however f_readdir
  // gave them -- the directory's order, not one we can predict. Which key each
  // lands on is luck, so say so rather than quietly pick.
  assert.equal(strcasecmp('a', 'A'), 0);
  const kits = new Map([['dk', [wav('Kick.wav'), wav('kick.wav')]]]);
  const issues = validate(buildLibrary({ samples: [], kits }), []);
  assert.ok(
    issues.some((i) => /only in case/.test(i.text)),
    'a case-only clash should be called out, not silently ordered'
  );
});

test('kit keys follow that order from C2', () => {
  const kits = new Map([['909', [wav('03_hat.wav'), wav('01_kick.wav'), wav('02_snare.wav')]]]);
  const lib = buildLibrary({ samples: [], kits });
  const notes = lib.sounds[0].files.map((f) => [f.name, noteName(f.note)]);
  assert.deepEqual(notes, [
    ['01_kick.wav', 'C2'],
    ['02_snare.wav', 'C#2'],
    ['03_hat.wav', 'D2'],
  ]);
});

test('soundName drops .wav and cuts at 31 characters', () => {
  assert.equal(soundName('bass.wav'), 'bass');
  assert.equal(soundName('bass.WAV'), 'bass');
  assert.equal(soundName('a'.repeat(40) + '.wav'), 'a'.repeat(31));
  assert.equal(soundName('no-extension'), 'no-extension');
});

test('the WAV formats the firmware rejects', () => {
  const ok = { format: 1, bits: 16, channels: 2, rate: 44100, blockAlign: 4 };
  assert.equal(acceptsWavFormat(ok), true);
  assert.equal(acceptsWavFormat({ ...ok, format: 3, bits: 32, blockAlign: 8 }), true, 'float32');
  assert.equal(
    acceptsWavFormat({ ...ok, format: 0xfffe }),
    false,
    'WAVE_FORMAT_EXTENSIBLE is rejected outright — this is the common trap'
  );
  assert.equal(acceptsWavFormat({ ...ok, bits: 12 }), false);
  assert.equal(acceptsWavFormat({ ...ok, format: 3, bits: 64, blockAlign: 16 }), false);
  assert.equal(acceptsWavFormat({ ...ok, blockAlign: 3 }), false, 'align must match');
  assert.equal(acceptsWavFormat({ ...ok, rate: 0 }), false);
});

test('pool cost counts loaded channels, not the file�s', () => {
  assert.equal(loadedChannels(1), 1);
  assert.equal(loadedChannels(2), 2);
  assert.equal(loadedChannels(6), 2, 'the first two of anything wider');
  assert.equal(poolCost(1000, 6), 2000);
});

test('playback speed converts the rate as the voice plays', () => {
  assert.equal(playbackSpeed(48000), 1);
  assert.equal(playbackSpeed(96000), 2);
  assert.ok(Math.abs(playbackSpeed(44100) - 0.91875) < 1e-9);
});

test('safeFilename keeps FAT and the firmware happy', () => {
  assert.equal(safeFilename('a/b:c*.wav'), 'a-b-c-.wav');
  assert.equal(safeFilename('...hidden.wav'), 'hidden.wav', 'a dotfile would be skipped');
  assert.equal(safeFilename('  '), 'untitled');
  assert.equal(safeFilename('x'.repeat(80)).length, 63);
});

// ------------------------------------------------------------------ presets

test('squash lowercases and drops every space', () => {
  assert.equal(squash('Saw + Sub'), 'saw+sub');
  assert.equal(squash('  BOTH '), 'both');
  assert.equal(squash('S A W'), 'saw', 'spaces anywhere, not just the ends');
});

test('shape values match however they are spaced', () => {
  for (const written of ['saw + sub', 'Saw+Sub', 'SAW + SUB', 's a w + s u b']) {
    const { values, problems } = parsePreset(`shape = ${written}`);
    assert.deepEqual(problems, [], written);
    assert.equal(values.shape, 'saw + sub');
  }
});

test('a # anywhere truncates the line, before the = is found', () => {
  const { values, problems } = parsePreset('sound = kick#2');
  assert.equal(values.sound, 'kick', 'the rest of the name is eaten as a comment');
  assert.deepEqual(problems, []);
  const bare = parsePreset('# layer = both');
  assert.deepEqual(bare.values, {}, 'a whole-line comment sets nothing');
  const semi = parsePreset('layer = both ; the default');
  assert.equal(semi.values.layer, 'both');
});

test('numbers are clamped, not rejected — and we warn where the firmware is silent', () => {
  const { values, problems, warnings } = parsePreset('cutoff = 900');
  assert.equal(values.cutoff, 100, 'presets.cpp clamps in Number()');
  assert.deepEqual(problems, [], 'and it is NOT an error');
  assert.equal(warnings.length, 1);
  assert.match(warnings[0], /outside 0\.\.100/);
});

test('attack and release have floors of 1 ms and 5 ms', () => {
  assert.equal(parsePreset('attack = 0').values.attack, 1);
  assert.equal(parsePreset('release = 0').values.release, 5);
  assert.equal(parsePreset('release = 99999').values.release, 4000);
});

test('a line the Rodeo skips is reported as a problem', () => {
  const { problems } = parsePreset(['layer', 'wobble = 3', 'shape = sine', 'cutoff = banana'].join('\n'));
  assert.equal(problems.length, 4);
  assert.match(problems[0].why, /no "="/);
  assert.match(problems[1].why, /not a key/);
  assert.match(problems[2].why, /not one of/);
  assert.match(problems[3].why, /not a number/);
  assert.deepEqual(problems.map((p) => p.line), [1, 2, 3, 4]);
});

test('sound keeps its case and spaces; everything else does not', () => {
  const { values } = parsePreset('sound = My Big Pad\nlayer = BOTH');
  assert.equal(values.sound, 'My Big Pad');
  assert.equal(values.layer, 'both');
});

test('what we write, we can read back', () => {
  const written = formatPreset({ layer: 'both', shape: 'saw + sub', sound: 'bass', cutoff: 42 });
  const { values, problems, warnings } = parsePreset(written);
  assert.deepEqual(problems, []);
  assert.deepEqual(warnings, []);
  assert.deepEqual(values, { layer: 'both', shape: 'saw + sub', sound: 'bass', cutoff: 42 });
});

test('every key in KEYS round-trips through the parser', () => {
  const sample = {
    layer: 'synth', shape: 'pulse', sound: 'thing', lfo: 'on', fx: 'reverb',
    detune: 12, octave: -1, attack: 50, release: 200,
    cutoff: 30, resonance: 40, env: 50, mix: 60,
  };
  assert.deepEqual(Object.keys(sample).sort(), Object.keys(KEYS).sort(), 'a key was added without a test');
  const { values, problems } = parsePreset(formatPreset(sample));
  assert.deepEqual(problems, []);
  assert.deepEqual(values, sample);
});

// ------------------------------------------------------------------- library

test('samples come before kits, and 64 sounds is the ceiling', () => {
  const samples = Array.from({ length: 64 }, (_, i) => wav(`s${String(i).padStart(2, '0')}.wav`));
  const kits = new Map([['drums', [wav('a.wav')]]]);
  const lib = buildLibrary({ samples, kits });
  assert.equal(lib.sounds.length, 64);
  assert.ok(lib.sounds.every((s) => !s.kit), 'the kit never gets listed — samples fill the table first');
  assert.ok(lib.notes.some((n) => /64/.test(n)));
});

test('an empty kit folder is skipped, as Scan does', () => {
  const lib = buildLibrary({ samples: [], kits: new Map([['empty', []], ['full', [wav('a.wav')]]]) });
  assert.deepEqual(lib.sounds.map((s) => s.name), ['full']);
});

test('the limits are the firmware\'s numbers, written out', () => {
  // Absolute, not derived. The first version of this file asserted the pool
  // using POOL_SAMPLES itself, so changing the constant moved the test with it
  // and a wrong pool size passed.
  assert.equal(POOL_SAMPLES, 25_165_824, 'kPoolSamples = 24 * 1024 * 1024 (samples.cpp:17)');
  assert.equal(POOL_SAMPLES * 2, 48 * 1024 * 1024, '48 MiB of int16');
  assert.equal(MAX_SOUNDS, 64);
  assert.equal(MAX_FILES, 512);
  assert.equal(MAX_NAME, 31);
  assert.equal(MAX_FILENAME, 63);
  assert.equal(MAX_PRESETS, 64);
  assert.equal(KIT_FIRST_NOTE, 36);
  assert.equal(noteName(KIT_FIRST_NOTE), 'C2');
  assert.equal(noteName(ROOT_NOTE), 'C4');
});

test('the pool fills and the overflowing sample is dropped, not the card', () => {
  // 12 Mi frames of stereo is 24 Mi int16 -- the whole pool, exactly.
  const big = wav('big.wav', { frames: 12 * 1024 * 1024, channels: 2 });
  const small = wav('small.wav', { frames: 100, channels: 1 });
  const lib = buildLibrary({ samples: [big, small], kits: new Map() });
  const [b, s] = lib.sounds;
  assert.equal(b.files[0].loaded, true, 'exactly full still fits');
  assert.equal(lib.pool, 25_165_824);
  assert.equal(s.files[0].loaded, false);
  assert.match(s.files[0].why, /pool is full/);
  const issues = validate(lib, []);
  assert.ok(issues.some((i) => i.level === 'error' && /small/.test(i.text)));
});

test('a preset naming a missing sound is an error, with a suggestion', () => {
  const lib = buildLibrary({ samples: [wav('bass.wav')], kits: new Map() });
  const presets = [parsePreset('sound = bas', 'lead')];
  const issues = validate(lib, presets);
  const miss = issues.find((i) => /not on the card/.test(i.text));
  assert.ok(miss, 'the firmware only says this at play time, via MissingSound()');
  assert.match(miss.text, /did you mean "bass"/);
});

test('a preset naming a sound that exists is fine, case-insensitively', () => {
  const lib = buildLibrary({ samples: [wav('Bass.wav')], kits: new Map() });
  const issues = validate(lib, [parsePreset('sound = bass', 'p')]);
  assert.deepEqual(issues.filter((i) => /not on the card/.test(i.text)), []);
});

test('two sounds with one name is an error — the Rodeo picks one silently', () => {
  const lib = buildLibrary({
    samples: [wav('clap.wav')],
    kits: new Map([['clap', [wav('a.wav')]]]),
  });
  const issues = validate(lib, []);
  assert.ok(issues.some((i) => i.level === 'error' && /both called "clap"/.test(i.text)));
});

test('a kit longer than the keyboard warns', () => {
  const files = Array.from({ length: 100 }, (_, i) => wav(`${String(i).padStart(3, '0')}.wav`));
  const lib = buildLibrary({ samples: [], kits: new Map([['huge', files]]) });
  const issues = validate(lib, []);
  assert.ok(issues.some((i) => /have none/.test(i.text)));
});

test('poolStats reports the load wait from the card rate', () => {
  const lib = buildLibrary({ samples: [wav('a.wav', { bytes: 1_240_000 })], kits: new Map() });
  const stats = poolStats(lib);
  assert.equal(stats.sounds, 1);
  assert.equal(stats.files, 1);
  assert.ok(Math.abs(stats.loadSeconds - 1) < 0.01, 'about a second per 1.24 MB');
  assert.equal(stats.bytes, 48000 * 1 * 2);
});

// ---------------------------------------------------------------------- wav

import { encodeWav, firstTwo, peak, readWavHeader, scale, toMono, trimSilence } from './js/wav.js';

test('what we encode, the firmware accepts and we can read back', () => {
  const left = Float32Array.from([0, 0.5, -0.5, 1, -1]);
  const buf = encodeWav([left], 48000);
  const h = readWavHeader(buf);
  assert.equal(h.accepted, true);
  assert.equal(h.format, 1, 'plain PCM, never extensible');
  assert.equal(h.bits, 16);
  assert.equal(h.channels, 1);
  assert.equal(h.rate, 48000);
  assert.equal(h.frames, 5);
  assert.equal(h.bytes, 44 + 10);
});

test('full scale is exact in both directions and never wraps', () => {
  const buf = encodeWav([Float32Array.from([1, -1, 2, -2])], 48000);
  const v = new DataView(buf);
  assert.equal(v.getInt16(44, true), 32767);
  assert.equal(v.getInt16(46, true), -32768);
  assert.equal(v.getInt16(48, true), 32767, 'clipped, not wrapped');
  assert.equal(v.getInt16(50, true), -32768);
});

test('a stereo encode interleaves', () => {
  const buf = encodeWav([Float32Array.from([1, 0]), Float32Array.from([0, -1])], 44100);
  const v = new DataView(buf);
  assert.equal(readWavHeader(buf).channels, 2);
  assert.equal(readWavHeader(buf).frames, 2);
  assert.equal(v.getInt16(44, true), 32767, 'L0');
  assert.equal(v.getInt16(46, true), 0, 'R0');
  assert.equal(v.getInt16(50, true), -32768, 'R1');
});

test('readWavHeader rejects what the firmware rejects, and says why', () => {
  const buf = encodeWav([Float32Array.from([0, 0])], 48000);
  new DataView(buf).setUint16(20, 0xfffe, true); // pretend it is extensible
  const h = readWavHeader(buf);
  assert.equal(h.accepted, false);
  assert.match(h.why, /EXTENSIBLE/);
  assert.match(h.why, /re-save it through the console/);

  assert.equal(readWavHeader(new ArrayBuffer(4)).accepted, false);
  const notRiff = encodeWav([Float32Array.from([0])], 48000);
  new DataView(notRiff).setUint8(0, 0x58);
  assert.match(readWavHeader(notRiff).why, /not a RIFF/);
});

test('a data chunk claiming more than the file holds reports what is there', () => {
  const buf = encodeWav([Float32Array.from([0, 0, 0, 0])], 48000);
  new DataView(buf).setUint32(40, 0xffff, true); // truncated download
  assert.equal(readWavHeader(buf).frames, 4, 'not 32767');
});

test('a header walk skips chunks it does not know', () => {
  // LIST chunks before fmt are common from DAWs; an odd size needs its pad byte.
  const payload = encodeWav([Float32Array.from([0.25, -0.25])], 32000);
  const extra = 8 + 5 + 1; // LIST header + 5 bytes + pad
  const out = new ArrayBuffer(payload.byteLength + extra);
  const src = new Uint8Array(payload);
  const dst = new Uint8Array(out);
  const view = new DataView(out);
  dst.set(src.subarray(0, 12), 0);
  for (let i = 0; i < 4; i++) view.setUint8(12 + i, 'LIST'.charCodeAt(i));
  view.setUint32(16, 5, true);
  dst.set(src.subarray(12), 12 + extra);
  view.setUint32(4, out.byteLength - 8, true);
  const h = readWavHeader(out);
  assert.equal(h.accepted, true, 'the odd-sized LIST must be padded past correctly');
  assert.equal(h.rate, 32000);
  assert.equal(h.frames, 2);
});

test('channel helpers match what the Rodeo would keep', () => {
  const six = Array.from({ length: 6 }, () => Float32Array.from([1, 1]));
  assert.equal(firstTwo(six).length, 2);
  assert.equal(toMono([Float32Array.from([1]), Float32Array.from([0])])[0][0], 0.5);
  // Float32Array rounds 0.9 to 0.8999999761581421, so compare with a tolerance
  // rather than pretending these are doubles.
  assert.ok(Math.abs(peak([Float32Array.from([0.2, -0.9])]) - 0.9) < 1e-6);
  assert.equal(scale([Float32Array.from([0.5])], 2)[0][0], 1);
});

test('trimSilence keeps a little lead-in and drops the tail', () => {
  const n = 48000;
  const c = new Float32Array(n);
  for (let i = 1000; i < 2000; i++) c[i] = 0.5;
  const [out] = trimSilence([c], 48000);
  assert.ok(out.length < 1200 && out.length > 1000, `kept ${out.length}`);
  assert.ok(Math.abs(out[out.length - 1]) > 0.4, 'the tail ends on the sound');
  const silent = trimSilence([new Float32Array(100)], 48000);
  assert.equal(silent[0].length, 0, 'an entirely silent file trims to nothing');
});

// --------------------------------------------------------------------- MIDI
//
// Read off main.cpp's `Midi()` and synth.cpp's `Allocate()`/`Take()`. The
// allocator is the interesting one: the console claims on screen what Rodeo
// should do with the notes it just sent, and a wrong claim is worse than none.

import {
  BEND_SEMITONES,
  VOICE_COUNT,
  Voices,
  allNotesOff,
  isRodeoPort,
  noteOff,
  noteOn,
  pitchBend,
  sustain,
} from './js/midi.js';

test('messages are the bytes the firmware switches on', () => {
  assert.deepEqual(noteOn(60, 100), [0x90, 60, 100]);
  assert.deepEqual(noteOff(60), [0x80, 60, 0]);
  assert.deepEqual(sustain(true), [0xb0, 64, 127]);
  assert.deepEqual(sustain(false), [0xb0, 64, 0]);
  // CC 120 and 123 both reach AllNotesOff (main.cpp:87); we send 123.
  assert.deepEqual(allNotesOff(), [0xb0, 123, 0]);
  // The channel is masked off by `status & 0xF0`, so it may be anything.
  assert.deepEqual(noteOn(60, 100, 9), [0x99, 60, 100]);
  assert.deepEqual(noteOn(200, 900), [0x90, 127, 127], 'clamped, never wrapped');
});

test('pitch bend is the exact inverse of main.cpp:83', () => {
  const back = ([, lo, hi]) => (2 * ((lo | (hi << 7)) - 8192)) / 8192;
  assert.deepEqual(pitchBend(0), [0xe0, 0, 64], 'centre is 8192');
  assert.equal(back(pitchBend(0)), 0);
  assert.equal(back(pitchBend(-BEND_SEMITONES)), -2, 'the bottom is reachable exactly');
  for (const s of [-1.5, -0.25, 0.5, 1]) assert.ok(Math.abs(back(pitchBend(s)) - s) < 1e-3, s);
  // raw 16383 is +1.99975 semitones: +2 cannot be sent, and the console must
  // not pretend otherwise by wrapping to 0 or overflowing the byte.
  const top = pitchBend(BEND_SEMITONES);
  assert.deepEqual(top, [0xe0, 0x7f, 0x7f]);
  assert.ok(back(top) < 2 && back(top) > 1.999);
  assert.deepEqual(pitchBend(99), top, 'out of range clamps to the top');
});

test('a port is a Rodeo by either string the descriptors set', () => {
  assert.ok(isRodeoPort('Rodeo MIDI'));
  assert.ok(isRodeoPort('Rodeo Demo'));
  assert.ok(isRodeoPort('rodeo demo midi 1'));
  assert.ok(!isRodeoPort('IAC Driver Bus 1'));
  assert.ok(!isRodeoPort(''));
  assert.ok(!isRodeoPort(undefined));
});

test('velocity 0 is a note off, not a silent note', () => {
  const v = new Voices();
  v.noteOn(60, 100, 0);
  assert.equal(v.sounding(0), 1);
  v.noteOn(60, 0, 1); // synth.cpp:294 routes this to NoteOff
  assert.equal(v.slots[0].gate, false);
  assert.equal(v.slots[0].key, false);
});

test('Allocate takes a free voice, then steals the oldest', () => {
  const v = new Voices({ release: 100 });
  for (let i = 0; i < VOICE_COUNT; i++) {
    const r = v.noteOn(60 + i, 100, i);
    assert.equal(r.index, i, 'eight notes take eight voices in order');
    assert.equal(r.how, 'free');
    assert.equal(r.took, null);
  }
  const ninth = v.noteOn(80, 100, 10);
  assert.equal(ninth.how, 'stole a sounding voice');
  assert.equal(ninth.index, 0, 'the oldest voice goes');
  assert.equal(ninth.took, 60, 'and it was holding the first note played');
  assert.equal(v.sounding(10), VOICE_COUNT, 'still eight, never nine');
});

test('a released voice is stolen before a held one, whatever the order', () => {
  const v = new Voices({ release: 10_000 }); // long release: all still sounding
  for (let i = 0; i < VOICE_COUNT; i++) v.noteOn(60 + i, 100, i);
  v.noteOff(64, 100); // the fifth note played, now released but still ringing
  const next = v.noteOn(90, 100, 110);
  assert.equal(next.how, 'stole a released voice');
  assert.equal(next.took, 64, 'not note 60, which is older but still held');
});

test('pressing a key again takes its own voice back, even a quiet one', () => {
  const v = new Voices({ release: 50 });
  v.noteOn(60, 100, 0); // slot 0
  v.noteOn(62, 100, 1); // slot 1
  v.noteOff(60, 2);
  v.noteOff(62, 3);
  // Both are long finished, so both are free, and the free scan runs from slot
  // 0 upward. `Allocate` checks `v.note == note` FIRST, though (synth.cpp:138),
  // and `note` is never cleared on release — so 62 goes home to slot 1 rather
  // than taking slot 0, which is what a plain free-voice search would do.
  const again = v.noteOn(62, 100, 500);
  assert.equal(again.how, 'retrigger');
  assert.equal(again.index, 1);
  assert.equal(again.took, null, 'nothing audible was taken');
  // And the corollary, which is why the test above was wrong the first time: a
  // note that has gone quiet does NOT keep its voice reserved. Any other note
  // claiming a free voice takes the lowest-numbered idle one, name and all.
  const fresh = new Voices({ release: 50 });
  fresh.noteOn(60, 100, 0);
  fresh.noteOff(60, 2);
  const stranger = fresh.noteOn(61, 100, 500);
  assert.equal(stranger.index, 0, '61 takes the idle slot 60 was using');
  assert.equal(fresh.slots[0].note, 61);
});

test('sustain holds a key-up and lifting the pedal drops everything at once', () => {
  const v = new Voices({ release: 1000 });
  v.setSustain(true, 0);
  v.noteOn(60, 100, 0);
  v.noteOn(64, 100, 1);
  v.noteOff(60, 10);
  v.noteOff(64, 11);
  assert.equal(v.slots[0].gate, true, 'the pedal is down, so it keeps sounding');
  assert.equal(v.slots[0].key, false, 'but the key is up');
  const r = v.setSustain(false, 20);
  assert.deepEqual(r.released, [0, 1]);
  assert.equal(v.slots[0].gate, false);
});

test('a note off only reaches a voice that still holds its key', () => {
  const v = new Voices({ release: 10_000 });
  for (let i = 0; i < VOICE_COUNT; i++) v.noteOn(60 + i, 100, i);
  v.noteOn(90, 100, 10); // steals slot 0, which was holding 60
  const r = v.noteOff(60, 20);
  assert.deepEqual(r.off, [], 'the key-up for a stolen note finds nothing, and is harmless');
  assert.equal(v.slots[0].note, 90, 'the thief keeps the voice');
});

test('in sampler layer a key with no sample allocates nothing', () => {
  const v = new Voices();
  // `Start` returns before `Allocate` when the sampler alone has nothing on
  // this key (synth.cpp:161), so such a key cannot steal a voice either.
  for (let i = 0; i < VOICE_COUNT; i++) v.noteOn(60 + i, 100, i, true);
  const quiet = v.noteOn(90, 100, 10, false);
  assert.deepEqual(quiet, { silent: true, note: 90 });
  assert.equal(v.slots[0].note, 60, 'nothing was stolen');
});

test('all notes off RELEASES every voice rather than muting it', () => {
  const v = new Voices({ release: 2000 });
  for (let i = 0; i < VOICE_COUNT; i++) v.noteOn(60 + i, 100, i);
  v.allOff(10);
  assert.ok(v.slots.every((s) => !s.gate && !s.key), 'every key and gate is down');
  // `Ev::AllOff` calls `Release`, which only drops the gate (synth.cpp:218,191),
  // so the amp envelopes still run their release stage. Panic on a preset with
  // a 4 s release is four more seconds of sound, and the page should not claim
  // the board went quiet.
  assert.equal(v.sounding(11), VOICE_COUNT, 'still ringing the instant after');
  assert.equal(v.sounding(2011), 0, 'quiet once the release has run');
});

test('a sequencer step cannot overrun the firmware event queue', () => {
  // `Push` drops once 64 events are unread, and `Process` drains them every
  // 1/3 ms (synth.cpp:40-47). Everything the page can send at one instant is
  // one column of the grid plus one arp note, so the ceiling is the grid's
  // own height — worth asserting, because widening the grid would raise it.
  const full = emptyPattern();
  for (const row of full) row.fill(1);
  const worst = stepNotes(full, 0, 36).length + 1;
  assert.equal(worst, ROWS + 1);
  assert.ok(worst < 64, `${worst} notes could land in the same millisecond`);
});

// ---------------------------------------------------- the clock and the arp
//
// Unlike everything above, js/seq.js is a copy of nothing: Rodeo has no
// sequencer and no arpeggiator. So these check the arithmetic against music,
// not against the firmware — the one firmware fact that reaches here is that
// the ninth note steals, which is tested above.

import {
  ARP_MODES,
  BPM_MAX,
  BPM_MIN,
  DIVISIONS,
  ROWS,
  STEPS,
  SWING_MAX,
  arpSequence,
  emptyPattern,
  gateMs,
  hasSteps,
  pickStep,
  rowNote,
  stepMs,
  stepNotes,
  stepTime,
  swingOffset,
} from './js/seq.js';

test('a step is as long as the tempo says', () => {
  const by = (id) => DIVISIONS.find((d) => d.id === id).beats;
  assert.equal(stepMs(120, by('1/4')), 500);
  assert.equal(stepMs(120, by('1/8')), 250);
  assert.equal(stepMs(120, by('1/16')), 125);
  assert.equal(stepMs(60, by('1/16')), 250);
  // Triplets: three in the space of the next division up. Three eighth-note
  // triplets fill one quarter, not two — which is what the first version of
  // this test asserted, and the reason it is spelled out both ways here.
  assert.ok(Math.abs(stepMs(120, by('1/8T')) * 3 - stepMs(120, by('1/4'))) < 1e-9);
  assert.ok(Math.abs(stepMs(120, by('1/16T')) * 3 - stepMs(120, by('1/8'))) < 1e-9);
  assert.ok(stepMs(120, by('1/8T')) < stepMs(120, by('1/8')), 'a triplet is the shorter step');
  assert.equal(stepMs(10, by('1/4')), stepMs(BPM_MIN, by('1/4')), 'clamped, not divided by nonsense');
  assert.equal(stepMs(10_000, by('1/4')), stepMs(BPM_MAX, by('1/4')));
});

test('swing delays the off-beats and nothing else', () => {
  assert.equal(swingOffset(0, 100, 0.5), 0);
  assert.equal(swingOffset(2, 100, 0.5), 0);
  assert.equal(swingOffset(1, 100, 0.5), 50);
  assert.equal(swingOffset(3, 100, 0.5), 50);
  assert.equal(swingOffset(1, 100, 0), 0, 'no swing is an even grid');
  // However hard it is pushed, a swung step must still land before the next.
  assert.ok(swingOffset(1, 100, 99) < 100);
  assert.equal(swingOffset(1, 100, 99), 100 * SWING_MAX);
});

test('swing shifts a step without the grid drifting', () => {
  const ms = 125;
  const at = (i) => stepTime(1000, i, ms, 0.5);
  assert.equal(at(0), 1000);
  assert.equal(at(1), 1000 + 125 + 62.5);
  assert.equal(at(2), 1250, 'back on the grid, not 1312.5');
  assert.equal(at(16), 1000 + 16 * ms, 'sixteen steps later is exactly a bar');
  for (let i = 1; i < 32; i++) assert.ok(at(i) > at(i - 1), `step ${i} must come after ${i - 1}`);
});

test('the arp climbs, falls, and does not stutter at the top', () => {
  const triad = [60, 64, 67];
  assert.deepEqual(arpSequence(triad, 'up'), [60, 64, 67]);
  assert.deepEqual(arpSequence(triad, 'down'), [67, 64, 60]);
  // Classic up-down: four steps from three notes, endpoints played once.
  assert.deepEqual(arpSequence(triad, 'up-down'), [60, 64, 67, 64]);
  assert.deepEqual(arpSequence([60, 64], 'up-down'), [60, 64], 'two notes cannot turn around');
  assert.deepEqual(arpSequence([60], 'up-down'), [60]);
  assert.deepEqual(arpSequence([], 'up'), []);
  assert.deepEqual(arpSequence(triad, 'off'), [], 'off is off, whatever is held');
});

test('the arp sorts by pitch except where it should not', () => {
  const played = [67, 60, 64]; // held in that order
  assert.deepEqual(arpSequence(played, 'up'), [60, 64, 67]);
  assert.deepEqual(arpSequence(played, 'as played'), [67, 60, 64]);
  assert.deepEqual(arpSequence([60, 60, 64], 'up'), [60, 64], 'a doubled note is one note');
});

test('arp octaves stack the whole figure, in order', () => {
  assert.deepEqual(arpSequence([60, 64], 'up', 2), [60, 64, 72, 76]);
  assert.deepEqual(arpSequence([60, 64], 'down', 2), [76, 72, 64, 60]);
  assert.deepEqual(arpSequence([60], 'up', 4), [60, 72, 84, 96]);
  assert.deepEqual(arpSequence([60], 'up', 9), arpSequence([60], 'up', 4), 'clamped at four');
  assert.deepEqual(arpSequence([60], 'up', 0), [60]);
});

test('the cycle wraps forever and random stays inside it', () => {
  const cycle = arpSequence([60, 64, 67], 'up');
  assert.equal(pickStep(cycle, 0, 'up'), 60);
  assert.equal(pickStep(cycle, 3, 'up'), 60, 'step 3 wraps to the start');
  assert.equal(pickStep(cycle, 100, 'up'), cycle[100 % 3]);
  assert.equal(pickStep([], 0, 'up'), null, 'nothing held is silence, not a crash');
  // Random picks from the held notes; it must never invent one.
  for (const r of [0, 0.4, 0.99, 0.999999]) {
    const n = pickStep(cycle, 0, 'random', () => r);
    assert.ok(cycle.includes(n), `${r} gave ${n}`);
  }
  assert.ok(ARP_MODES.includes('random') && ARP_MODES[0] === 'off');
});

test('the grid reads as a piano roll, top row highest', () => {
  assert.equal(rowNote(36, 0), 43, 'the top row is the highest note');
  assert.equal(rowNote(36, ROWS - 1), 36, 'the bottom row is the root');
  for (let r = 1; r < ROWS; r++) assert.ok(rowNote(36, r) < rowNote(36, r - 1));
});

test('a step sounds exactly the cells switched on in its column', () => {
  const p = emptyPattern();
  assert.equal(hasSteps(p), false);
  assert.deepEqual(stepNotes(p, 0, 36), []);
  p[ROWS - 1][0] = 1; // bottom row, first step
  p[0][0] = 1; // top row, same step
  p[3][5] = 1;
  assert.equal(hasSteps(p), true);
  assert.deepEqual(stepNotes(p, 0, 36), [43, 36], 'both notes in the column, top row first');
  assert.deepEqual(stepNotes(p, 1, 36), []);
  assert.deepEqual(stepNotes(p, 5, 36), [rowNote(36, 3)]);
  assert.deepEqual(stepNotes(p, STEPS, 36), stepNotes(p, 0, 36), 'the pattern loops');
  assert.deepEqual(stepNotes(p, 0, 48), [55, 48], 'the root transposes the lot');
});

test('a gate is never longer than its step, nor too short to sound', () => {
  assert.equal(gateMs(100, 0.5), 50);
  assert.equal(gateMs(100, 1), 100);
  assert.equal(gateMs(100, 2), 100, 'a gate cannot overrun its own step');
  assert.equal(gateMs(20, 0.05), 15, 'and never so short the envelope cannot open');
  assert.equal(gateMs(1000, 0.001), 50, 'the floor is a fraction, then a minimum');
});

// --------------------------------------------------- the `#S` line, read
//
// Built from instruments/rodeo-demo/firmware/src/state_line.cpp and main.cpp's
// StateLine(). The lines below are the shape that code emits; when the firmware
// changes the line, these are what should fail.

import {
  CHAIN_STATES,
  FAULTS,
  breakAfter,
  faultText,
  kindInput,
  kindName,
  kindSlots,
  kindSupported,
  parseStateLine,
  FX_NAMES,
  PANEL_ENCODERS,
  PANEL_POTS,
  TAKEOVER,
  encoderReadout,
  panelBanks,
  potForRelease,
  potHeld,
  potPercent,
  releaseFromPot,
  scanHealth,
  slots,
  takeLines,
} from './js/chain.js';

const LINE =
  '#S {"preset":2,"name":"bass","layer":"both","shape":"saw","sound":1,"nsounds":18,' +
  '"cut":41,"res":12,"env":60,"atk":8,"rel":220,"voices":3,"card":1,"loaded":18,' +
  '"samples":18,"skipped":0,"kb":31204,"loadms":25140,' +
  '"chain":{"state":"scan","sats":2,"broken":-1,"panel":1,"s":[' +
  '{"kind":5,"fw":"1.0","in":[412,900,77,512]},' +
  '{"kind":6,"fw":"1.0","in":[3,-1,0,0],"btn":[0,1,0,0]}],' +
  '"good":24581,"bad":0},"cpu":34,"peak":51}';

test('a whole state line parses to the firmware’s own numbers', () => {
  const st = parseStateLine(LINE);
  assert.equal(st.preset, 2, '0-based, unlike the prose which prints preset + 1');
  assert.equal(st.name, 'bass');
  assert.equal(st.rel, 220);
  assert.equal(st.chain.sats, 2);
  assert.equal(st.chain.s.length, 2);
  assert.equal(st.cpu, 34, 'cpu and peak come last, after the chain closes');
});

test('anything that is not a whole state line is skipped, not guessed at', () => {
  // The firmware gives up on a line if the computer stops reading for 50 ms
  // (main.cpp:165). Half a line must never become half a screen of numbers.
  assert.equal(parseStateLine(LINE.slice(0, 180)), null, 'a truncated line');
  assert.equal(parseStateLine('rodeo-demo. s = state, d = disk mode'), null, 'the greeting');
  assert.equal(parseStateLine('chain: 2 satellites, pot bank and encoder bank found'), null);
  assert.equal(parseStateLine('#S not json at all'), null);
  assert.equal(parseStateLine('#S [1,2,3]'), null, 'valid JSON, wrong shape');
  assert.equal(parseStateLine('#S null'), null);
  assert.equal(parseStateLine(''), null);
  assert.equal(parseStateLine(undefined), null);
  assert.equal(parseStateLine(` ${LINE}`), null, 'the prefix is at the start or it is not state');
});

test('a read can split a line anywhere, and does', () => {
  // USB CDC hands over whatever happened to be in the buffer.
  let buffer = '';
  const got = [];
  for (const chunk of ['#S {"a":1}\r\n#S {"b', '":2}\r\nrodeo-demo\r\n#S {"c":3}']) {
    buffer += chunk;
    const { lines, rest } = takeLines(buffer);
    buffer = rest;
    got.push(...lines);
  }
  assert.deepEqual(got, ['#S {"a":1}', '#S {"b":2}', 'rodeo-demo']);
  assert.equal(buffer, '#S {"c":3}', 'the unfinished line is kept for the next read');
  assert.deepEqual(parseStateLine(got[1]), { b: 2 });
});

test('board kinds match sat_frame.h', () => {
  assert.equal(kindName(5), 'pot bank');
  assert.equal(kindName(6), 'encoder bank');
  assert.equal(kindName(7), 'button bank');
  assert.equal(kindName(15), 'no ID fitted');
  assert.equal(kindName(9), 'kind 9', 'a kind kept for later is named, not invented');
  // Only the three banks have four slots.
  for (const k of [5, 6, 7]) assert.equal(kindSlots(k), 4, `kind ${k}`);
  for (const k of [0, 1, 2, 3, 4, 14, 15]) assert.equal(kindSlots(k), 1, `kind ${k}`);
  assert.equal(kindInput(1), 'pot');
  assert.equal(kindInput(5), 'pot');
  assert.equal(kindInput(6), 'encoder');
  assert.equal(kindInput(7), 'button');
  assert.equal(kindInput(4), 'switch');
  assert.equal(kindInput(14), null, 'a custom board declares its own slots; the line cannot say');
  assert.equal(kindSupported(7), true);
  assert.equal(kindSupported(14), false, 'custom boards bring their own firmware');
  assert.equal(kindSupported(15), true);
});

test('slots are drawn by what the board actually is', () => {
  const st = parseStateLine(LINE);
  const pots = slots(st.chain.s[0]);
  assert.equal(pots.length, 4);
  assert.ok(pots.every((s) => s.type === 'pot'));
  assert.equal(potPercent(pots[1].value), 90);
  assert.equal(potPercent(0), 0);
  assert.equal(potPercent(1000), 100);

  const encs = slots(st.chain.s[1]);
  assert.ok(encs.every((s) => s.type === 'encoder'));
  assert.equal(encs[0].value, 3, 'a running detent total, which can go either way');
  assert.equal(encs[1].value, -1);
  assert.equal(encs[1].pressed, true, 'btn[] is how an encoder reports its push switch');
  assert.equal(encs[0].pressed, false);

  // A button bank has no btn[]: its presses are already in in[].
  const buttons = slots({ kind: 7, in: [0, 1, 0, 0] });
  assert.ok(buttons.every((s) => s.type === 'button'));
  assert.equal(buttons[1].pressed, true);
  // A custom board's numbers are shown as numbers.
  assert.equal(slots({ kind: 14, in: [7] })[0].type, null);
  assert.deepEqual(slots({}), [], 'a satellite with no in[] draws nothing, and does not throw');
});

test('a faulted slot outranks whatever its board kind would draw', () => {
  const sat = { kind: 5, in: [500, 0, 0, 0], fault: [null, [1, 47], null, [2, 9]] };
  const got = slots(sat);
  assert.equal(got[0].type, 'pot', 'the slots that are fine still draw as pots');
  assert.equal(got[1].type, 'fault');
  assert.equal(got[3].type, 'fault');
  assert.match(faultText([1, 47]), /between two kinds/);
  assert.match(faultText([1, 47]), /4\.7/, 'code 1 carries the reading as kind x 10');
  assert.match(faultText([2, 9]), /kind 9/, 'code 2 carries the kind');
  assert.equal(faultText([3]), FAULTS[3]);
  assert.match(faultText([99]), /fault 99/, 'an unknown code is reported, not swallowed');
});

test('a broken ring is reported as a place, not a boolean', () => {
  assert.equal(breakAfter({ broken: -1 }), null);
  assert.equal(breakAfter({ broken: 0 }), 0, 'broken before the first satellite is still a break');
  assert.equal(breakAfter({ broken: 2 }), 2);
  assert.equal(breakAfter(undefined), null);
  assert.ok(CHAIN_STATES.partial && CHAIN_STATES.scan && CHAIN_STATES.version);
});

test('scan health says nothing before any scans have run', () => {
  assert.equal(scanHealth({ good: 0, bad: 0 }), null, 'not 0%, which would read as broken');
  assert.equal(scanHealth({}), null);
  assert.equal(scanHealth({ good: 24581, bad: 0 }), 100);
  assert.equal(scanHealth({ good: 3, bad: 1 }), 75);
});

test('the panel takes the first bank of each kind, as Find() does', () => {
  // panel.cpp:73-76 keeps the first pot bank and the first encoder bank it
  // meets; a second of either is on the chain but drives nothing.
  const two = { s: [{ kind: 7 }, { kind: 5 }, { kind: 6 }, { kind: 5 }] };
  assert.deepEqual(panelBanks(two), { pot: 1, enc: 2 });
  assert.deepEqual(panelBanks({ s: [{ kind: 7 }] }), { pot: null, enc: null });
  assert.deepEqual(panelBanks({}), { pot: null, enc: null });
});

test('release is the pot curve from panel.cpp, both ways', () => {
  // p.release = 0.005 * 800^pot seconds (panel.cpp:187).
  assert.equal(Math.round(releaseFromPot(0)), 5);
  assert.equal(Math.round(releaseFromPot(1)), 4000);
  assert.ok(Math.abs(releaseFromPot(0.5) - 5 * Math.sqrt(800)) < 1e-6);
  for (const ms of [5, 50, 420, 4000])
    assert.ok(Math.abs(releaseFromPot(potForRelease(ms)) - ms) < 0.01, `${ms} ms`);
  assert.equal(potForRelease(1), potForRelease(5), 'below the floor clamps, not NaN');
  assert.equal(releaseFromPot(2), releaseFromPot(1), 'and above the ceiling too');
});

test('a knob that disagrees with its parameter is the preset still holding it', () => {
  const [cutoff, , , release] = PANEL_POTS;
  // Agreeing: the knob is what you hear.
  assert.equal(potHeld(410, 41, cutoff.toPot), false);
  assert.equal(potHeld(410, 42, cutoff.toPot), false, 'within the 2% takeover band');
  // Disagreeing by more than kTakeover: the preset set it and the knob has
  // not been moved since (panel.cpp:178-186).
  assert.equal(potHeld(700, 41, cutoff.toPot), true);
  assert.equal(TAKEOVER, 0.02);
  // Release compares on its own curve, not linearly: 420 ms is knob 66%, so a
  // knob at 66% agrees even though 420 and 66 look nothing alike.
  assert.equal(potHeld(663, 420, release.toPot), false);
  assert.equal(potHeld(200, 420, release.toPot), true);
  // Nothing to compare is not a disagreement.
  assert.equal(potHeld(500, undefined, cutoff.toPot), false);
  assert.equal(potHeld(undefined, 41, cutoff.toPot), false);
});

test('every panel control names the parameter it drives', () => {
  assert.equal(PANEL_POTS.length, 4);
  assert.equal(PANEL_ENCODERS.length, 4);
  assert.deepEqual(PANEL_POTS.map((p) => p.key), ['cut', 'res', 'env', 'rel']);
  assert.equal(PANEL_ENCODERS[0].sampler, 'sound', 'E1 picks the sound in sampler layer');
  // E2's detune and E4's effects mix used to be missing from the line; the
  // firmware added det/oct/lfo/fx/mix in 807d9b8, so every control now
  // reports. This test asserted the opposite until then.
  assert.deepEqual(PANEL_ENCODERS.map((e) => e.key), ['shape', 'det', 'atk', 'mix']);
  assert.ok(PANEL_ENCODERS.every((e) => e.tap), 'and each says what a tap does');
  // The index in `fx` is the enum's own order (synth.h:28), the same order
  // presets.cpp parses the words in — which is where preset.js's FX came from.
  assert.deepEqual(FX_NAMES, ['chorus', 'reverb', 'both']);
});

test('an encoder row reads out what it turns and what its tap set', () => {
  const st = { shape: 'saw', layer: 'both', det: 8, oct: -1, atk: 5, lfo: 1, mix: 25, fx: 2,
               sound: 1, nsounds: 4 };
  const [e1, e2, e3, e4] = PANEL_ENCODERS.map((spec) => encoderReadout(spec, st));
  assert.deepEqual([e1.name, e1.value, e1.extra], ['shape', 'saw', 'both']);
  assert.deepEqual([e2.name, e2.value, e2.extra], ['detune', '8 cents', 'octave -1']);
  assert.deepEqual([e3.name, e3.value, e3.extra], ['attack', '5 ms', 'LFO on']);
  assert.deepEqual([e4.name, e4.value, e4.extra], ['effects mix', '25%', 'both']);
  assert.equal(encoderReadout(PANEL_ENCODERS[2], { ...st, lfo: 0 }).extra, 'LFO off');
  assert.equal(encoderReadout(PANEL_ENCODERS[3], { ...st, fx: 0 }).extra, 'chorus');
  assert.equal(encoderReadout(PANEL_ENCODERS[1], { ...st, oct: 2 }).extra, 'octave +2');
  // In sampler layer E1 picks the sound instead of the shape.
  const sampled = encoderReadout(PANEL_ENCODERS[0], st, true);
  assert.deepEqual([sampled.name, sampled.value], ['sound', '2 of 4']);
});

test('a value the line does not carry reads as absent, never as zero', () => {
  // An older firmware, or a field dropped from the line later: the row must
  // not print a blank, which looks exactly like a parameter set to nothing.
  const old = { shape: 'saw', layer: 'both', atk: 5 };
  const [, e2, e3, e4] = PANEL_ENCODERS.map((spec) => encoderReadout(spec, old));
  assert.equal(e2.value, 'not reported');
  assert.equal(e2.reported, false);
  assert.equal(e2.extra, '', 'and no octave either');
  assert.equal(e3.value, '5 ms', 'what it does have still reads');
  assert.equal(e3.extra, '', 'but its LFO does not');
  assert.equal(e4.value, 'not reported');
  assert.equal(encoderReadout(PANEL_ENCODERS[1], {}).value, 'not reported');
  assert.equal(encoderReadout(PANEL_ENCODERS[1], undefined).value, 'not reported');
  // Zero is a value, not an absence.
  assert.equal(encoderReadout(PANEL_ENCODERS[1], { det: 0, oct: 0 }).value, '0 cents');
  assert.equal(encoderReadout(PANEL_ENCODERS[1], { det: 0, oct: 0 }).extra, 'octave 0');
});
