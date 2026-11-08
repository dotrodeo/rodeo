// The `#S` state line, read.
//
// A copy of two things, and both stay authoritative: the line's shape comes
// from `instruments/rodeo-demo/firmware/src/state_line.cpp` and `main.cpp`'s
// `StateLine()`, and the board kinds from `rodeo-sdk/common/sat_frame.h`.
// planning/RODEO_CONSOLE_STATE_LINE.md is the agreed contract and its "As
// built" section lists where the firmware differs from the original ask.
//
// The rule the parser is built around: **a line that does not parse is
// skipped, never guessed at**. The firmware gives up on a line if the computer
// stops reading for 50 ms (main.cpp:165), so a truncated line is normal and
// means nothing is wrong. Half a line of JSON must not become half a screen of
// wrong numbers.

// ------------------------------------------------------------------ the shapes
//
// What `parseStateLine` hands back. It checks only that the line is a JSON
// object, so every field is optional: an older firmware, or a field dropped
// later, simply leaves it out, and the readers below already cope.

/** A FAULT slot: `[code, detail]` (sat_frame.h:108). */
export type Fault = [code?: number, detail?: number];

/** One satellite in `chain.s`, in ring order (state_line.cpp). */
export interface Satellite {
  kind: number;
  fw?: string;
  /** Each slot's value, typed by the board kind. */
  in?: number[];
  /** Push switches, sent only for encoder satellites (state_line.cpp:131). */
  btn?: number[];
  fault?: (Fault | null)[];
}

/** `chain` in the line: the ring as `SatChain` sees it. */
export interface ChainState {
  state?: string;
  sats?: number;
  /** Satellites before the break, or -1 (SatChain::BrokenAfter). */
  broken?: number;
  panel?: number;
  s?: Satellite[];
  good?: number;
  bad?: number;
  error?: string;
}

/**
 * The `#S` line itself (state_line.cpp). The keys the panel reads by name are
 * listed; the index signature is there because `encoderReadout` looks keys up
 * from the PANEL_ENCODERS table.
 */
export interface StateLine {
  [key: string]: unknown;
  preset?: number;
  name?: string;
  layer?: string;
  shape?: string;
  sound?: number;
  nsounds?: number;
  cut?: number;
  res?: number;
  env?: number;
  drv?: number;
  atk?: number;
  dec?: number;
  sus?: number;
  rel?: number;
  det?: number;
  oct?: number;
  hpf?: number;
  lfo?: number;
  mix?: number;
  fx?: number;
  fatk?: number;
  fdec?: number;
  fsus?: number;
  glide?: number;
  mono?: number;
  voices?: number;
  card?: number;
  loaded?: number;
  samples?: number;
  skipped?: number;
  kb?: number;
  loadms?: number;
  chain?: ChainState;
  cpu?: number;
  peak?: number;
}

export type SlotInput = 'pot' | 'button' | 'encoder' | 'switch';

/** A satellite slot, ready to draw. */
export interface Slot {
  index: number;
  type: SlotInput | 'fault' | null;
  value: number;
  pressed: boolean;
  fault: Fault | null;
}

/** Board kinds (sat_frame.h:119-130). 8-13 are kept for future satellites. */
export const KINDS: Record<number, string> = {
  0: 'test fixture',
  1: 'pot',
  2: 'button',
  3: 'encoder',
  4: 'switch',
  5: 'pot bank',
  6: 'encoder bank',
  7: 'button bank',
  14: 'custom',
  15: 'no ID fitted',
};

export function kindName(kind: number): string {
  return KINDS[kind] ?? `kind ${kind}`;
}

/** `sat_kind_slots` — 4 for the banks, 1 otherwise (sat_frame.h:140). */
export function kindSlots(kind: number): 1 | 4 {
  return kind === 5 || kind === 6 || kind === 7 ? 4 : 1;
}

/**
 * `sat_kind_input` — what a board kind's slots report (sat_frame.h:150). A
 * custom board declares its own in DISCOVER and the line cannot say, so this
 * returns null for it and the panel shows the raw number.
 */
