// A parameter's value in words, from what the hello says about it.

import type { Param, Snapshot } from '@dotrodeo/rodeo-host';
import type { Profile } from './profile.ts';

/** "41 %", "saw + sub", "420 ms", "+3": the firmware's own words first. */
export function describe(p: Param, v: number, s: Snapshot, profile: Profile = {}): string {
  const own = profile.describe?.(p, v, s);
  if (own !== null && own !== undefined) return own;
  if (p.names) return p.names[v - p.min] ?? String(v);
  if (p.unit === '‰') return `${Math.round(v / 10)} %`;
  if (p.unit) return `${v} ${p.unit}`;
  // A range through zero reads signed, as a detune or an octave does
  return p.min < 0 && v > 0 ? `+${v}` : String(v);
}
