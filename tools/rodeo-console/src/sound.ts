// The parameter panel: every parameter the Rodeo has, set from the page over
// MIDI and following the Rodeo as it moves (the protocol's state stream).
//
// Drawn as the instrument is: a column per bank, a knob per control, with what
// a tap and a held turn do under its name. A profile (profile.ts) says what the
// banks are; without one, the pots make the columns, four to a bank, in pot
// order. Any parameter no control turns goes in a row underneath: a switch for
// an off/on, a menu for a list, a knob for the rest. The table itself comes from
// the Rodeo (hello), so any firmware on the SDK gets a panel.

import { rodeo } from './link.ts';
import { describe } from './describe.ts';
import { Knob, type Taper } from './knob.ts';
import type { Param, Snapshot } from '@dotrodeo/rodeo-host';
import type { PanelBank, PanelControl, Profile } from './profile.ts';

/** After the page sets a parameter, the stream still carries the old value
 *  for a moment; show the page's own value for this long. */
const QUIET_MS = 400;

interface Cell {
  el: HTMLElement;
  update(s: Snapshot): void;
}

let box: HTMLElement | null = null;
let banksBox: HTMLElement | null = null;
let who: HTMLElement | null = null;
let profile: Profile = {};
let cells: Cell[] = [];
let built: Snapshot['hello'] = null; // the hello the controls were built from
let shape = ''; // and the menus' lengths, which a firmware's Max() can change
const mine = new Map<string, { v: number; at: number }>(); // what the page set, and when

function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  props: Partial<HTMLElementTagNameMap[K]> = {},
  kids: (Node | string)[] = []
): HTMLElementTagNameMap[K] {
  const n = Object.assign(document.createElement(tag), props);
  n.append(...kids);
  return n;
}

// ------------------------------------------------------------------ values

/** The snapshot with what the page has just set laid over the stream. */
function current(s: Snapshot): Snapshot {
  if (!s.state) return s;
  const now = performance.now();
  const p = { ...s.state.p };
  for (const [key, m] of mine) if (now - m.at < QUIET_MS) p[key] = m.v;
  return { ...s, state: { ...s.state, p } };
}

function set(key: string, v: number) {
  mine.set(key, { v, at: performance.now() });
  rodeo.set(key, v);
  paint(rodeo.snapshot);
}

/** A menu for a parameter with names, or one the firmware lists itself. */
function menu(p: Param, s: Snapshot): string[] | null {
  return profile.menu?.(p, s) ?? p.names ?? null;
}

const isToggle = (p: Param, s: Snapshot) => {
  const names = menu(p, s);
  return !!names && names.length === 2 && names[0] === 'off' && names[1] === 'on';
};

/** How a knob moves for `p`: a time or frequency over a wide range goes in ratio. */
function taper(p: Param, s: Snapshot): Taper {
  const names = menu(p, s);
  const max = names ? p.min + Math.max(0, names.length - 1) : p.max;
  const wide = p.max / Math.max(p.min, 1) >= 50;
  const log = !names && wide && (p.unit === 'ms' || p.unit === 'Hz');
  return { min: p.min, max, log, lo: Math.max(1, Math.round(p.max / 1000)), ...(profile.taper?.(p) ?? {}) };
}

const say = (p: Param, v: number, s: Snapshot) => describe(p, v, s, profile);

// ------------------------------------------------------- small controls

/** An off/on as a switch. */
function toggle(key: string, label: string): Cell {
  const b = el('button', { className: 'switch', type: 'button' }, [el('span')]);
  b.setAttribute('role', 'switch');
  b.setAttribute('aria-label', label);
  b.onclick = () => {
    const s = current(rodeo.snapshot);
    const p = s.params[key];
    if (p) set(key, (s.state?.p[key] ?? p.min) === p.min ? p.max : p.min);
  };
  return {
    el: b,
    update(s) {
      const p = s.params[key];
      const v = s.state?.p[key];
      const on = !!p && v !== undefined && v !== p.min;
      b.setAttribute('aria-checked', String(on));
      b.disabled = v === undefined;
    },
  };
}

