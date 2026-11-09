// What rodeo-demo adds to the console (rodeo-console/profile): its panel's
// grouping, what its encoders turn, the preset picker, the voice model, and the
// card's kit on the keyboard. The console knows the host protocol and nothing
// about this firmware; this is everything it would otherwise have had to.
//
// Read off instruments/rodeo-demo/firmware/src: panel.h for the controls,
// params.cpp for the table, synth.cpp for the voices. The firmware is right.

import type { Param, Snapshot } from '@dotrodeo/rodeo-host';
import { demoState, loadPreset, presets, sounds } from '@dotrodeo/rodeo-host/rodeo-demo';
import { describe as plain } from 'rodeo-console/describe';
import { rodeo } from 'rodeo-console/link';
import type { EncoderLabel, KeySpan, Profile, VoiceModel } from 'rodeo-console/profile';
import { noteName } from './card.ts';
import { KEYS } from './preset.ts';
import { ROOT_NOTE, VOICE_COUNT, Voices } from './voices.ts';

/** The panel's banks, as panel.h lays them out. */
export const GROUPS: [string, string[]][] = [
  ['pot bank 1 · filter', ['cutoff', 'resonance', 'env', 'drive']],
  ['pot bank 2 · amp envelope', ['attack', 'decay', 'sustain', 'release']],
  ['encoder bank 1', ['layer', 'shape', 'sound', 'detune', 'octave', 'hpf', 'lfo', 'mix', 'fx']],
  ['encoder bank 2 · filter envelope', ['fattack', 'fdecay', 'fsustain', 'glide', 'mono']],
  ['preset only', ['filter', 'keytrack']],
];

/**
 * Encoders 1-8: bank 1's four, then bank 2's (panel.h). They send detents, not
 * positions, so there is nothing to compare: only the parameter each one
 * turns, what its tap does, and what its tap or hold last set.
 */
export interface EncoderSpec {
  name: string;
  /** The parameter it turns. */
  key: string;
  /** What it turns instead in sampler layer (E1 picks the sound). */
  sampler?: 'sound';
  tap: string;
  /** The parameters its tap and hold set, said beside its value. */
  shows?: string[];
}

export const ENCODERS: EncoderSpec[] = [
  { name: 'shape', key: 'shape', sampler: 'sound', tap: 'layer', shows: ['layer'] },
  { name: 'detune', key: 'detune', tap: 'detune and octave back; held: octave', shows: ['octave'] },
  { name: 'high-pass', key: 'hpf', tap: 'LFO on/off', shows: ['lfo'] },
  { name: 'effects mix', key: 'mix', tap: 'chorus / reverb / both', shows: ['fx'] },
  { name: 'filter attack', key: 'fattack', tap: 'back to following attack' },
  { name: 'filter decay', key: 'fdecay', tap: 'back to 400 ms' },
  { name: 'filter sustain', key: 'fsustain', tap: 'back to 25%' },
  { name: 'glide', key: 'glide', tap: 'mono on/off', shows: ['mono'] },
];

/** The words rodeo-demo's panel uses where the table's units would not do. */
export function describe(p: Param, v: number, s: Snapshot): string | null {
  if (p.key === 'sound') return sounds(s)[v]?.name ?? `sound ${v + 1}`;
  if (p.key === 'fattack' && v === 0) return 'follows attack';
  if ((p.key === 'hpf' || p.key === 'glide') && v === 0) return 'off';
  return null;
}

const say = (p: Param, v: number, s: Snapshot) => plain(p, v, s, { describe });

/** The layer the Rodeo says it is in, or null before it has said. */
function layer(s: Snapshot): string | null {
  const p = s.params.layer;
  const v = s.state?.p.layer;
  return p?.names && v !== undefined ? (p.names[v - p.min] ?? null) : null;
}

/** One encoder row: what it turns, its value, and what its tap or hold set. */
export function encoder(n: number, s: Snapshot): EncoderLabel | null {
  const spec = ENCODERS[n];
  if (!spec) return null; // a third encoder bank: nothing on the panel reads it
  const sampler = layer(s) === 'sampler' && spec.sampler;
  const key = sampler ? spec.sampler! : spec.key; // set when sampler is
  const p = s.params[key];
  const v = s.state?.p[key];
  const value = p && v !== undefined ? say(p, v, s) : 'not reported';
  const extra = (spec.shows ?? [])
    .map((k) => {
      const q = s.params[k];
      const w = s.state?.p[k];
      if (!q || w === undefined) return '';
      if (k === 'octave') return `octave ${w > 0 ? `+${w}` : w}`;
      if (k === 'lfo') return `LFO ${q.names?.[w] ?? w}`;
      if (k === 'mono') return w ? 'mono' : 'poly';
      return say(q, w, s);
    })
    .filter(Boolean)
    .join(' · ');
  return { name: sampler ? 'sound' : spec.name, value, extra, tap: spec.tap };
}

