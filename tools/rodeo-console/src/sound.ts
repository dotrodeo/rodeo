// The parameter panel: every parameter the Rodeo has, set from the page over
// MIDI and following the Rodeo as it moves (the protocol's state stream).
//
// The table comes from the Rodeo (hello), not from here, so any firmware on
// the SDK gets a panel. A profile (profile.ts) can group it the way the
// firmware's own panel is, and put the firmware's own controls above it.

import { rodeo } from './link.ts';
import { describe } from './describe.ts';
import type { Param, Snapshot } from '@dotrodeo/rodeo-host';
import type { Profile } from './profile.ts';

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
let groupsBox: HTMLElement | null = null;
let who: HTMLElement | null = null;
let profile: Profile = {};
let rows = new Map<string, Row>();
let built: Snapshot['hello'] = null; // the hello the controls were built from
let shape = ''; // and the menus' lengths, which a firmware's Max() can change
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

function where(p: Param): string {
  if (profile.where) return profile.where(p);
  return p.pot ? `pot ${p.pot}` : '';
}

/** A menu for a parameter with names, or one the firmware lists itself. */
function menu(p: Param, s: Snapshot): string[] | null {
  return profile.menu?.(p, s) ?? p.names ?? null;
}

function control(p: Param, s: Snapshot): Control {
  const names = menu(p, s);
  if (names) {
    const sel = el('select', { disabled: !names.length });
    names.forEach((n, i) => sel.append(el('option', { value: String(p.min + i), textContent: n })));
    if (!names.length) sel.append(el('option', { textContent: 'nothing to pick' }));
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
  if (r) show(r, v, rodeo.snapshot);
}

/** The value beside a slider; a menu already says its own. */
function show(r: Row, v: number, s: Snapshot) {
  r.value.textContent = r.control instanceof HTMLSelectElement ? '' : describe(r.param, v, s, profile);
}

function groups(params: Param[]): [string, string[]][] {
  if (profile.groups) return profile.groups;
  const pots = params.filter((p) => p.pot).sort((a, b) => (a.pot ?? 0) - (b.pot ?? 0));
  return pots.length ? [['pots', pots.map((p) => p.key)]] : [];
}

function build(s: Snapshot) {
  if (!groupsBox) return;
  rows = new Map();
  const params = Object.values(s.params);
  const named = groups(params);
  const placed = new Set(named.flatMap(([, keys]) => keys));
  const other = params.filter((p) => !placed.has(p.key)).map((p) => p.key);
  const all = other.length ? [...named, [named.length ? 'other' : 'parameters', other] as [string, string[]]] : named;

  const cards = all.map(([title, keys]) => {
    const card = el('div', { className: 'sound-group' }, [el('div', { className: 'sat-head', textContent: title })]);
    for (const key of keys) {
      const p = s.params[key];
      if (!p) continue;
      const c = control(p, s);
      const value = el('output');
      // A menu says its own value, so it takes the value's column too
      const isMenu = c instanceof HTMLSelectElement;
      const row = el('div', { className: isMenu ? 'sound-row menu' : 'sound-row' }, [
        el('span', { className: 'sound-name' }, [key, el('span', { className: 'small dim', textContent: ` ${where(p)}` })]),
        c,
        value,
      ]);
      rows.set(key, { param: p, control: c, value, row });
      card.append(row);
    }
    return card;
  });
  groupsBox.replaceChildren(...cards);
  built = s.hello;
  shape = menus(s);
}

/** The menus' contents, so a list that grows (a card with more sounds) rebuilds. */
function menus(s: Snapshot): string {
  return Object.values(s.params)
    .map((p) => `${p.max}:${menu(p, s)?.join('|') ?? ''}`)
    .join(',');
}

function paint(s: Snapshot) {
  if (!box) return;
  if (s.status !== 'ready' || !s.hello) {
    built = null;
    groupsBox?.replaceChildren();
    box.hidden = true;
    return;
  }
  box.hidden = false;
  if (built !== s.hello || shape !== menus(s)) build(s);
  if (who) who.textContent = `${s.hello.fw} ${s.hello.build ?? ''} · protocol ${s.hello.proto}`;

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
    show(r, v, s);
  }
}

/** Draws into `container` from now on; it stays empty until a Rodeo answers. */
export function mountSound(container: HTMLElement, p: Profile = {}): () => void {
  box = container;
  profile = p;
  box.hidden = true;
  const extras = el('div', { className: 'sound-extras' });
  who = el('span', { className: 'small dim' });
  groupsBox = el('div', { className: 'sound-groups' });
  box.replaceChildren(
    el('div', { className: 'row' }, [extras, el('span', { className: 'grow' }), who]),
    groupsBox
  );
  const unextras = profile.mountSound?.(extras);
  const off = rodeo.subscribe(paint);
  paint(rodeo.snapshot);
  return () => {
    off();
    unextras?.();
    box = groupsBox = who = null;
    built = null;
  };
}
