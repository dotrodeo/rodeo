// A clock, a step sequencer and an arpeggiator.
//
// Unlike card.js and midi.js this is not a copy of anything in the firmware —
// Rodeo has no sequencer and no arp, and is not getting one. This is the page
// playing the instrument, so the only firmware fact it has to respect is the
// one in midi.js: eight voices, and the ninth note steals.
//
// Everything here is arithmetic on numbers, which is the point: the scheduling
// in live.js is a dozen lines around these functions, and these are tested.

export const DIVISIONS = [
  { id: '1/4', beats: 1 },
  { id: '1/8', beats: 0.5 },
  { id: '1/8T', beats: 1 / 3 },
  { id: '1/16', beats: 0.25 },
  { id: '1/16T', beats: 1 / 6 },
  { id: '1/32', beats: 0.125 },
];

export const ARP_MODES = ['off', 'up', 'down', 'up-down', 'as played', 'random'];

export const BPM_MIN = 40;
export const BPM_MAX = 240;
/** Past this the swung step would land on the one after it. */
export const SWING_MAX = 0.75;

const clamp = (x, lo, hi) => (x < lo ? lo : x > hi ? hi : x);

/** One step, in ms. */
export function stepMs(bpm, beats) {
  return (60000 / clamp(bpm, BPM_MIN, BPM_MAX)) * beats;
}

/**
 * Swing delays every second step, which is the whole of it. `amount` is a
 * fraction of one step, capped so a swung step can never reach the next one.
 */
export function swingOffset(index, ms, amount) {
  return index % 2 ? ms * clamp(amount, 0, SWING_MAX) : 0;
}

/** When step `index` of an endless run starting at `t0` sounds. */
export function stepTime(t0, index, ms, swing) {
  return t0 + index * ms + swingOffset(index, ms, swing);
}

// ------------------------------------------------------------------- the arp

/**
 * The notes an arpeggiator cycles through, held notes expanded over `octaves`.
 * `up-down` does not repeat its endpoints, so three notes give four steps, not
 * six — the usual convention, and the one that sounds like an arpeggio rather
 * than a stutter at the top.
 */
export function arpSequence(notes, mode, octaves = 1) {
  if (!notes.length || mode === 'off') return [];
  const n = clamp(Math.round(octaves), 1, 4);
  const played = [...notes];
  const up = [...new Set(played)].sort((a, b) => a - b);
  const base = mode === 'as played' || mode === 'random' ? played : up;
  const spread = [];
  for (let o = 0; o < n; o++) for (const x of base) spread.push(x + 12 * o);
  if (mode === 'down') return [...spread].reverse();
  if (mode === 'up-down') {
    if (spread.length < 3) return spread;
    return spread.concat(spread.slice(1, -1).reverse());
  }
  return spread;
}

/** Which note of the cycle step `i` plays. Random picks, it does not shuffle. */
export function pickStep(sequence, i, mode, rnd = Math.random) {
  if (!sequence.length) return null;
  if (mode === 'random') return sequence[Math.floor(rnd() * sequence.length) % sequence.length];
  return sequence[((i % sequence.length) + sequence.length) % sequence.length];
}

// ------------------------------------------------------------- the sequencer

export const ROWS = 8;
export const STEPS = 16;

/** Rows are chromatic from `root` up, which is how a kit maps to keys. */
export function rowNote(root, row) {
  return root + (ROWS - 1 - row); // row 0 is the top one, as on a piano roll
}

export function emptyPattern(rows = ROWS, steps = STEPS) {
  return Array.from({ length: rows }, () => new Uint8Array(steps));
}

/**
 * What a step sounds: the notes switched on in that column, and how long each
 * should last. `gate` is a fraction of the step, floored so a note is never so
 * short that the Rodeo's envelope cannot open.
 */
export function stepNotes(pattern, step, root) {
  const out = [];
  for (let r = 0; r < pattern.length; r++) if (pattern[r][step % pattern[r].length]) out.push(rowNote(root, r));
  return out;
}

export function gateMs(ms, gate) {
  return Math.max(15, ms * clamp(gate, 0.05, 1));
}

/** True when any step is on, for deciding whether the transport has work. */
export function hasSteps(pattern) {
  return pattern.some((row) => row.some((v) => v));
}
