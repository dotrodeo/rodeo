// The host protocol's computer side: the parser, the messages, the client, and
// rodeo-demo's part, against a fake Rodeo and a clock moved by hand.
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { RodeoClient, type Timers } from './client.ts';
import { MidiParser } from './parser.ts';
import { Cmd, HEADER, message, parseReply, pitchBend, setMessage } from './protocol.ts';
import { DemoCmd, demoState, followRodeoDemo, loadPreset, presets, sounds } from './rodeo-demo.ts';

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

const cutoff = { id: 0, key: 'cutoff', min: 0, max: 1000, def: 600, unit: '‰', pot: 1 };
const octave = { id: 6, key: 'octave', min: -2, max: 2, def: 0 };

test('protocol: set messages subtract the minimum, 14-bit LSB first, clamped', () => {
  assert.deepEqual(setMessage([[cutoff, 1000], [octave, -1]]), [
    ...HEADER, 2, Cmd.set, 0, 1000 & 0x7f, 1000 >> 7, 6, 1, 0, 0xf7,
  ]);
  const p = HEADER.length + 2; // past the header, the version, the command
  assert.deepEqual(setMessage([[octave, 9]]).slice(p, p + 3), [6, 4, 0]);
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
  const v1 = reply(Cmd.said, {});
  v1[HEADER.length] = 1; // rodeo-demo before the SDK's protocol
  assert.equal(parseReply(v1), null);
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

function rig(demo = false) {
  const timers = new FakeTimers();
  const sent: number[][] = [];
  const client = new RodeoClient(timers);
  if (demo) followRodeoDemo(client);
  client.attach({ send: (b) => (sent.push(b), true), now: () => timers.t });
  const cmds = () => sent.filter((m) => m[0] === 0xf0).map((m) => m[5]);
  return { timers, sent, client, cmds };
}

const hello = {
  fw: 'rodeo-demo',
  proto: 2,
  build: 'test',
  sdk: '0.2.0',
  chain: 1,
  voices: 8,
  params: [cutoff, octave],
};

test('client: hello until answered, then rodeo-demo\'s lists and the stream', () => {
  const { timers, client, cmds } = rig(true);
  assert.equal(client.snapshot.status, 'connecting');
  timers.advance(1100);
  assert.deepEqual(cmds(), [Cmd.hello, Cmd.hello, Cmd.hello]);
  client.receive(reply(Cmd.helloReply, hello));
  assert.equal(client.snapshot.status, 'ready');
  assert.deepEqual(cmds().slice(3), [DemoCmd.presets, DemoCmd.sounds, Cmd.stream]);
  timers.advance(4000); // keepalive every 2 s, no more hellos
  assert.deepEqual(cmds().slice(6), [Cmd.stream, Cmd.stream]);
});

test('client: another firmware is asked for nothing of rodeo-demo\'s', () => {
  const { client, cmds } = rig(true);
  client.receive(reply(Cmd.helloReply, { ...hello, fw: 'host-example', voices: undefined }));
  assert.equal(client.snapshot.status, 'ready');
  assert.deepEqual(cmds().slice(1), [Cmd.stream]);
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

test('client: a firmware\'s own replies, and rodeo-demo\'s reading of them', () => {
  const { client, sent } = rig(true);
  client.receive(reply(Cmd.helloReply, hello));
  client.receive(reply(DemoCmd.presets + 0x40, { presets: ['bass', 'lead'], current: 1 }));
  client.receive(reply(DemoCmd.sounds + 0x40, { sounds: [{ name: 'basic', kit: 1, ready: 1 }] }));
  client.receive(reply(Cmd.stateReply, { p: {}, hold: 0, cpu: 3, preset: 1, name: 'lead' }));
  const s = client.snapshot;
  assert.deepEqual(presets(s), ['bass', 'lead']);
  assert.deepEqual(sounds(s).map((x) => x.name), ['basic']);
  assert.equal(demoState(s)?.name, 'lead');
  sent.length = 0;
  loadPreset(client, 130);
  assert.deepEqual(sent[0], message(DemoCmd.load, [130 & 0x7f, 130 >> 7]));
  assert.throws(() => client.request(Cmd.dfu), RangeError, 'the SDK\'s commands have their own methods');

  client.receive(reply(Cmd.helloReply, { ...hello, fw: 'host-example' }));
  assert.deepEqual(presets(client.snapshot), [], 'a new hello forgets the last firmware\'s replies');
  assert.equal(demoState(client.snapshot), null);
});

test('client: the chain streams when asked, and dfu is one message', () => {
  const { timers, client, cmds, sent } = rig();
  client.receive(reply(Cmd.helloReply, hello));
  sent.length = 0;
  client.streamChain(true);
  assert.deepEqual(sent[0], message(Cmd.stream, [3]), 'state and chain, straight away');
  timers.advance(2000);
  assert.deepEqual(sent[1], message(Cmd.stream, [3]), 'and in the keepalive');
  const chain = { state: 'scan', sats: 1, broken: -1, s: [{ kind: 5, fw: '1.0', in: [1, 2, 3, 4] }], good: 9, bad: 0 };
  client.receive(reply(Cmd.chainReply, chain));
  assert.deepEqual(client.snapshot.chain, chain);
  client.streamChain(false);
  assert.equal(client.snapshot.chain, null);
  assert.deepEqual(sent.at(-1), message(Cmd.stream, [1]));
  client.dfu();
  assert.equal(cmds().at(-1), Cmd.dfu);
});
