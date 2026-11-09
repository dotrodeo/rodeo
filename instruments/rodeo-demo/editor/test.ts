// npm test, in instruments/rodeo-demo/editor (node --test; Node strips the types)
//
// These test that the editor agrees with the firmware, not that the editor
// agrees with itself. Every case below was read off
// instruments/rodeo-demo/firmware/src/{samples,presets}.cpp; when one fails
// after a firmware change, the firmware is right.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync } from 'node:fs';
import type { AcceptedCardFile, CardFile } from './src/card.ts';
import type { RejectedWav } from './src/wav.ts';

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
} from './src/card.ts';
import { KEYS, formatPreset, parsePreset, squash } from './src/preset.ts';
import { buildLibrary, poolStats, validate } from './src/model.ts';

const wav = (name: string, over: Partial<AcceptedCardFile> = {}): CardFile => ({
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
  const notes = lib.sounds[0].files.map((f) => [f.name, noteName(f.note as number)]);
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
    layer: 'synth', shape: 'fm', sound: 'thing', lfo: 'on', fx: 'reverb',
    detune: 12, octave: -1, attack: 50, release: 200,
    cutoff: 30, resonance: 40, env: 50, mix: 60,
    drive: 30, decay: 600, sustain: 80, hpf: 120, fattack: 5, fdecay: 900,
    fsustain: 10, glide: 80, mono: 'on', filter: 'ladder12', keytrack: 50,
  };
  assert.deepEqual(Object.keys(sample).sort(), Object.keys(KEYS).sort(), 'a key was added without a test');
  const { values, problems } = parsePreset(formatPreset(sample));
  assert.deepEqual(problems, []);
  assert.deepEqual(values, sample);
});

test('the pots in KEYS are the panel\'s, one key each', () => {
  // panel.h: pot bank 1 is the filter, pot bank 2 the amp envelope. A key with
  // a pot is one a preset can leave to the knob.
  const pots = Object.entries(KEYS).filter(([, s]) => s.pot).sort((a, b) => a[1].pot! - b[1].pot!);
  assert.deepEqual(pots.map(([k]) => k),
                   ['cutoff', 'resonance', 'env', 'drive', 'attack', 'decay', 'sustain', 'release']);
  assert.deepEqual(pots.map(([, s]) => s.pot), [1, 2, 3, 4, 5, 6, 7, 8]);
});

test('the presets on the card parse without a problem', () => {
  // They use the engine's keys that the editor once did not know.
  const dir = new URL('../sd-card/presets/', import.meta.url);
  for (const f of readdirSync(dir).filter((n) => n.endsWith('.txt'))) {
    const { problems } = parsePreset(readFileSync(new URL(f, dir), 'utf8'), f);
    assert.deepEqual(problems, [], f);
  }
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
  assert.match(s.files[0].why as string, /pool is full/);
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

import { encodeWav, firstTwo, peak, readWavHeader, scale, toMono, trimSilence } from './src/wav.ts';

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
  assert.match((readWavHeader(notRiff) as RejectedWav).why, /not a RIFF/);
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

// ------------------------------------------------------------ the voices
//
// Read off synth.cpp's `Allocate()`/`Take()`. The editor claims on screen what
// the Rodeo should do with the notes the console just sent, and a wrong claim
// is worse than none.

import { VOICE_COUNT, Voices } from './src/voices.ts';

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

