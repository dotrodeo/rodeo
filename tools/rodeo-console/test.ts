// npm test, in tools/rodeo-console (node --test test.ts; Node strips the types)
//
// What the console sends and how it reads what comes back. The protocol's own
// tests are the SDK's (rodeo-sdk/host-js); rodeo-demo's are its editor's
// (instruments/rodeo-demo/editor).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { Param, Snapshot } from '@dotrodeo/rodeo-host';
import type { Satellite } from './src/chain.ts';

// --------------------------------------------------------------------- MIDI

import {
  BEND_SEMITONES,
  allNotesOff,
  isRodeoPort,
  noteName,
  noteOff,
  noteOn,
  pitchBend,
  sustain,
} from './src/midi.ts';

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
  const back = ([, lo, hi]: number[]) => (2 * ((lo | (hi << 7)) - 8192)) / 8192;
  assert.deepEqual(pitchBend(0), [0xe0, 0, 64], 'centre is 8192');
  assert.equal(back(pitchBend(0)), 0);
  assert.equal(back(pitchBend(-BEND_SEMITONES)), -2, 'the bottom is reachable exactly');
  for (const s of [-1.5, -0.25, 0.5, 1]) assert.ok(Math.abs(back(pitchBend(s)) - s) < 1e-3, String(s));
  // raw 16383 is +1.99975 semitones: +2 cannot be sent, and the console must
  // not pretend otherwise by wrapping to 0 or overflowing the byte.
  const top = pitchBend(BEND_SEMITONES);
  assert.deepEqual(top, [0xe0, 0x7f, 0x7f]);
  assert.ok(back(top) < 2 && back(top) > 1.999);
  assert.deepEqual(pitchBend(99), top, 'out of range clamps to the top');
});

test('a port is a Rodeo by the word in its name', () => {
  assert.ok(isRodeoPort('Rodeo MIDI'));
  assert.ok(isRodeoPort('Rodeo Host Example'));
  assert.ok(isRodeoPort('Rodeo Demo'));
  assert.ok(isRodeoPort('rodeo demo midi 1'));
  assert.ok(!isRodeoPort('IAC Driver Bus 1'));
  assert.ok(!isRodeoPort(''));
  assert.ok(!isRodeoPort(undefined));
});

test('notes are named with C4 at 60', () => {
  assert.equal(noteName(60), 'C4');
  assert.equal(noteName(36), 'C2');
  assert.equal(noteName(61), 'C#4');
  assert.equal(noteName(0), 'C-1');
});

// ---------------------------------------------------- the clock and the arp

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

//
// Unlike everything above, src/seq.ts is a copy of nothing: Rodeo has no
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
} from './src/seq.ts';

test('a step is as long as the tempo says', () => {
  const by = (id: string) => DIVISIONS.find((d) => d.id === id)!.beats;
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
  const at = (i: number) => stepTime(1000, i, ms, 0.5);
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
    assert.ok(cycle.includes(n as number), `${r} gave ${n}`);
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

// ------------------------------------------------------------- the chain
//
// The host protocol's chain report (rodeo-sdk/common/host.cpp). The report
// below is the shape that code emits; when the firmware changes it, these are
// what should fail.

import {
  CHAIN_STATES,
  FAULTS,
  breakAfter,
  faultText,
  kindInput,
  kindName,
  kindSlots,
  kindSupported,
  places,
  potPercent,
  scanHealth,
  slots,
  takeLines,
} from './src/chain.ts';

const REPORT = {
  state: 'scan',
  sats: 2,
  broken: -1,
  s: [
    { kind: 5, fw: '1.0', in: [412, 900, 77, 512] },
    { kind: 6, fw: '1.0', in: [3, -1, 0, 0], btn: [0, 1, 0, 0] },
  ],
  good: 24581,
  bad: 0,
};

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
  assert.equal(kindInput(14), null, 'a custom board declares its own slots; the report cannot say');
  assert.equal(kindSupported(7), true);
  assert.equal(kindSupported(14), false, 'custom boards bring their own firmware');
  assert.equal(kindSupported(15), true);
});