/** A list as a menu. */
function picker(key: string, label: string): Cell {
  const sel = el('select');
  sel.setAttribute('aria-label', label);
  sel.onchange = () => set(key, Number(sel.value));
  let was = '';
  return {
    el: sel,
    update(s) {
      const p = s.params[key];
      const names = p ? (menu(p, s) ?? []) : [];
      const now = names.join('\n');
      if (now !== was) {
        was = now;
        sel.replaceChildren(...names.map((n, i) => el('option', { value: String(p!.min + i), textContent: n })));
        if (!names.length) sel.append(el('option', { textContent: 'nothing to pick' }));
      }
      sel.disabled = !names.length;
      const v = s.state?.p[key];
      if (v !== undefined && document.activeElement !== sel) sel.value = String(v);
    },
  };
}

/** A small number stepped by − and +, such as an octave. */
function stepper(key: string, label: string): Cell {
  const out = el('output');
  const step = (dir: number) => {
    const s = current(rodeo.snapshot);
    const p = s.params[key];
    const v = s.state?.p[key];
    if (p && v !== undefined) set(key, Math.max(p.min, Math.min(p.max, v + dir)));
  };
  const down = el('button', { type: 'button', textContent: '−', title: `${label} down`, onclick: () => step(-1) });
  const up = el('button', { type: 'button', textContent: '+', title: `${label} up`, onclick: () => step(1) });
  return {
    el: el('span', { className: 'stepper' }, [down, out, up]),
    update(s) {
      const p = s.params[key];
      const v = s.state?.p[key];
      out.textContent = p && v !== undefined ? say(p, v, s) : '–';
    },
  };
}

/** A parameter's own control, by what it is. */
function own(key: string, label: string, s: Snapshot): Cell {
  const p = s.params[key];
  if (p && isToggle(p, s)) return toggle(key, label);
  if (p && menu(p, s)) return picker(key, label);
  if (p && p.max - p.min <= 8) return stepper(key, label);
  return knobCell({ kind: 'pot', turns: [key] }, s);
}

// --------------------------------------------------------------- knobs

const hint = (c: PanelControl) =>
  [c.tap && `tap: ${c.tap.label}`, c.hold && `hold: ${c.hold.label}`].filter(Boolean).join(' · ');

/** One control of a bank: its name, what its tap and hold do, the knob, its
 *  value, and under that whatever its tap or hold set. */
function knobCell(c: PanelControl, s0: Snapshot): Cell {
  const which = (s: Snapshot) => c.which?.(s) ?? c.turns[0];
  const name = el('div', { className: 'ctl-name' });
  const how = el('div', { className: 'ctl-hint', textContent: hint(c) });
  const value = el('output', { className: 'ctl-value' });
  const tap = c.tap;
  const hold = c.hold;
  const knob = new Knob(
    {
      turn: (v) => set(which(current(rodeo.snapshot)), v),
      tap: tap
        ? () => {
            for (const [k, v] of tap.act(current(rodeo.snapshot))) set(k, v);
          }
        : undefined,
      holdTurn: hold
        ? (dir) => {
            const s = current(rodeo.snapshot);
            if (hold.step) return hold.step(dir, s);
            const p = hold.key ? s.params[hold.key] : undefined;
            const v = hold.key ? s.state?.p[hold.key] : undefined;
            if (p && v !== undefined) set(p.key, Math.max(p.min, Math.min(p.max, v + dir)));
          }
        : undefined,
    },
    c.name?.(s0) ?? which(s0)
  );
  knob.el.title = [
    'drag or scroll to turn',
    tap && `click: ${tap.label}`,
    hold && `Shift-drag: held, turn (${hold.label})`,
  ]
    .filter(Boolean)
    .join(' · ');
  const shows = (c.shows ?? []).map((k) => {
    const ctl = own(k, k, s0);
    return { ctl, row: el('label', { className: 'ctl-show' }, [el('span', { textContent: k }), ctl.el]) };
  });
  const cell = el('div', { className: `ctl ${c.kind}` }, [
    name,
    how,
    knob.el,
    value,
    ...(shows.length ? [el('div', { className: 'ctl-shows' }, shows.map((x) => x.row))] : []),
  ]);
  if (!hint(c)) how.remove();
  return {
    el: cell,
    update(s) {
      const key = which(s);
      const p = s.params[key];
      name.textContent = c.name?.(s) ?? key;
      const v = s.state?.p[key];
      if (p && v !== undefined) {
        knob.set(v, taper(p, s));
        value.textContent = say(p, v, s);
      } else value.textContent = p ? 'not reported' : 'not on this firmware';
      // The pot bit: a preset or the page holds this, and the knob on the
      // instrument is not what you hear until it moves.
      const pot = c.kind === 'pot' ? p?.pot : undefined;
      const held = !!pot && !!s.state && !!(s.state.hold & (1 << (pot - 1)));
      cell.classList.toggle('held', held);
      cell.title = held ? 'held by a preset or this page: the pot is not what you hear until it is turned' : '';
      for (const x of shows) x.ctl.update(s);
    },
  };
}

