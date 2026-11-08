// The MIDI client, ported from the phone app (instruments/rodeo-demo/app, on
// the rodeo-mobile-app branch). npm test runs it with the rest.
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { RodeoClient, type Timers } from './client.ts';
import { MidiParser } from './parser.ts';
import { Cmd, type Param, message, parseReply, pitchBend, setMessage } from './protocol.ts';
import { DEFAULTS, describe, paramsToPreset, presetDefaults, presetToParams, toParam } from './audition.ts';
import { KEYS } from '../preset.ts';

test('parser: messages split across packets, running status, real time inside SysEx', () => {
  const got: number[][] = [];
  const p = new MidiParser((m) => got.push(m));
  p.push([0x90, 60]);
  p.push([100, 62, 90]); // running status
  p.push([0xf0, 0x7d, 0xf8, 0x52]); // a clock in the middle of SysEx
  p.push([0x44, 0xf7, 0xc0, 5, 0xb0, 7, 64]);
  assert.deepEqual(got, [
    [0x90, 60, 100],
    [0x90, 62, 90],
    [0xf8],
    [0xf0, 0x7d, 0x52, 0x44, 0xf7],
    [0xc0, 5],
    [0xb0, 7, 64],
  ]);
});

test('parser: an unfinished SysEx is dropped when a status byte arrives', () => {
  const got: number[][] = [];
  const p = new MidiParser((m) => got.push(m));
  p.push([0xf0, 1, 2, 0x80, 60, 0]);
  assert.deepEqual(got, [[0x80, 60, 0]]);
});

const cutoff = { id: 0, key: 'cutoff', min: 0, max: 1000, unit: '‰', pot: 1 };
const octave = { id: 6, key: 'octave', min: -2, max: 2 };

test('protocol: set messages subtract the minimum, 14-bit LSB first, clamped', () => {
  assert.deepEqual(setMessage([[cutoff, 1000], [octave, -1]]), [
    0xf0, 0x7d, 0x52, 0x44, 1, Cmd.set, 0, 1000 & 0x7f, 1000 >> 7, 6, 1, 0, 0xf7,
  ]);
  assert.deepEqual(setMessage([[octave, 9]]).slice(6, 9), [6, 4, 0]);
});

test('protocol: pitch bend spans -2..+2 semitones', () => {
  assert.deepEqual(pitchBend(0), [0xe0, 0, 64]);
  assert.deepEqual(pitchBend(2), [0xe0, 0x7f, 0x7f]);
  assert.deepEqual(pitchBend(-2), [0xe0, 1, 0]);
});

function reply(cmd: number, body: unknown): number[] {
  const text = JSON.stringify(body);
  return message(cmd, [...text].map((c) => c.charCodeAt(0)));
}

test('protocol: replies parse; other SysEx and versions do not', () => {
  assert.deepEqual(parseReply(reply(Cmd.said, { said: 'hi' })), { cmd: Cmd.said, body: { said: 'hi' } });
  assert.equal(parseReply([0xf0, 0x7e, 0x7f, 6, 2, 0, 0xf7]), null);
  const v2 = reply(Cmd.said, {});
  v2[4] = 2;
  assert.equal(parseReply(v2), null);
});

// A clock the tests move by hand
class FakeTimers implements Timers {
  t = 0;
  private next = 1;
  private jobs = new Map<number, { at: number; every: number; f: () => void }>();
  setInterval(f: () => void, ms: number) {
    const id = this.next++;
    this.jobs.set(id, { at: this.t + ms, every: ms, f });
    return id;
  }
  setTimeout(f: () => void, ms: number) {
    const id = this.next++;
    this.jobs.set(id, { at: this.t + ms, every: 0, f });
    return id;
  }
  clearInterval(id: unknown) {
    this.jobs.delete(id as number);
  }
  clearTimeout(id: unknown) {
    this.jobs.delete(id as number);
  }
  advance(ms: number) {
    const end = this.t + ms;
    for (;;) {
      let first: [number, { at: number; every: number; f: () => void }] | null = null;
      for (const e of this.jobs) if (e[1].at <= end && (!first || e[1].at < first[1].at)) first = e;
      if (!first) break;
      const [id, job] = first;
      this.t = job.at;
      if (job.every) job.at += job.every;
      else this.jobs.delete(id);
      job.f();
    }
    this.t = end;
  }
}

