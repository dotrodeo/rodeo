// The sound: every parameter the Rodeo has, set from the page over MIDI and
// following the panel as it moves (the protocol's state stream). Grouped the
// way the panel is (panel.h), with what drives each one beside it.
//
// The table comes from the Rodeo (hello), not from here: a parameter the page
// has never heard of still gets a control, under "other".

import { KEYS } from './preset.ts';
import { rodeo } from './rodeo/link.ts';
import { describe } from './rodeo/audition.ts';
import type { Snapshot } from './rodeo/client.ts';
import type { Param } from './rodeo/protocol.ts';

const GROUPS: [string, string[]][] = [
  ['pot bank 1 · filter', ['cutoff', 'resonance', 'env', 'drive']],
  ['pot bank 2 · amp envelope', ['attack', 'decay', 'sustain', 'release']],
  ['encoder bank 1', ['layer', 'shape', 'sound', 'detune', 'octave', 'hpf', 'lfo', 'mix', 'fx']],
  ['encoder bank 2 · filter envelope', ['fattack', 'fdecay', 'fsustain', 'glide', 'mono']],
  ['preset only', ['filter', 'keytrack']],
];

/** After the page sets a parameter, the stream still carries the old value
 *  for a moment; leave the control alone for this long. */
const QUIET_MS = 400;

type Control = HTMLInputElement | HTMLSelectElement;

interface Row {
  param: Param;
  control: Control;
  value: HTMLOutputElement;
  row: HTMLElement;
}

let box: HTMLElement | null = null;
let rows = new Map<string, Row>();
let built: Snapshot['hello'] = null; // the hello the controls were built from
let soundCount = -1;
const touched = new Map<string, number>(); // key -> when the page last set it

function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  props: Partial<HTMLElementTagNameMap[K]> = {},
  kids: (Node | string)[] = []
): HTMLElementTagNameMap[K] {
  const n = Object.assign(document.createElement(tag), props);
  n.append(...kids);
  return n;
}

function where(key: string): string {
  const spec = KEYS[key];
  return spec?.pot ? `pot ${spec.pot}` : spec?.enc ?? '';
}

function control(p: Param, s: Snapshot): Control {
  if (p.key === 'sound' || p.names) {
    const names = p.key === 'sound' ? s.sounds.map((x) => x.name) : (p.names ?? []);
    const sel = el('select', { disabled: !names.length });
    names.forEach((n, i) =>
      sel.append(el('option', { value: String(p.key === 'sound' ? i : p.min + i), textContent: n }))
    );
    if (!names.length) sel.append(el('option', { textContent: 'no sounds on the card' }));
    sel.onchange = () => set(p.key, Number(sel.value));
    return sel;
  }
  const range = el('input', { type: 'range', min: String(p.min), max: String(p.max), step: '1' });
  range.oninput = () => set(p.key, Number(range.value));
  return range;
}

function set(key: string, v: number) {
  touched.set(key, performance.now());
  rodeo.set(key, v);
  const r = rows.get(key);
  if (r) show(r, v, rodeo.snapshot.sounds);
}

/** The value beside a slider; a menu already says its own. */
function show(r: Row, v: number, sounds: Snapshot['sounds']) {
  r.value.textContent = r.control instanceof HTMLSelectElement ? '' : describe(r.param, v, sounds);
}

function build(s: Snapshot) {
  if (!box) return;
  rows = new Map();
  const params = Object.values(s.params);
  const placed = new Set(GROUPS.flatMap(([, keys]) => keys));
  const other = params.filter((p) => !placed.has(p.key)).map((p) => p.key);
  const groups = other.length ? [...GROUPS, ['other', other] as [string, string[]]] : GROUPS;

  const cards = groups.map(([title, keys]) => {
    const card = el('div', { className: 'sound-group' }, [el('div', { className: 'sat-head', textContent: title })]);
    for (const key of keys) {
      const p = s.params[key];
      if (!p) continue;
      const c = control(p, s);
      const value = el('output');
      const row = el('div', { className: 'sound-row' }, [
        el('span', { className: 'sound-name' }, [key, el('span', { className: 'small dim', textContent: ` ${where(key)}` })]),
        c,
        value,
      ]);
      rows.set(key, { param: p, control: c, value, row });
      card.append(row);
    }
    return card;
  });

  const presets = el('select', { id: 'sound-preset', title: 'loads it on the Rodeo (Program Change does the same)' });
  presets.onchange = () => rodeo.loadPreset(Number(presets.value));
  box.replaceChildren(
    el('div', { className: 'row' }, [
      el('span', { className: 'small dim', textContent: 'preset' }),
      presets,
      el('span', { className: 'grow' }),
      el('span', { className: 'small dim', id: 'sound-who' }),
    ]),
    el('div', { className: 'sound-groups' }, cards)
  );
  built = s.hello;
  soundCount = s.sounds.length;
}

function paint(s: Snapshot) {
  if (!box) return;
  if (s.status !== 'ready' || !s.hello) {
    built = null;
    box.replaceChildren();
    box.hidden = true;
    return;
  }
  box.hidden = false;
  if (built !== s.hello || soundCount !== s.sounds.length) build(s);

  const presets = document.getElementById('sound-preset') as HTMLSelectElement | null;
  if (presets && presets.options.length !== s.presets.length) {
    presets.replaceChildren(
      ...s.presets.map((name, i) => el('option', { value: String(i), textContent: name }))
    );
    if (!s.presets.length) presets.append(el('option', { textContent: 'no presets on the card' }));
  }
  if (presets && s.state && document.activeElement !== presets) presets.value = String(s.state.preset);
  const who = document.getElementById('sound-who');
  if (who) who.textContent = `${s.hello.fw} · protocol ${s.hello.proto}`;

  const st = s.state;
  if (!st) return;
  const now = performance.now();
  for (const [key, r] of rows) {
    const v = st.p[key];
    if (v === undefined) continue;
    // The pot bit: a preset or the page holds this, and the knob is not what
    // you hear until it moves (the same thing the chain view shows)
    const held = !!r.param.pot && !!(st.hold & (1 << (r.param.pot - 1)));
    r.row.classList.toggle('held', held);
    r.row.title = held ? 'held: the knob is not what you hear until it is turned' : '';
    if (now - (touched.get(key) ?? -Infinity) < QUIET_MS) continue;
    r.control.value = String(v);
    show(r, v, s.sounds);
  }
}

/** Draws into `container` from now on; it stays empty until a Rodeo answers. */
export function mountSound(container: HTMLElement): () => void {
  box = container;
  box.hidden = true;
  const off = rodeo.subscribe(paint);
  paint(rodeo.snapshot);
  return () => {
    off();
    box = null;
    built = null;
  };
}