export function kindInput(kind: number): SlotInput | null {
  switch (kind) {
    case 1:
    case 5:
      return 'pot';
    case 2:
    case 7:
      return 'button';
    case 3:
    case 6:
      return 'encoder';
    case 4:
      return 'switch';
    default:
      return null;
  }
}

/** `sat_kind_supported` (sat_frame.h:166) — the stock firmware runs these. */
export function kindSupported(kind: number): boolean {
  return kind <= 7 || kind === 15;
}

/** FAULT slot codes, D0 (sat_frame.h:108). */
export const FAULTS: Record<number, string> = {
  1: 'ID resistor reads between two kinds',
  2: 'the stock firmware does not run this board kind',
  3: 'sensor',
};

export function faultText([code, detail]: Fault = []): string {
  // `code` indexes as "undefined" when absent, as it always has.
  const what = FAULTS[code as number] ?? `fault ${code}`;
  // Code 1's detail is the reading as kind x 10, code 2's is the kind.
  if (code === 1) return `${what} (reads ${((detail as number) / 10).toFixed(1)})`;
  if (code === 2) return `${what} (${kindName(detail as number)})`;
  return what;
}

/** The chain states `StateWord()` emits (state_line.cpp:24). */
export const CHAIN_STATES: Record<string, string> = {
  powerwait: 'waiting for the satellites to boot',
  discover: 'learning the chain',
  scan: 'scanning',
  findbreak: 'the ring is broken — looking for where',
  partial: 'broken: scanning the satellites before the break',
  version: 'a satellite runs a firmware major this build does not speak',
};

export const PREFIX = '#S ';

/**
 * Split a growing serial buffer into whole lines. Returns the lines and the
 * unfinished remainder to keep for next time — a read can split a line
 * anywhere, and USB CDC splits them often.
 */
export function takeLines(buffer: string): { lines: string[]; rest: string } {
  const parts = buffer.split('\n');
  // split() always returns at least one part, so `at(-1)` is never undefined.
  return { lines: parts.slice(0, -1).map((l) => l.replace(/\r$/, '')), rest: parts.at(-1) as string };
}

/**
 * One line to state, or null for anything that is not a whole `#S` line —
 * the greeting, `PrintState()`'s prose, and the truncated lines described
 * above all land here and are all equally "not state".
 */
export function parseStateLine(line: unknown): StateLine | null {
  if (typeof line !== 'string' || !line.startsWith(PREFIX)) return null;
  let state: unknown;
  try {
    state = JSON.parse(line.slice(PREFIX.length));
  } catch {
    return null;
  }
  if (!state || typeof state !== 'object' || Array.isArray(state)) return null;
  // Only the outer shape is checked, which is the rule: the readers treat every
  // field as possibly missing.
  return state as StateLine;
}

/**
 * A satellite's slots as things to draw. `in[]` is typed by the board kind
 * rather than carried in the line, so an unknown kind shows its numbers raw
 * instead of being drawn as something it might not be.
 */
export function slots(sat: Partial<Satellite>): Slot[] {
  const kind = sat?.kind ?? -1;
  const values = Array.isArray(sat?.in) ? sat.in : [];
  const type = kindInput(kind);
  return values.map((value, i) => ({
    index: i,
    type: Array.isArray(sat.fault) && sat.fault[i] ? 'fault' : type,
    value,
    // btn[] is sent only for encoder satellites (state_line.cpp:131).
    pressed: Array.isArray(sat.btn) ? !!sat.btn[i] : type === 'button' ? !!value : false,
    fault: Array.isArray(sat.fault) ? sat.fault[i] ?? null : null,
  }));
}

/** A pot's 0..1000 as a percentage, which is how the panel reads it. */
export function potPercent(value: number): number {
  return Math.max(0, Math.min(100, value / 10));
}

/**
 * Where the ring is broken, as something to say. `broken` is the count of
 * satellites before the break, or -1 (SatChain::BrokenAfter).
 */
export function breakAfter(chain?: ChainState): number | null {
  const n = chain?.broken ?? -1;
  return n < 0 ? null : n;
}

/** Scans that came back whole, as a percentage, or null before any ran. */
export function scanHealth(chain?: ChainState): number | null {
  const good = chain?.good ?? 0;
  const bad = chain?.bad ?? 0;
  if (!good && !bad) return null;
  return (good / (good + bad)) * 100;
}

