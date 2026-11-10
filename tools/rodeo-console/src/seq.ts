// A clock, a piano roll and an arpeggiator.
//
// Unlike card.ts and midi.ts this is not a copy of anything in the firmware —
// Rodeo has no sequencer and no arp, and is not getting one. This is the page
// playing the instrument, so the only firmware fact it has to respect is the
// one in midi.ts: eight voices, and the ninth note steals.
//
// Everything here is arithmetic on numbers, which is the point: the scheduling
// in console.ts is a few dozen lines around these functions, and these are tested.

/** A step length, in beats. */
export interface Division {
  id: string;
  beats: number;
}

export const DIVISIONS: Division[] = [
  { id: '1/4', beats: 1 },
  { id: '1/8', beats: 0.5 },
  { id: '1/8T', beats: 1 / 3 },
  { id: '1/16', beats: 0.25 },
  { id: '1/16T', beats: 1 / 6 },
  { id: '1/32', beats: 0.125 },
];

export type ArpMode = 'off' | 'up' | 'down' | 'up-down' | 'as played' | 'random';

export const ARP_MODES: ArpMode[] = ['off', 'up', 'down', 'up-down', 'as played', 'random'];

export const BPM_MIN = 40;
export const BPM_MAX = 240;
/** Past this the swung step would land on the one after it. */
export const SWING_MAX = 0.75;

const clamp = (x: number, lo: number, hi: number) => (x < lo ? lo : x > hi ? hi : x);

/** One step, in ms. */
export function stepMs(bpm: number, beats: number): number {
  return (60000 / clamp(bpm, BPM_MIN, BPM_MAX)) * beats;
}

/**
 * Swing delays every second step, which is the whole of it. `amount` is a
 * fraction of one step, capped so a swung step can never reach the next one.
 */
export function swingOffset(index: number, ms: number, amount: number): number {
  return index % 2 ? ms * clamp(amount, 0, SWING_MAX) : 0;
}

/** When step `index` of an endless run starting at `t0` sounds. */
export function stepTime(t0: number, index: number, ms: number, swing: number): number {
  return t0 + index * ms + swingOffset(index, ms, swing);
}

// ------------------------------------------------------------------- the arp

/**
 * The notes an arpeggiator cycles through, held notes expanded over `octaves`.
 * `up-down` does not repeat its endpoints, so three notes give four steps, not
 * six — the usual convention, and the one that sounds like an arpeggio rather
 * than a stutter at the top.
 */
export function arpSequence(notes: readonly number[], mode: ArpMode, octaves = 1): number[] {
  if (!notes.length || mode === 'off') return [];
  const n = clamp(Math.round(octaves), 1, 4);
  const played = [...notes];
  const up = [...new Set(played)].sort((a, b) => a - b);
  const base = mode === 'as played' || mode === 'random' ? played : up;
  const spread: number[] = [];
  for (let o = 0; o < n; o++) for (const x of base) spread.push(x + 12 * o);
  if (mode === 'down') return [...spread].reverse();
  if (mode === 'up-down') {
    if (spread.length < 3) return spread;
    return spread.concat(spread.slice(1, -1).reverse());
  }
  return spread;
}

/** Which note of the cycle step `i` plays. Random picks, it does not shuffle. */
export function pickStep(
  sequence: readonly number[],
  i: number,
  mode: ArpMode,
  rnd: () => number = Math.random
): number | null {
  if (!sequence.length) return null;
  if (mode === 'random') return sequence[Math.floor(rnd() * sequence.length) % sequence.length];
  return sequence[((i % sequence.length) + sequence.length) % sequence.length];
}

// ----------------------------------------------------------- the arp clock

/**
 * Where the arp's first note goes when it starts under a running transport: the
 * next `beats` boundary after `beat`, so a 1/8 arp lands on the roll's 1/8s
 * rather than wherever the key happened to go down. Free-running, it starts now.
 */
export function nextBoundary(beat: number, beats: number): number {
  const k = Math.ceil(beat / beats - 1e-9);
  return k * beats || 0; // not -0
}

