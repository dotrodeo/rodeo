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

/** Board kinds (sat_frame.h:119-130). 8-13 are kept for future satellites. */
export const KINDS = {
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

export function kindName(kind) {
  return KINDS[kind] ?? `kind ${kind}`;
}

/** `sat_kind_slots` — 4 for the banks, 1 otherwise (sat_frame.h:140). */
export function kindSlots(kind) {
  return kind === 5 || kind === 6 || kind === 7 ? 4 : 1;
}

/**
 * `sat_kind_input` — what a board kind's slots report (sat_frame.h:150). A
 * custom board declares its own in DISCOVER and the line cannot say, so this
 * returns null for it and the panel shows the raw number.
 */
export function kindInput(kind) {
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
export function kindSupported(kind) {
  return kind <= 7 || kind === 15;
}

/** FAULT slot codes, D0 (sat_frame.h:108). */
export const FAULTS = {
  1: 'ID resistor reads between two kinds',
  2: 'the stock firmware does not run this board kind',
  3: 'sensor',
};

export function faultText([code, detail] = []) {
  const what = FAULTS[code] ?? `fault ${code}`;
  // Code 1's detail is the reading as kind x 10, code 2's is the kind.
  if (code === 1) return `${what} (reads ${(detail / 10).toFixed(1)})`;
  if (code === 2) return `${what} (${kindName(detail)})`;
  return what;
}

/** The chain states `StateWord()` emits (state_line.cpp:24). */
export const CHAIN_STATES = {
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
export function takeLines(buffer) {
  const parts = buffer.split('\n');
  return { lines: parts.slice(0, -1).map((l) => l.replace(/\r$/, '')), rest: parts.at(-1) };
}

/**
 * One line to state, or null for anything that is not a whole `#S` line —
 * the greeting, `PrintState()`'s prose, and the truncated lines described
 * above all land here and are all equally "not state".
 */
export function parseStateLine(line) {
  if (typeof line !== 'string' || !line.startsWith(PREFIX)) return null;
  let state;
  try {
    state = JSON.parse(line.slice(PREFIX.length));
  } catch {
    return null;
  }
  if (!state || typeof state !== 'object' || Array.isArray(state)) return null;
  return state;
}

/**
 * A satellite's slots as things to draw. `in[]` is typed by the board kind
 * rather than carried in the line, so an unknown kind shows its numbers raw
 * instead of being drawn as something it might not be.
 */
export function slots(sat) {
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
export function potPercent(value) {
  return Math.max(0, Math.min(100, value / 10));
}

/**
 * Where the ring is broken, as something to say. `broken` is the count of
 * satellites before the break, or -1 (SatChain::BrokenAfter).
 */
export function breakAfter(chain) {
  const n = chain?.broken ?? -1;
  return n < 0 ? null : n;
}

/** Scans that came back whole, as a percentage, or null before any ran. */
export function scanHealth(chain) {
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

/** `Find()` takes the FIRST bank of each kind on the chain (panel.cpp:73-76). */
export function panelBanks(c) {
  const list = Array.isArray(c?.s) ? c.s : [];
  const first = (kind) => {
    const i = list.findIndex((sat) => sat?.kind === kind);
    return i < 0 ? null : i;
  };
  return { pot: first(5), enc: first(6) };
}

/** `p.release = 0.005 * 800^pot` seconds (panel.cpp:187). */
export function releaseFromPot(pot) {
  return 5 * Math.pow(800, Math.max(0, Math.min(1, pot)));
}

export function potForRelease(ms) {
  return Math.log(Math.max(5, ms) / 5) / Math.log(800);
}

/** Pots 1-4, in slot order (panel.h:5-6). */
export const PANEL_POTS = [
  { name: 'cutoff', key: 'cut', unit: '%', toPot: (v) => v / 100 },
  { name: 'resonance', key: 'res', unit: '%', toPot: (v) => v / 100 },
  { name: 'filter envelope', key: 'env', unit: '%', toPot: (v) => v / 100 },
  { name: 'release', key: 'rel', unit: 'ms', toPot: potForRelease },
];

/**
 * Encoders 1-4 (panel.h:8-12). They send detents, not positions, so there is
 * nothing to compare — only the parameter each one is turning. `key: null`
 * means the `#S` line does not carry that value, which the page says rather
 * than leaving a blank that looks like zero.
 */
export const PANEL_ENCODERS = [
  { name: 'shape', key: 'shape', sampler: 'sound', tap: 'layer', held: 'preset' },
  { name: 'detune', key: null, tap: 'detune and octave back', held: 'octave' },
  { name: 'attack', key: 'atk', unit: 'ms', tap: 'LFO on/off' },
  { name: 'effects mix', key: null, tap: 'chorus / reverb / both' },
];

/** How far a pot must move to take its parameter back (panel.cpp:40). */
export const TAKEOVER = 0.02;

/**
 * Whether a pot's position and its parameter disagree, which is normal and
 * worth showing: loading a preset sets the parameters, and each pot keeps its
 * hands off until it is moved more than `TAKEOVER` from where it was when the
 * preset landed (panel.cpp:178-186). Until then the knob lies about the sound,
 * and this is the only view that can say so.
 */
export function potHeld(potValue, paramValue, toPot) {
  if (!Number.isFinite(potValue) || !Number.isFinite(paramValue)) return false;
  return Math.abs(potValue / 1000 - toPot(paramValue)) > TAKEOVER;
}
