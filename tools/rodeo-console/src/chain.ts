// The satellite chain, as the host protocol's chain report gives it
// (rodeo-sdk/common/host.cpp, docs/guides/host.mdx): what to draw for each
// satellite and each slot. Any firmware built on the SDK sends the same report.
//
// The board kinds are a copy of `rodeo-sdk/common/sat_frame.h`, which stays
// authoritative. Every field is read as possibly missing: a firmware without a
// chain sends none, and an older one may leave a field out.

import type { ChainReport } from '@dotrodeo/rodeo-host';

/** A FAULT slot: `[code, detail]` (sat_frame.h:108). */
export type Fault = [code?: number, detail?: number];

/** One satellite in `s`, in ring order. */
export type Satellite = ChainReport['s'][number];

/** The report, every field optional. */
export type ChainState = Partial<ChainReport>;

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
 * custom board declares its own in DISCOVER and the report cannot say, so this
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

/** The chain states `StateWord()` emits (rodeo-sdk/common/host.cpp). */
export const CHAIN_STATES: Record<string, string> = {
  powerwait: 'waiting for the satellites to boot',
  discover: 'learning the chain',
  scan: 'scanning',
  findbreak: 'the ring is broken — looking for where',
  partial: 'broken: scanning the satellites before the break',
  version: 'a satellite runs a firmware major this build does not speak',
};

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
 * A satellite's slots as things to draw. `in[]` is typed by the board kind
 * rather than carried in the report, so an unknown kind shows its numbers raw
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
    // btn[] is sent only for encoder satellites (host.cpp).
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

/**
 * Where each pot and encoder is, counted across the chain in ring order: pot
 * 1 is the first pot slot on the first satellite that has pots, and so on.
 * This is what a parameter's `pot` in the hello means to the console, and it
 * is how the SDK's own banks number them (the first pot bank's four are pots
 * 1-4). A faulted slot keeps its number.
 */
export interface Place {
  sat: number;
  slot: number;
  /** 0-based among its kind. */
  n: number;
}

export function places(chain?: ChainState): { pots: Place[]; encs: Place[] } {
  const pots: Place[] = [];
  const encs: Place[] = [];
  (Array.isArray(chain?.s) ? chain.s : []).forEach((sat, i) => {
    const input = kindInput(sat?.kind ?? -1);
    const n = Array.isArray(sat?.in) ? sat.in.length : 0;
    for (let k = 0; k < n; k++) {
      if (input === 'pot') pots.push({ sat: i, slot: k, n: pots.length });
      if (input === 'encoder') encs.push({ sat: i, slot: k, n: encs.length });
    }
  });
  return { pots, encs };
}