// ------------------------------------------------------- the panel's controls
//
// What each control on the panel does, from `instruments/rodeo-demo/firmware/
// src/panel.h`'s table and `panel.cpp`'s `Update()`. This is what lets the page
// put a knob's position next to the number it is driving — which is the only
// way to see the one thing about this panel that surprises people, below.

/**
 * `Find()` takes the first TWO banks of each kind on the chain, in ring order:
 * the first is bank 1, the next bank 2 (panel.cpp). `pots` and `encs` hold
 * their places on the chain, bank 1 first; `pot` and `enc` are bank 1's.
 */
export interface PanelBanks {
  pots: number[];
  encs: number[];
  pot: number | null;
  enc: number | null;
}

export function panelBanks(c?: ChainState): PanelBanks {
  const list: Satellite[] = Array.isArray(c?.s) ? c.s : [];
  const of = (kind: number) =>
    list.flatMap((sat, i) => (sat?.kind === kind ? [i] : [])).slice(0, PANEL_BANKS);
  const pots = of(5);
  const encs = of(6);
  return { pots, encs, pot: pots[0] ?? null, enc: encs[0] ?? null };
}

/** Two of each kind of bank: a third on the chain drives nothing. */
export const PANEL_BANKS = 2;

/** `p.release = 0.005 * 800^pot` seconds (panel.cpp:187). */
export function releaseFromPot(pot: number): number {
  return 5 * Math.pow(800, Math.max(0, Math.min(1, pot)));
}

export function potForRelease(ms: number): number {
  return Math.log(Math.max(5, ms) / 5) / Math.log(800);
}

/** Where on an `lo * (hi / lo)^pot` curve, from 0 to 1, a value sits. */
const potOnCurve = (lo: number, hi: number) => (v: number) =>
  Math.log(Math.max(lo, Math.min(hi, v)) / lo) / Math.log(hi / lo);

/**
 * Pots 1-8, in slot order: bank 1's four, then bank 2's (panel.h). Attack is
 * `0.001 * 4000^pot` s and decay `0.001 * 10000^pot` s (panel.cpp's `Pot()`).
 */
/** The `#S` keys the pots drive. */
export type PotKey = 'cut' | 'res' | 'env' | 'drv' | 'atk' | 'dec' | 'sus' | 'rel';

export interface PotSpec {
  name: string;
  key: PotKey;
  unit: '%' | 'ms';
  /** The parameter's value back to a 0..1 pot position. */
  toPot: (v: number) => number;
}

export const PANEL_POTS: PotSpec[] = [
  { name: 'cutoff', key: 'cut', unit: '%', toPot: (v) => v / 100 },
  { name: 'resonance', key: 'res', unit: '%', toPot: (v) => v / 100 },
  { name: 'filter envelope', key: 'env', unit: '%', toPot: (v) => v / 100 },
  { name: 'drive', key: 'drv', unit: '%', toPot: (v) => v / 100 },
  { name: 'attack', key: 'atk', unit: 'ms', toPot: potOnCurve(1, 4000) },
  { name: 'decay', key: 'dec', unit: 'ms', toPot: potOnCurve(1, 10000) },
  { name: 'sustain', key: 'sus', unit: '%', toPot: (v) => v / 100 },
  { name: 'release', key: 'rel', unit: 'ms', toPot: potForRelease },
];

/** `enum class Fx` order (synth.h:28), which `fx` in the line is an index into. */
export const FX_NAMES = ['chorus', 'reverb', 'both'];

/**
 * Encoders 1-8: bank 1's four, then bank 2's (panel.h). They send detents, not
 * positions, so there is nothing to compare — only the parameter each one
 * turns, what its tap does, and what it does held. `zero` is what a 0 means
 * for the parameters that have an off.
 */
export interface EncoderSpec {
  name: string;
  /** The `#S` key it turns. */
  key: string;
  /** What it turns instead in sampler layer (E1 picks the sound). */
  sampler?: 'sound';
  unit?: string;
  /** What a 0 means, for the parameters that have an off. */
  zero?: string;
  tap: string;
  /** The `#S` key that says what the tap last set. */
  tapKey?: 'layer' | 'lfo' | 'fx' | 'mono';
  held?: string;
  heldKey?: 'oct';
}