// --------------------------------------------------------------- layout

/** Without a profile: the pots, four to a column, by pot number. */
function genericBanks(params: Param[]): PanelBank[] {
  const pots = params.filter((p) => p.pot).sort((a, b) => (a.pot ?? 0) - (b.pot ?? 0));
  const banks: PanelBank[] = [];
  for (let i = 0; i < pots.length; i += 4)
    banks.push({
      title: `pot bank ${i / 4 + 1}`,
      controls: pots.slice(i, i + 4).map((p) => ({ kind: 'pot', turns: [p.key] })),
    });
  return banks;
}

function build(s: Snapshot) {
  if (!banksBox) return;
  const params = Object.values(s.params);
  const banks = profile.panel ?? genericBanks(params);
  const placed = new Set(
    banks.flatMap((b) => b.controls.flatMap((c) => [...c.turns, ...(c.shows ?? []), ...(c.hold?.key ? [c.hold.key] : [])]))
  );
  const rest = params.filter((p) => !placed.has(p.key));

  cells = [];
  const columns = banks.map((b) => {
    const col = el('div', { className: 'bank' }, [el('div', { className: 'sat-head', textContent: b.title })]);
    for (const c of b.controls) {
      const cell = knobCell(c, s);
      cells.push(cell);
      col.append(cell.el);
    }
    return col;
  });
  const grid = el('div', { className: 'panel-grid' }, columns);
  grid.style.setProperty('--banks', String(Math.max(1, banks.length)));
  grid.style.setProperty('--rows', String(Math.max(1, ...banks.map((b) => b.controls.length))));

  const kids: HTMLElement[] = banks.length ? [grid] : [];
  if (rest.length) {
    const row = el('div', { className: 'panel-rest' });
    for (const p of rest) {
      const ctl = own(p.key, p.key, s);
      cells.push(ctl);
      // a knob draws its own name; the small controls need one
      row.append(ctl.el.classList.contains('ctl') ? ctl.el : el('label', { className: 'ctl rest' }, [el('div', { className: 'ctl-name', textContent: p.key }), ctl.el]));
    }
    const title = profile.panelRest ?? (banks.length ? 'more' : 'parameters');
    kids.push(el('div', { className: 'bank rest-bank' }, [el('div', { className: 'sat-head', textContent: title }), row]));
  }
  banksBox.replaceChildren(...kids);
  built = s.hello;
  shape = menus(s);
}

/** The menus' contents, so a list that grows (a card with more sounds) rebuilds. */
function menus(s: Snapshot): string {
  return Object.values(s.params)
    .map((p) => `${p.max}:${menu(p, s)?.join('|') ?? ''}`)
    .join(',');
}

function paint(snap: Snapshot) {
  if (!box) return;
  if (snap.status !== 'ready' || !snap.hello) {
    built = null;
    banksBox?.replaceChildren();
    box.hidden = true;
    return;
  }
  box.hidden = false;
  if (built !== snap.hello || shape !== menus(snap)) build(snap);
  if (who) who.textContent = `${snap.hello.fw} ${snap.hello.build ?? ''} · protocol ${snap.hello.proto}`;
  const s = current(snap);
  for (const c of cells) c.update(s);
}

/** Draws into `container` from now on; it stays empty until a Rodeo answers. */
export function mountSound(container: HTMLElement, p: Profile = {}): () => void {
  box = container;
  profile = p;
  box.hidden = true;
  const extras = el('div', { className: 'sound-extras' });
  who = el('span', { className: 'small dim' });
  banksBox = el('div', { className: 'sound-banks' });
  box.replaceChildren(
    el('div', { className: 'row' }, [extras, el('span', { className: 'grow' }), who]),
    banksBox
  );
  const unextras = profile.mountSound?.(extras);
  const off = rodeo.subscribe(paint);
  paint(rodeo.snapshot);
  return () => {
    off();
    unextras?.();
    box = banksBox = who = null;
    built = null;
    cells = [];
  };
}
