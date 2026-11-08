// A clock, a step sequencer and an arpeggiator.
//
// Unlike card.ts and midi.ts this is not a copy of anything in the firmware —
// Rodeo has no sequencer and no arp, and is not getting one. This is the page
// playing the instrument, so the only firmware fact it has to respect is the
// one in midi.ts: eight voices, and the ninth note steals.
//
// Everything here is arithmetic on numbers, which is the point: the scheduling
// in live.ts is a dozen lines around these functions, and these are tested.

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

// ------------------------------------------------------------- the sequencer

export const ROWS = 8;
export const STEPS = 16;

/** Rows are chromatic from `root` up, which is how a kit maps to keys. */
/** Rows of steps, 1 for on; row 0 is the top. */
export type Pattern = Uint8Array[];

export function rowNote(root: number, row: number): number {
  return root + (ROWS - 1 - row); // row 0 is the top one, as on a piano roll
}

export function emptyPattern(rows = ROWS, steps = STEPS): Pattern {
  return Array.from({ length: rows }, () => new Uint8Array(steps));
}

/**
 * What a step sounds: the notes switched on in that column, and how long each
 * should last. `gate` is a fraction of the step, floored so a note is never so
 * short that the Rodeo's envelope cannot open.
 */
export function stepNotes(pattern: Pattern, step: number, root: number): number[] {
  const out: number[] = [];
  for (let r = 0; r < pattern.length; r++) if (pattern[r][step % pattern[r].length]) out.push(rowNote(root, r));
  return out;
}

export function gateMs(ms: number, gate: number): number {
  return Math.max(15, ms * clamp(gate, 0.05, 1));
}

/** True when any step is on, for deciding whether the transport has work. */
export function hasSteps(pattern: Pattern): boolean {
  return pattern.some((row) => row.some((v) => v));
}
