// rodeo-demo's profile for the console (profile.ts): its words, its encoders,
// its readout. The table below is params.cpp's, as the hello sends it.
import assert from 'node:assert/strict';
import { test } from 'node:test';

import type { Param, Snapshot } from '@dotrodeo/rodeo-host';
import { describe as say } from 'rodeo-console/describe';
import { ENCODERS, PANEL, PRESET_ONLY, describe, encoder, readout } from './profile.ts';

const P = (id: number, key: string, min: number, max: number, unit?: string, names?: string[], pot?: number): Param =>
  ({ id, key, min, max, def: min, ...(unit ? { unit } : {}), ...(names ? { names } : {}), ...(pot ? { pot } : {}) });

const TABLE = [
  P(0, 'cutoff', 0, 1000, '‰', undefined, 1),
  P(1, 'resonance', 0, 1000, '‰', undefined, 2),
  P(2, 'env', 0, 1000, '‰', undefined, 3),
  P(3, 'release', 5, 4000, 'ms', undefined, 8),
  P(4, 'attack', 1, 4000, 'ms', undefined, 5),
  P(5, 'detune', 0, 50, 'cents'),
  P(6, 'octave', -2, 2),
  P(7, 'mix', 0, 100, '%'),
  P(8, 'lfo', 0, 1, undefined, ['off', 'on']),
  P(9, 'fx', 0, 2, undefined, ['chorus', 'reverb', 'both']),
  P(10, 'layer', 0, 2, undefined, ['synth', 'sampler', 'both']),
  P(11, 'shape', 0, 6, undefined, ['saw', 'square', 'pulse', 'triangle', 'saw + sub', 'square + sub', 'fm']),
  P(12, 'sound', 0, 3),
  P(16, 'decay', 1, 10000, 'ms', undefined, 6),
  P(17, 'sustain', 0, 100, '%', undefined, 7),
  P(18, 'fattack', 0, 4000, 'ms'),
  P(19, 'fdecay', 1, 10000, 'ms'),
  P(20, 'fsustain', 0, 100, '%'),
  P(21, 'filter', 0, 2, undefined, ['svf', 'ladder', 'ladder12']),
  P(22, 'drive', 0, 100, '%', undefined, 4),
  P(23, 'hpf', 0, 2000, 'Hz'),
  P(24, 'keytrack', 0, 100, '%'),
  P(25, 'mono', 0, 1, undefined, ['off', 'on']),
  P(26, 'glide', 0, 2000, 'ms'),
];
const params = Object.fromEntries(TABLE.map((p) => [p.key, p]));

function snap(p: Record<string, number>, extra: Record<string, unknown> = {}): Snapshot {
  return {
    status: 'ready',
    hello: { fw: 'rodeo-demo', build: 'test', proto: 2, sdk: '0.3.0', chain: 1, params: TABLE },
    params,
    state: { p, hold: 0, cpu: 10, preset: 1, name: 'bass', voices: 3, card: 1, sounds: 4, loaded: 4, samples: 9, panel: 1, ...extra },
    chain: null,
    said: null,
    replies: { 0x62: { sounds: [{ name: 'kick' }, { name: 'Grand Piano' }, { name: 'c' }, { name: 'd' }] } },
  } as unknown as Snapshot;
}

test('the panel is the instrument: two pot banks, two encoder banks, four each', () => {
  assert.deepEqual(PANEL.map((b) => b.controls.map((c) => c.kind)), [
    ['pot', 'pot', 'pot', 'pot'],
    ['pot', 'pot', 'pot', 'pot'],
    ['encoder', 'encoder', 'encoder', 'encoder'],
    ['encoder', 'encoder', 'encoder', 'encoder'],
  ]);
  // each pot drives the parameter the hello puts on it, in order
  const pots = PANEL.slice(0, 2).flatMap((b) => b.controls.map((c) => c.turns[0]));
  assert.deepEqual(pots.map((k) => params[k]!.pot), [1, 2, 3, 4, 5, 6, 7, 8]);
});

test('every parameter in the table has one place: a control, or preset only', () => {
  const placed = PANEL.flatMap((b) =>
    b.controls.flatMap((c) => [...c.turns, ...(c.shows ?? []), ...(c.hold?.key ? [c.hold.key] : [])])
  );
  const unique = new Set(placed);
  for (const p of TABLE) assert.ok(unique.has(p.key) !== PRESET_ONLY.includes(p.key), p.key);
  assert.equal(unique.size + PRESET_ONLY.length, TABLE.length);
});

