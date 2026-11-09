// A preset as parameter sets, so the editor can be heard before it is saved:
// the preset file's keys and words, in the units the MIDI protocol takes
// (planning/RODEO_DEMO_MIDI_PROTOCOL.md). Two differences from the file: the
// pots' first three parameters are ‰ on the wire and % in the file, and a
// choice or a sound goes by its number, not its name.

import { KEYS, squash } from '../preset.ts';
import type { PresetValues } from '../preset.ts';
import type { Param } from '@dotrodeo/rodeo-host';
import type { Sound } from '@dotrodeo/rodeo-host/rodeo-demo';

/** Keys whose file value is % but whose parameter is ‰ (params.cpp). */
const PER_MILLE = new Set(['cutoff', 'resonance', 'env']);

export type PresetValue = string | number;

/**
 * One preset key's value as a parameter value, or a reason it can't be sent:
 * the Rodeo doesn't have that parameter, that choice, or that sound.
 */
export function toParam(
  key: string,
  value: PresetValue,
  params: Record<string, Param>,
  sounds: Sound[]
): { value: number } | { why: string } {
  const p = params[key];
  if (!p) return { why: `the Rodeo has no ${key}` };
  if (key === 'sound') {
    // presets.cpp finds a sound by its squashed name, as here
    const want = squash(String(value));
    const i = sounds.findIndex((s) => squash(s.name) === want);
    return i < 0 ? { why: `no sound "${value}" on the Rodeo` } : { value: i };
  }
  if (p.names) {
    const i = p.names.findIndex((n) => squash(n) === squash(String(value)));
    return i < 0 ? { why: `${key} has no "${value}"` } : { value: p.min + i };
  }
  const x = Number(value);
  if (!Number.isFinite(x)) return { why: `${key} = ${value} is not a number` };
  return { value: PER_MILLE.has(key) ? Math.round(x * 10) : Math.round(x) };
}

/**
 * The sets that put back what loading the preset would: the default of every
 * parameter it leaves out (each parameter's `def`, from the Rodeo's hello), so
 * a sound left over from the last one isn't heard as this one's. Not a pot's
 * parameter, which stays where its pot is, nor the sound, which stays as it is.
 */
export function presetDefaults(
  values: Record<string, PresetValue | undefined>,
  params: Record<string, Param>
): [string, number][] {
  return Object.values(params)
    .filter((p) => !p.pot && p.key !== 'sound' && (values[p.key] === undefined || values[p.key] === ''))
    .map((p) => [p.key, p.def]);
}

/** Every key a preset sets, as parameter values, and the ones that can't be sent. */
export function presetToParams(
  values: Record<string, PresetValue | undefined>,
  params: Record<string, Param>,
  sounds: Sound[]
): { sets: [string, number][]; skipped: string[] } {
  const sets: [string, number][] = [];
  const skipped: string[] = [];
  for (const [key, value] of Object.entries(values)) {
    if (value === undefined || value === '') continue;
    const r = toParam(key, value, params, sounds);
    if ('value' in r) sets.push([key, r.value]);
    else skipped.push(r.why);
  }
  return { sets, skipped };
}

/** A parameter's value as the panel says it: "41 %", "saw + sub", "420 ms". */
export function describe(p: Param, v: number, sounds: Sound[] = []): string {
  if (p.key === 'sound') return sounds[v]?.name ?? `sound ${v + 1}`;
  if (p.key === 'fattack' && v === 0) return 'follows attack';
  if (p.key === 'hpf' && v === 0) return 'off';
  if (p.names) return p.names[v - p.min] ?? String(v);
  if (p.unit === '‰') return `${Math.round(v / 10)} %`;
  if (p.unit) return `${v} ${p.unit}`;
  return v > 0 ? `+${v}` : String(v);
}

/**
 * The other way: what the Rodeo is playing, as a preset's values, so a sound
 * dialled in on the panel can be kept. Only keys a preset file has; the
 * filter attack is left out while it follows attack, as a file says that.
 */
export function paramsToPreset(
  p: Record<string, number>,
  params: Record<string, Param>,
  sounds: Sound[]
): PresetValues {
  const out: PresetValues = {};
  for (const key of Object.keys(KEYS)) {
    const v = p[key];
    const param = params[key];
    if (v === undefined || !param) continue;
    if (key === 'sound') {
      const s = sounds[v];
      if (s) out.sound = s.name;
    } else if (key === 'fattack' && v === 0) {
      continue;
    } else if (param.names) {
      const name = param.names[v - param.min];
      if (name !== undefined) out[key] = name;
    } else {
      out[key] = PER_MILLE.has(key) ? Math.round(v / 10) : v;
    }
  }
  return out;
}