// ---------------------------------------------------------- the piano roll

/** The roll covers C0 to C5, with C4 at 60 as midi.ts names them. */
export const NOTE_LO = 12;
export const NOTE_HI = 72;
export const ROWS = NOTE_HI - NOTE_LO + 1;
/** The roll's grid is sixteenths: 16 steps a bar, 2 to 8 bars. */
export const STEP_BEATS = 0.25;
export const STEPS_PER_BAR = 16;
export const BARS_MIN = 2;
export const BARS_MAX = 8;

/** A note on the roll: a pitch, a first step, and a length in steps. */
export interface Note {
  note: number;
  start: number;
  len: number;
}

/**
 * Notes past the end are kept, not dropped, so going from 8 bars to 2 and back
 * loses nothing; they just do not play while the roll is shorter than them.
 */
export interface Roll {
  bars: number;
  notes: Note[];
}

export const clampBars = (bars: number) => clamp(Math.round(bars) || BARS_MIN, BARS_MIN, BARS_MAX);

export function emptyRoll(bars = 4): Roll {
  return { bars: clampBars(bars), notes: [] };
}

export function rollSteps(roll: Roll): number {
  return clampBars(roll.bars) * STEPS_PER_BAR;
}

/** Row 0 is the top, as on any piano roll. */
export const rowNote = (row: number) => NOTE_HI - row;
export const noteRow = (note: number) => NOTE_HI - note;
export const onRoll = (note: number) => note >= NOTE_LO && note <= NOTE_HI;

/** How many steps a note sounds this time round: cut where the loop ends. */
export function playedLen(roll: Roll, n: Note): number {
  return Math.max(0, Math.min(n.len, rollSteps(roll) - n.start));
}

/** The notes that start on step `index` of an endless run, highest first. */
export function startsAt(roll: Roll, index: number): Note[] {
  const step = index % rollSteps(roll);
  return roll.notes.filter((n) => n.start === step && playedLen(roll, n) > 0).sort((a, b) => b.note - a.note);
}

/** The note under a cell; the last placed wins, as it is the one drawn on top. */
export function noteAt(roll: Roll, note: number, step: number): Note | undefined {
  for (let i = roll.notes.length - 1; i >= 0; i--) {
    const n = roll.notes[i];
    if (n.note === note && step >= n.start && step < n.start + n.len) return n;
  }
  return undefined;
}

/**
 * Settle `n` on the roll: a note of the same pitch it overlaps is shortened to
 * end where `n` starts, or removed if `n` covers its start. Two overlapping
 * notes of one pitch would send on, on, off, off — and the first off would cut
 * the second note short.
 */
export function place(roll: Roll, n: Note): void {
  n.note = clamp(n.note, NOTE_LO, NOTE_HI);
  n.start = Math.max(0, n.start);
  n.len = Math.max(1, n.len);
  const end = n.start + n.len;
  roll.notes = roll.notes.filter((o) => {
    if (o === n || o.note !== n.note) return true;
    if (o.start + o.len <= n.start || o.start >= end) return true;
    if (o.start < n.start) {
      o.len = n.start - o.start;
      return true;
    }
    return false;
  });
  if (!roll.notes.includes(n)) roll.notes.push(n);
}

/** A gap before every note off, so a note followed by its own pitch retriggers. */
export const LEGATO_GAP = 4;

/**
 * How long a note starting on step `start` holds, in ms: its steps, with swing
 * moving both of its ends, less the gap — and never so short the envelope
 * cannot open.
 */
export function holdMs(start: number, len: number, ms: number, swing: number): number {
  const span = len * ms + swingOffset(start + len, ms, swing) - swingOffset(start, ms, swing);
  return Math.max(15, span - LEGATO_GAP);
}

export function gateMs(ms: number, gate: number): number {
  return Math.max(15, ms * clamp(gate, 0.05, 1));
}

export function hasNotes(roll: Roll): boolean {
  return roll.notes.some((n) => playedLen(roll, n) > 0);
}
