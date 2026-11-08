// Preset files: `key = value` text the Rodeo only ever reads.
//
// Mirrors `instruments/rodeo-demo/firmware/src/presets.cpp`. Quirks reproduced
// on purpose, because a console that is more forgiving than the firmware is
// worse than no console:
//
//   - `Squash` lowercases AND strips every space, so `Saw + Sub` == `saw+sub`,
//     and so does `S A W`. Applied to keys and to values, not to `sound`.
//   - The key is squashed into `char[16]`, so anything past 15 characters is
//     cut before the comparison. `resonance` is 9, so nothing real hits this,
//     but a typo long enough to truncate matches oddly rather than failing.
//   - `#` or `;` ANYWHERE truncates the line, and it happens before the `=`
//     split — so a `#` in a sound name eats the rest of the name silently.
//   - Numbers are CLAMPED to range, not rejected (presets.cpp:56). `cutoff =
//     900` is accepted as 100. We surface that as a warning; the firmware says
//     nothing.
//   - A line with no `=`, or an unknown key, or a value out of vocabulary, is
//     skipped and listed by `Problems()` on a serial port nobody is reading.

import { MAX_NAME, MAX_PRESET_BYTES } from './card.js';

/** synth.cpp's ShapeName — the order is the enum's, and E1 steps through it. */
export const SHAPES = ['saw', 'square', 'pulse', 'triangle', 'saw + sub', 'square + sub', 'fm'];
export const FILTERS = ['svf', 'ladder', 'ladder12'];
export const LAYERS = ['synth', 'sampler', 'both'];
export const FX = ['chorus', 'reverb', 'both'];

/**
 * Every key, with what presets.cpp will do with it, in panel order: pots 1-8,
 * then the encoders (panel.h). `pot` is the pot that drives it, which is what
 * a key left out means: the pot keeps it. `enc` is the encoder that turns it.
 */
export const KEYS = {
  layer: { kind: 'enum', values: LAYERS, enc: 'E1 tap' },
  shape: { kind: 'enum', values: SHAPES, enc: 'E1' },
  sound: { kind: 'name', enc: 'E1' },
  cutoff: { kind: 'number', lo: 0, hi: 100, unit: '%', pot: 1 },
  resonance: { kind: 'number', lo: 0, hi: 100, unit: '%', pot: 2 },
  env: { kind: 'number', lo: 0, hi: 100, unit: '%', pot: 3 },
  drive: { kind: 'number', lo: 0, hi: 100, unit: '%', pot: 4 },
  attack: { kind: 'number', lo: 1, hi: 4000, unit: 'ms', pot: 5 },
  decay: { kind: 'number', lo: 1, hi: 10000, unit: 'ms', pot: 6 },
  sustain: { kind: 'number', lo: 0, hi: 100, unit: '%', pot: 7 },
  release: { kind: 'number', lo: 5, hi: 4000, unit: 'ms', pot: 8 },
  detune: { kind: 'number', lo: 0, hi: 50, unit: 'cents', enc: 'E2' },
  octave: { kind: 'number', lo: -2, hi: 2, unit: '', round: true, enc: 'E2 held' },
  hpf: { kind: 'number', lo: 0, hi: 2000, unit: 'Hz', enc: 'E3' },
  lfo: { kind: 'enum', values: ['on', 'off'], enc: 'E3 tap' },
  mix: { kind: 'number', lo: 0, hi: 100, unit: '%', enc: 'E4' },
  fx: { kind: 'enum', values: FX, enc: 'E4 tap' },
  fattack: { kind: 'number', lo: 1, hi: 4000, unit: 'ms', enc: 'E5' },
  fdecay: { kind: 'number', lo: 1, hi: 10000, unit: 'ms', enc: 'E6' },
  fsustain: { kind: 'number', lo: 0, hi: 100, unit: '%', enc: 'E7' },
  glide: { kind: 'number', lo: 0, hi: 2000, unit: 'ms', enc: 'E8' },
  mono: { kind: 'enum', values: ['on', 'off'], enc: 'E8 tap' },
  filter: { kind: 'enum', values: FILTERS },
  keytrack: { kind: 'number', lo: 0, hi: 100, unit: '%' },
};