export interface EncoderReadout {
  name: string;
  value: string;
  extra: string;
  reported: boolean;
}

export const PANEL_ENCODERS: EncoderSpec[] = [
  { name: 'shape', key: 'shape', sampler: 'sound', tap: 'layer', tapKey: 'layer' },
  { name: 'detune', key: 'det', unit: ' cents', tap: 'detune and octave back', held: 'octave', heldKey: 'oct' },
  { name: 'high-pass', key: 'hpf', unit: ' Hz', zero: 'off', tap: 'LFO on/off', tapKey: 'lfo' },
  { name: 'effects mix', key: 'mix', unit: '%', tap: 'chorus / reverb / both', tapKey: 'fx' },
  { name: 'filter attack', key: 'fatk', unit: ' ms', zero: 'follows attack', tap: 'back to following attack' },
  { name: 'filter decay', key: 'fdec', unit: ' ms', tap: 'back to 400 ms' },
  { name: 'filter sustain', key: 'fsus', unit: '%', tap: 'back to 25%' },
  { name: 'glide', key: 'glide', unit: ' ms', zero: 'off', tap: 'mono on/off', tapKey: 'mono' },
];

/**
 * One encoder row's text: what it is turning, and what its tap or hold last
 * set. A key the line does not carry reads as "not reported" rather than a
 * blank, which would look like zero.
 */
export function encoderReadout(
  spec: EncoderSpec,
  st: StateLine | null | undefined,
  sampler = false
): EncoderReadout {
  const name = sampler && spec.sampler ? spec.sampler : spec.name;
  const key = sampler && spec.sampler ? spec.sampler : spec.key;
  // The line's values are numbers or strings; which one each key is, is the
  // caller's business below, where each is cast to what that key carries.
  const of = (k: string) =>
    (st && st[k] !== undefined && st[k] !== null ? st[k] : null) as string | number | null;

  let value;
  if (key === 'sound') {
    const n = of('sound') as number | null;
    value = n === null ? 'not reported' : `${n + 1} of ${of('nsounds') ?? '?'}`;
  } else if (!key || of(key) === null) {
    value = 'not reported';
  } else if (spec.zero && of(key) === 0) {
    value = spec.zero;
  } else {
    value = `${of(key)}${spec.unit ?? ''}`;
  }

  const parts: string[] = [];
  if (spec.heldKey && of(spec.heldKey) !== null) {
    const oct = of(spec.heldKey) as number;
    parts.push(`octave ${oct > 0 ? `+${oct}` : oct}`);
  }
  if (spec.tapKey === 'lfo' && of('lfo') !== null) parts.push(`LFO ${of('lfo') ? 'on' : 'off'}`);
  else if (spec.tapKey === 'fx' && of('fx') !== null) parts.push(FX_NAMES[of('fx') as number] ?? `fx ${of('fx')}`);
  else if (spec.tapKey === 'layer' && of('layer') !== null) parts.push(String(of('layer')));
  else if (spec.tapKey === 'mono' && of('mono') !== null) parts.push(of('mono') ? 'mono' : 'poly');

  return { name, value, extra: parts.join(' · '), reported: value !== 'not reported' };
}

/** How far a pot must move to take its parameter back (panel.cpp:40). */
export const TAKEOVER = 0.02;

/**
 * Whether a pot's position and its parameter disagree, which is normal and
 * worth showing: loading a preset sets the parameters, and each pot keeps its
 * hands off until it is moved more than `TAKEOVER` from where it was when the
 * preset landed (panel.cpp:178-186). Until then the knob lies about the sound,
 * and this is the only view that can say so.
 */
export function potHeld(
  potValue: number | undefined,
  paramValue: number | undefined,
  toPot: (v: number) => number
): boolean {
  if (!Number.isFinite(potValue) || !Number.isFinite(paramValue)) return false;
  // Both finite numbers now; Number.isFinite does not narrow for TypeScript.
  return Math.abs((potValue as number) / 1000 - toPot(paramValue as number)) > TAKEOVER;
}