function rig() {
  const timers = new FakeTimers();
  const sent: number[][] = [];
  const client = new RodeoClient(timers);
  client.attach({ send: (b) => (sent.push(b), true), now: () => timers.t });
  const cmds = () => sent.filter((m) => m[0] === 0xf0).map((m) => m[5]);
  return { timers, sent, client, cmds };
}

const hello = {
  fw: 'rodeo-demo',
  proto: 1,
  build: 'test',
  voices: 8,
  params: [cutoff, octave],
};

test('client: hello until answered, then the lists and the stream', () => {
  const { timers, client, cmds } = rig();
  assert.equal(client.snapshot.status, 'connecting');
  timers.advance(1100);
  assert.deepEqual(cmds(), [Cmd.hello, Cmd.hello, Cmd.hello]);
  client.receive(reply(Cmd.helloReply, hello));
  assert.equal(client.snapshot.status, 'ready');
  assert.deepEqual(cmds().slice(3), [Cmd.presets, Cmd.sounds, Cmd.stream]);
  timers.advance(4000); // keepalive every 2 s, no more hellos
  assert.deepEqual(cmds().slice(6), [Cmd.stream, Cmd.stream]);
});

test('client: no answer means not a Rodeo', () => {
  const { timers, client } = rig();
  timers.advance(5000);
  assert.equal(client.snapshot.status, 'not-rodeo');
});

test('client: a drag sends at most every 20 ms per parameter, the latest value', () => {
  const { timers, client, sent } = rig();
  client.receive(reply(Cmd.helloReply, hello));
  sent.length = 0;
  for (let v = 0; v <= 100; v += 10) {
    client.set('cutoff', v);
    timers.advance(4);
  }
  timers.advance(50);
  const sets = sent.filter((m) => m[5] === Cmd.set).map((m) => m[7] | (m[8] << 7));
  assert.ok(sets.length >= 2 && sets.length <= 4, `sent ${sets.length}`);
  assert.equal(sets[sets.length - 1], 100);
});

test('client: leaving the foreground silences notes and stops the stream', () => {
  const { timers, client, sent, cmds } = rig();
  client.receive(reply(Cmd.helloReply, hello));
  client.noteOn(60, 100);
  sent.length = 0;
  client.setActive(false);
  assert.deepEqual(sent[0], [0xb0, 123, 0]);
  assert.deepEqual(sent[1].slice(5, 7), [Cmd.stream, 0]);
  timers.advance(10000);
  assert.equal(cmds().length, 1); // no keepalives while away
  client.setActive(true);
  assert.deepEqual(cmds().slice(1), [Cmd.stream]);
});

test('client: state and said update the snapshot', () => {
  const { client } = rig();
  let seen = 0;
  client.subscribe(() => seen++);
  client.receive(reply(Cmd.helloReply, hello));
  client.receive(reply(Cmd.stateReply, { preset: 1, name: 'pad', p: { cutoff: 400 } }));
  client.receive(reply(Cmd.said, { said: 'cutoff: 40 %' }));
  assert.equal(client.snapshot.state?.name, 'pad');
  assert.equal(client.snapshot.said?.text, 'cutoff: 40 %');
  assert.equal(seen, 3);
});

// ------------------------------------------------- hearing the preset editor

const P = (key: string, min: number, max: number, unit = '', names?: string[]): Param =>
  ({ id: 0, key, min, max, unit, ...(names ? { names } : {}) });
const params: Record<string, Param> = {
  cutoff: P('cutoff', 0, 1000, '‰'),
  release: P('release', 5, 4000, 'ms'),
  shape: P('shape', 0, 6, '', ['saw', 'square', 'pulse', 'triangle', 'saw + sub', 'square + sub', 'fm']),
  lfo: P('lfo', 0, 1, '', ['off', 'on']),
  octave: P('octave', -2, 2),
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

test('audition: values read the way the panel says them', () => {
  assert.equal(describe(params.cutoff!, 412), '41 %');
  assert.equal(describe(params.release!, 420), '420 ms');
  assert.equal(describe(params.shape!, 4), 'saw + sub');
  assert.equal(describe(params.octave!, 1), '+1');
  assert.equal(describe(params.sound!, 1, sounds), 'Grand Piano');
  assert.equal(describe(P('fattack', 0, 4000, 'ms'), 0), 'follows attack');
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
  if (params.mix) assert.equal(got.mix, DEFAULTS.mix, 'blank counts as left out');
  for (const key of Object.keys(DEFAULTS)) assert.ok(!KEYS[key]?.pot, `${key} is not a pot's`);
});