/** The readout's rodeo-demo chips: the preset, the layer, the card. */
export function readout(s: Snapshot): [string, string][] {
  const d = demoState(s);
  if (!d) return [];
  const card = ({ 0: 'no card', 1: 'card mounted', 2: 'card is the computer’s' } as Record<number, string>)[d.card] ?? '?';
  return [
    [d.preset >= 0 ? `preset ${d.preset + 1}` : 'no preset', d.name],
    ['layer', layer(s) ?? '?'],
    ['card', card],
    ['loaded', `${d.loaded} of ${d.samples}`],
  ];
}

/** The note `allocate` is asked about when it should match nothing. */
const NO_NOTE = -99;

/**
 * The voice panel's model, synth.cpp's `Allocate()` (voices.ts), fed the notes
 * the console sends. The Rodeo reports how many voices are sounding, and that
 * wins over the count; which notes are where is the part nothing reports.
 */
function voiceModel(span: () => KeySpan | null): VoiceModel {
  const v = new Voices({ release: 300 });
  let sampler = false;
  rodeo.subscribe((s) => {
    const rel = s.state?.p.release;
    if (rel !== undefined) v.release = rel;
    sampler = layer(s) === 'sampler';
  });
  // `Start` returns before `Allocate` only in sampler layer on a key with no
  // sample (synth.cpp:158-161). Null: do not claim, whenever that's unknown.
  const hasSample = (note: number): boolean | null => {
    const kit = sampler ? span() : null;
    return kit ? note >= kit.from && note <= kit.to : null;
  };
  return {
    noteOn: (note, velocity, now) => void v.noteOn(note, velocity, now, hasSample(note)),
    noteOff: (note, now) => void v.noteOff(note, now),
    allOff: (now) => void v.allOff(now),
    setSustain: (down, now) => void v.setSustain(down, now),
    sounding: (now) => v.sounding(now),
    slots: (now) =>
      v.slots.map((s) => ({ note: s.note, state: !v.active(s, now) ? 'free' : s.gate ? 'held' : 'ringing' })),
    summary(now, s) {
      const real = demoState(s)?.voices;
      const predicted = v.sounding(now);
      const next = v.slots[v.allocate(NO_NOTE, now).index];
      const steal = predicted >= VOICE_COUNT ? ` — next steals ${noteName(next.note)}` : '';
      return real !== undefined ? `${real}/${VOICE_COUNT}${steal}` : `${predicted}/${VOICE_COUNT} predicted${steal}`;
    },
  };
}

/** The preset picker above the panel: loads on the Rodeo, as Program Change does. */
function mountPresets(box: HTMLElement): () => void {
  const label = Object.assign(document.createElement('span'), { className: 'small dim', textContent: 'preset' });
  const sel = Object.assign(document.createElement('select'), {
    id: 'sound-preset',
    title: 'loads it on the Rodeo (Program Change does the same)',
  });
  sel.onchange = () => loadPreset(rodeo, Number(sel.value));
  box.replaceChildren(label, sel);
  let was = '';
  const paint = (s: Snapshot) => {
    const names = presets(s);
    const now = names.join('\n');
    if (now !== was) {
      was = now;
      sel.replaceChildren(
        ...names.map((name, i) => Object.assign(document.createElement('option'), { value: String(i), textContent: name }))
      );
      if (!names.length) sel.append(Object.assign(document.createElement('option'), { textContent: 'no presets on the card' }));
    }
    const d = demoState(s);
    if (d && document.activeElement !== sel) sel.value = String(d.preset);
  };
  const off = rodeo.subscribe(paint);
  paint(rodeo.snapshot);
  return () => {
    off();
    box.replaceChildren();
  };
}

/** rodeo-demo's profile. `span` is the kit selected in the card dialog. */
export function rodeoDemo(span: () => KeySpan | null): Profile {
  return {
    title: 'rodeo-demo',
    groups: GROUPS,
    where: (p) => {
      const spec = KEYS[p.key];
      return spec?.pot ? `pot ${spec.pot}` : (spec?.enc ?? '');
    },
    describe,
    menu: (p, s) => (p.key === 'sound' ? sounds(s).map((x) => x.name) : null),
    readout,
    encoder,
    keySpan: span,
    silent: (note) => {
      const kit = span();
      return !!kit && layer(rodeo.snapshot) === 'sampler' && (note < kit.from || note > kit.to);
    },
    marks: [[ROOT_NOTE, 'root']],
    voices: voiceModel(span),
    commands: [
      { label: 'Log full state', send: 's', title: 'the firmware prints its state' },
      {
        label: 'Hand the card to this computer',
        send: 'd',
        say: 'disk mode — the serial and MIDI ports will close as the Rodeo re-enumerates',
      },
      { label: 'Take the card back', send: 'n' },
    ],
    mountSound: mountPresets,
  };
}