test('a tap does what Tap() in panel.cpp does', () => {
  const s = snap({ layer: 0, detune: 30, octave: 2, lfo: 0, fx: 2, fattack: 900, fdecay: 50, fsustain: 80, mono: 1 });
  const tap = (n: number, st = s) => ENCODERS[n]!.tap!.act(st);
  assert.deepEqual(tap(0), [['layer', 1]], 'synth to sampler');
  assert.deepEqual(tap(0, snap({ layer: 2 })), [['layer', 0]], 'both round to synth');
  const empty = { ...snap({ layer: 0 }), replies: {} } as unknown as Snapshot;
  assert.deepEqual(tap(0, empty), [['layer', 0]], 'no sounds on the card: stays synth');
  assert.deepEqual(tap(1), [['detune', params.detune!.def], ['octave', 0]], 'detune to its default, octave to 0');
  assert.deepEqual(tap(2), [['lfo', 1]]);
  assert.deepEqual(tap(3), [['fx', 0]], 'both round to chorus');
  assert.deepEqual(tap(4), [['fattack', 0]], 'follows attack');
  assert.deepEqual(tap(5), [['fdecay', params.fdecay!.def]], 'fdecay to its default');
  assert.deepEqual(tap(7), [['mono', 0]]);
  assert.equal(ENCODERS[1]!.hold?.key, 'octave', 'E2 held turns the octave');
});

test('values read the way the panel says them', () => {
  const s = snap({});
  const words = (key: string, v: number) => say(params[key]!, v, s, { describe });
  assert.equal(words('cutoff', 412), '41 %');
  assert.equal(words('release', 420), '420 ms');
  assert.equal(words('shape', 4), 'saw + sub');
  assert.equal(words('octave', 1), '+1');
  assert.equal(words('sound', 1), 'Grand Piano', 'the sound by its name on the card');
  assert.equal(words('fattack', 0), 'follows attack');
  assert.equal(words('hpf', 0), 'off');
  assert.equal(words('hpf', 120), '120 Hz');
});

test('an encoder row reads out what it turns and what its tap set', () => {
  const s = snap({ shape: 1, layer: 2, detune: 8, octave: -1, hpf: 120, lfo: 1, mix: 25, fx: 2,
                   sound: 1, fattack: 0, fdecay: 400, fsustain: 25, glide: 80, mono: 1 });
  const [e1, e2, e3, e4, e5, e6, e7, e8] = ENCODERS.map((_, n) => encoder(n, s)!);
  assert.deepEqual([e1.name, e1.value, e1.extra], ['sound', 'Grand Piano', 'both'], 'both layers: E1 turns the sound');
  assert.deepEqual([e2.name, e2.value, e2.extra], ['detune', '8 cents', 'octave -1']);
  assert.deepEqual([e3.name, e3.value, e3.extra], ['high-pass', '120 Hz', 'LFO on']);
  assert.deepEqual([e4.name, e4.value, e4.extra], ['effects mix', '25 %', 'both']);
  assert.deepEqual([e5.name, e5.value], ['filter attack', 'follows attack']);
  assert.deepEqual([e6.value, e7.value], ['400 ms', '25 %']);
  assert.deepEqual([e8.name, e8.value, e8.extra], ['glide', '80 ms', 'mono']);
  assert.equal(encoder(7, snap({ glide: 0, mono: 0 }))!.extra, 'poly');
  assert.equal(encoder(1, snap({ detune: 0, octave: 2 }))!.extra, 'octave +2');
  assert.equal(encoder(8, s), null, 'a third encoder bank drives nothing');
});

test('E1 turns the shape in synth layer only, the sound otherwise', () => {
  const e1 = encoder(0, snap({ layer: 1, shape: 0, sound: 1 }))!;
  assert.deepEqual([e1.name, e1.value], ['sound', 'Grand Piano']);
  const synth = encoder(0, snap({ layer: 0, shape: 1, sound: 1 }))!;
  assert.deepEqual([synth.name, synth.value], ['shape', 'square']);
});

test('a value the Rodeo does not send reads as absent, never as zero', () => {
  const e2 = encoder(1, snap({}))!;
  assert.equal(e2.value, 'not reported');
  assert.equal(e2.extra, '', 'and no octave either');
});

test('the readout says the preset 1-based, as the panel does', () => {
  const chips = Object.fromEntries(readout(snap({ layer: 1 })));
  assert.equal(chips['preset 2'], 'bass');
  assert.equal(chips.layer, 'sampler');
  assert.equal(chips.card, 'card mounted');
  assert.deepEqual(readout({ ...snap({}), hello: { ...snap({}).hello!, fw: 'other' } } as Snapshot), [], 'another firmware gets none');
});