test('slots are drawn by what the board actually is', () => {
  const pots = slots(REPORT.s[0]);
  assert.equal(pots.length, 4);
  assert.ok(pots.every((s) => s.type === 'pot'));
  assert.equal(potPercent(pots[1].value), 90);
  assert.equal(potPercent(0), 0);
  assert.equal(potPercent(1000), 100);

  const encs = slots(REPORT.s[1]);
  assert.ok(encs.every((s) => s.type === 'encoder'));
  assert.equal(encs[0].value, 3, 'a running detent total, which can go either way');
  assert.equal(encs[1].value, -1);
  assert.equal(encs[1].pressed, true, 'btn[] is how an encoder reports its push switch');
  assert.equal(encs[0].pressed, false);

  // A button bank has no btn[]: its presses are already in in[].
  const buttons = slots({ kind: 7, fw: '1.0', in: [0, 1, 0, 0] });
  assert.ok(buttons.every((s) => s.type === 'button'));
  assert.equal(buttons[1].pressed, true);
  // A custom board's numbers are shown as numbers.
  assert.equal(slots({ kind: 14, fw: '1.0', in: [7] })[0].type, null);
  assert.deepEqual(slots({}), [], 'a satellite with no in[] draws nothing, and does not throw');
});

test('a faulted slot outranks whatever its board kind would draw', () => {
  const sat: Satellite = { kind: 5, fw: '1.0', in: [500, 0, 0, 0], fault: [null, [1, 47], null, [2, 9]] };
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

test('pots and encoders are numbered across the chain, in ring order', () => {
  // A parameter's `pot` in the hello names one of these: pot 1 is the first
  // pot slot on the chain, whatever comes before it.
  const c = {
    s: [
      { kind: 7, fw: '1.0', in: [0, 0, 0, 0] },
      { kind: 5, fw: '1.0', in: [1, 2, 3, 4] },
      { kind: 6, fw: '1.0', in: [0, 0, 0, 0] },
      { kind: 1, fw: '1.0', in: [5] },
    ],
  };
  const at = places(c);
  assert.deepEqual(at.pots.map((p) => [p.sat, p.slot, p.n]), [[1, 0, 0], [1, 1, 1], [1, 2, 2], [1, 3, 3], [3, 0, 4]]);
  assert.deepEqual(at.encs.map((p) => p.sat), [2, 2, 2, 2]);
  assert.deepEqual(places({}), { pots: [], encs: [] });
});

// ---------------------------------------------------------- describe

import { describe as say } from './src/describe.ts';

const P = (over: Partial<Param>): Param => ({ id: 0, key: 'x', min: 0, max: 100, def: 0, ...over });
const SNAP = {} as Snapshot;

test('a value reads in the units the hello gives', () => {
  assert.equal(say(P({ unit: '‰', max: 1000 }), 412, SNAP), '41 %', 'per mille shows as percent');
  assert.equal(say(P({ unit: 'ms' }), 420, SNAP), '420 ms');
  assert.equal(say(P({ names: ['saw', 'square'], max: 1 }), 1, SNAP), 'square');
  assert.equal(say(P({ names: ['a'], min: 3, max: 3 }), 3, SNAP), 'a', 'names count from min');
  assert.equal(say(P({ min: -2, max: 2 }), 1, SNAP), '+1', 'a range through zero is signed');
  assert.equal(say(P({}), 1, SNAP), '1');
});

test("a firmware's own words come first", () => {
  const profile = { describe: (p: Param, v: number) => (v === 0 ? 'off' : null) };
  assert.equal(say(P({ unit: 'Hz' }), 0, SNAP, profile), 'off');
  assert.equal(say(P({ unit: 'Hz' }), 80, SNAP, profile), '80 Hz', 'null falls back');
});