/** `Squash`: drop whitespace, lowercase (presets.cpp:46). */
export function squash(s, max = Infinity) {
  let out = '';
  for (const ch of s) {
    if (out.length >= max) break;
    if (!/\s/.test(ch)) out += ch.toLowerCase();
  }
  return out;
}

/** `strtof` + the clamp in `Number` (presets.cpp:54). Null if not a number. */
function asNumber(v, lo, hi) {
  // strtof takes leading digits then demands the rest be empty; "12abc" fails,
  // "12" and "1e3" and ".5" pass.
  if (!/^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$/.test(v)) return null;
  const x = parseFloat(v);
  if (!Number.isFinite(x)) return null;
  return { value: Math.min(hi, Math.max(lo, x)), clamped: x < lo || x > hi, raw: x };
}

/**
 * Parse preset text the way the firmware does.
 * Returns { values, problems, warnings } — `problems` are lines the Rodeo will
 * skip, `warnings` are things it will accept but not as written.
 */
export function parsePreset(text, name = '') {
  const values = {};
  const problems = [];
  const warnings = [];

  const bytes = new TextEncoder().encode(text).length;
  if (bytes > MAX_PRESET_BYTES) {
    warnings.push(`file is ${bytes} bytes; the Rodeo reads the first ${MAX_PRESET_BYTES} and ignores the rest`);
  }

  text.split('\n').forEach((rawLine, i) => {
    const line = i + 1;
    // `#` or `;` anywhere ends the line, before anything else looks at it.
    const cut = rawLine.search(/[#;]/);
    const body = (cut === -1 ? rawLine : rawLine.slice(0, cut)).trim();
    if (!body) return;

    const eq = body.indexOf('=');
    if (eq === -1) {
      problems.push({ line, text: body, why: 'no "=", so the Rodeo skips it' });
      return;
    }
    const rawKey = body.slice(0, eq).trim();
    const rawValue = body.slice(eq + 1).trim();
    const key = squash(rawKey, 15); // char k[16]
    const spec = KEYS[key];
    if (!spec) {
      problems.push({ line, text: rawKey, why: `not a key the Rodeo knows` });
      return;
    }

    if (spec.kind === 'name') {
      // `sound` keeps case and spaces; only trimmed, then 31 chars.
      if (!rawValue) {
        problems.push({ line, text: rawKey, why: 'sound needs a name' });
        return;
      }
      if (rawValue.length > MAX_NAME) {
        warnings.push(`line ${line}: sound name is cut to "${rawValue.slice(0, MAX_NAME)}"`);
      }
      if (cut !== -1 && /[#;]/.test(rawLine.slice(cut))) {
        const after = rawLine.slice(cut);
        if (after.trim().length > 1) {
          warnings.push(`line ${line}: everything from "${after.trim()[0]}" is treated as a comment`);
        }
      }
      values[key] = rawValue.slice(0, MAX_NAME);
      return;
    }

    const v = squash(rawValue, 31); // char v[32]
    if (spec.kind === 'enum') {
      const hit = spec.values.find((candidate) => squash(candidate) === v);
      if (hit === undefined) {
        problems.push({
          line,
          text: `${rawKey} = ${rawValue}`,
          why: `not one of ${spec.values.join(', ')}`,
        });
        return;
      }
      values[key] = hit;
      return;
    }

    const n = asNumber(v, spec.lo, spec.hi);
    if (n === null) {
      problems.push({ line, text: `${rawKey} = ${rawValue}`, why: 'not a number' });
      return;
    }
    if (n.clamped) {
      warnings.push(
        `line ${line}: ${key} ${n.raw} is outside ${spec.lo}..${spec.hi}; the Rodeo uses ${n.value}`
      );
    }
    values[key] = spec.round ? Math.round(n.value) : n.value;
  });

  return { name, values, problems, warnings };
}

/** Write a preset back out: stable key order, a comment header, no surprises. */
export function formatPreset(values) {
  const order = Object.keys(KEYS);
  const lines = ['# written by rodeo-console', ''];
  for (const key of order) {
    if (values[key] === undefined || values[key] === '') continue;
    const spec = KEYS[key];
    const unit = spec.kind === 'number' && spec.unit ? `  # ${spec.unit}` : '';
    lines.push(`${key.padEnd(9)} = ${values[key]}${unit}`);
  }
  return lines.join('\n') + '\n';
}
