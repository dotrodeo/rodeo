// rodeo-demo's profile for the console (profile.ts): its words, its encoders,
// its readout. The table below is params.cpp's, as the hello sends it.
import assert from 'node:assert/strict';
import { test } from 'node:test';

import type { Param, Snapshot } from '@dotrodeo/rodeo-host';
import { describe as say } from 'rodeo-console/describe';
import { ENCODERS, GROUPS, describe, encoder, readout } from './profile.ts';

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

test('every parameter in the table has a place on the panel', () => {
  const placed = GROUPS.flatMap(([, keys]) => keys);
  for (const p of TABLE) assert.ok(placed.includes(p.key), p.key);
  assert.equal(new Set(placed).size, placed.length, 'and only one');
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
  assert.deepEqual([e1.name, e1.value, e1.extra], ['shape', 'square', 'both']);
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

test('in sampler layer E1 picks the sound instead of the shape', () => {
  const e1 = encoder(0, snap({ layer: 1, shape: 0, sound: 1 }))!;
  assert.deepEqual([e1.name, e1.value], ['sound', 'Grand Piano']);
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
