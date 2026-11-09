// The preset editor heard on the Rodeo (audition.ts).
import assert from 'node:assert/strict';
import { test } from 'node:test';

import type { Param } from '@dotrodeo/rodeo-host';
import { paramsToPreset, presetDefaults, presetToParams, toParam } from './audition.ts';

const P = (key: string, min: number, max: number, unit = '', names?: string[], def = min, pot?: number): Param =>
  ({ id: 0, key, min, max, def, unit, ...(names ? { names } : {}), ...(pot ? { pot } : {}) });
const params: Record<string, Param> = {
  cutoff: P('cutoff', 0, 1000, '‰', undefined, 600, 1),
  release: P('release', 5, 4000, 'ms'),
  shape: P('shape', 0, 6, '', ['saw', 'square', 'pulse', 'triangle', 'saw + sub', 'square + sub', 'fm']),
  lfo: P('lfo', 0, 1, '', ['off', 'on']),
  octave: P('octave', -2, 2),
  mix: P('mix', 0, 100, '%', undefined, 25),
  sound: P('sound', 0, 2),
};
const sounds = [
  { name: 'bass', kit: 0, ready: 1 },
  { name: 'Grand Piano', kit: 0, ready: 1 },
];

test('audition: a preset file value becomes the parameter the protocol takes', () => {
  assert.deepEqual(toParam('cutoff', 41, params, sounds), { value: 410 }, '% in the file, ‰ on the wire');
  assert.deepEqual(toParam('release', 420, params, sounds), { value: 420 });
  assert.deepEqual(toParam('shape', 'Saw + Sub', params, sounds), { value: 4 }, 'squashed, as presets.cpp does');
  assert.deepEqual(toParam('lfo', 'on', params, sounds), { value: 1 }, 'by the Rodeo\'s own names, not the file\'s order');
  assert.deepEqual(toParam('octave', -1, params, sounds), { value: -1 });
  assert.deepEqual(toParam('sound', 'grandpiano', params, sounds), { value: 1 });
  assert.ok('why' in toParam('sound', 'cello', params, sounds));
  assert.ok('why' in toParam('wobble', 3, params, sounds));
});

test('audition: a whole preset, with what cannot be sent said rather than dropped', () => {
  const { sets, skipped } = presetToParams(
    { cutoff: 50, shape: 'fm', sound: 'cello', mix: undefined, keytrack: 20 },
    params,
    sounds
  );
  assert.deepEqual(sets, [['cutoff', 500], ['shape', 6]]);
  assert.equal(skipped.length, 2);
});

test('audition: what the Rodeo plays comes back as a preset, and round-trips', () => {
  const p = { cutoff: 412, release: 420, shape: 4, lfo: 1, octave: -1, sound: 1 };
  const values = paramsToPreset(p, params, sounds);
  assert.deepEqual(values, {
    cutoff: 41, release: 420, shape: 'saw + sub', lfo: 'on', octave: -1, sound: 'Grand Piano',
  });
  const { sets, skipped } = presetToParams(values, params, sounds);
  assert.deepEqual(skipped, []);
  assert.deepEqual(Object.fromEntries(sets), { ...p, cutoff: 410 }, 'cutoff to the nearest %');
  const fa = { ...params, fattack: P('fattack', 0, 4000, 'ms') };
  assert.equal(paramsToPreset({ fattack: 0 }, fa, sounds).fattack, undefined, 'following attack is left out');
});

test('audition: what a preset leaves out goes back to the default, but not a pot or the sound', () => {
  const got = Object.fromEntries(presetDefaults({ shape: 'fm', mix: '' }, params));
  assert.equal(got.shape, undefined, 'the preset sets it');
  assert.equal(got.cutoff, undefined, 'a pot parameter stays on its pot');
  assert.equal(got.sound, undefined, 'the sound stays');
  for (const key of Object.keys(got)) assert.ok(params[key], `${key} is one the Rodeo has`);
  assert.equal(got.mix, 25, 'its default from hello; blank counts as left out');
  assert.equal(got.octave, -2, 'the default is the parameter\'s def, not 0');
});
