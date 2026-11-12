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
import type { EncoderLabel, KeySpan, PanelBank, PanelControl, Profile, VoiceModel } from 'rodeo-console/profile';
import { noteName } from './card.ts';
import { ROOT_NOTE, VOICE_COUNT, Voices } from './voices.ts';

const named = (n: string) => () => n;
const def = (s: Snapshot, key: string) => s.params[key]?.def ?? 0;
const now = (s: Snapshot, key: string) => s.state?.p[key];

/** The next of a list parameter's values, round to the first. */
function cycle(s: Snapshot, key: string): number {
  const p = s.params[key];
  const v = now(s, key) ?? p?.min ?? 0;
  return !p || v >= p.max ? (p?.min ?? 0) : v + 1;
}

const flip = (s: Snapshot, key: string) => (now(s, key) ? 0 : 1);

/**
 * Encoders 1-8: bank 1's four, then bank 2's, as panel.h has them (Turn and
 * Tap in panel.cpp). They send detents, not positions: what the page draws is
 * the parameter each one turns, what its tap and held turn do, and the values
 * those set.
 */
export const ENCODERS: PanelControl[] = [
  {
    kind: 'encoder',
    // Turn() turns the shape in synth layer only; sampler and both turn the sound
    turns: ['shape', 'sound'],
    which: (s) => (layer(s) === 'synth' || layer(s) === null ? 'shape' : 'sound'),
    name: (s) => (layer(s) === 'synth' || layer(s) === null ? 'shape' : 'sound'),
    tap: {
      label: 'synth / sampler / both',
      // Tap() keeps synth while the card has no sounds
      act: (s) => [['layer', sounds(s).length ? cycle(s, 'layer') : 0]],
    },
    hold: {
      label: 'preset',
      step(dir, s) {
        const count = presets(s).length;
        const at = demoState(s)?.preset ?? -1;
        if (count) loadPreset(rodeo, (((at + dir) % count) + count) % count);
      },
    },
    shows: ['layer'],
  },
  {
    kind: 'encoder',
    turns: ['detune'],
    name: named('detune'),
    tap: { label: 'detune and octave back', act: (s) => [['detune', def(s, 'detune')], ['octave', 0]] },
    hold: { label: 'octave', key: 'octave' },
    shows: ['octave'],
  },
  {
    kind: 'encoder',
    turns: ['hpf'],
    name: named('high-pass'),
    tap: { label: 'LFO on / off', act: (s) => [['lfo', flip(s, 'lfo')]] },
    shows: ['lfo'],
  },
  {
    kind: 'encoder',
    turns: ['mix'],
    name: named('effects mix'),
    tap: { label: 'chorus / reverb / both', act: (s) => [['fx', cycle(s, 'fx')]] },
    shows: ['fx'],
  },
  {
    kind: 'encoder',
    turns: ['fattack'],
    name: named('filter attack'),
    tap: { label: 'follow the attack', act: () => [['fattack', 0]] },
  },
  {
    kind: 'encoder',
    turns: ['fdecay'],
    name: named('filter decay'),
    tap: { label: 'back to 400 ms', act: (s) => [['fdecay', def(s, 'fdecay')]] },
  },
  {
    kind: 'encoder',
    turns: ['fsustain'],
    name: named('filter sustain'),
    tap: { label: 'back to 25 %', act: (s) => [['fsustain', def(s, 'fsustain')]] },
  },
  {
    kind: 'encoder',
    turns: ['glide'],
    name: named('glide'),
    tap: { label: 'mono on / off', act: (s) => [['mono', flip(s, 'mono')]] },
    shows: ['mono'],
  },
];

const pot = (key: string, name: string): PanelControl => ({ kind: 'pot', turns: [key], name: named(name) });

/** The panel as panel.h lays it out: two pot banks, then two encoder banks. */
export const PANEL: PanelBank[] = [
  {
    title: 'filter',
    controls: [pot('cutoff', 'cutoff'), pot('resonance', 'resonance'), pot('env', 'filter env'), pot('drive', 'drive')],
  },
  {
    title: 'amp envelope',
    controls: [pot('attack', 'attack'), pot('decay', 'decay'), pot('sustain', 'sustain'), pot('release', 'release')],
  },
  { title: 'shape · tone · effects', controls: ENCODERS.slice(0, 4) },
  { title: 'filter envelope · glide', controls: ENCODERS.slice(4) },
];

/** In a preset, but on no control: the panel's last row. */
export const PRESET_ONLY = ['filter', 'keytrack'];

/** Where an encoder's off sits below its first value (panel.cpp's Step()). */
const FIRST: Record<string, number> = { hpf: 20, fattack: 1, glide: 5 };

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

/** One encoder for the chain view: what it turns, its value, and what its tap or hold set. */
export function encoder(n: number, s: Snapshot): EncoderLabel | null {
  const c = ENCODERS[n];
  if (!c) return null; // a third encoder bank: nothing on the panel reads it
  const key = c.which?.(s) ?? c.turns[0];
  const p = s.params[key];
  const v = s.state?.p[key];
  const value = p && v !== undefined ? say(p, v, s) : 'not reported';
  const extra = (c.shows ?? [])
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
  const tap = [c.tap?.label, c.hold && `held: ${c.hold.label}`].filter(Boolean).join('; ');
  return { name: c.name?.(s) ?? key, value, extra, tap };
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
      return `${real ?? predicted}/${VOICE_COUNT}${steal}`;
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
    panel: PANEL,
    panelRest: 'preset only',
    taper: (p) => (FIRST[p.key] ? { lo: FIRST[p.key] } : null),
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
