// The console: play a Rodeo from the page over USB-MIDI, see its parameters
// and its satellite chain, and reboot it for flashing. Any firmware built on
// the SDK, through the host protocol (rodeo-sdk/docs/guides/host.mdx); a page
// for one firmware embeds this with a Profile (profile.ts) to add what only
// that firmware knows. The rodeo-demo editor does (instruments/rodeo-demo/editor).
//
// The clock, the sequencer and the arp are in seq.ts, and are the page's own:
// the Rodeo just hears notes. The serial port is a log, and the profile's
// commands; everything the panel shows comes over MIDI.

import * as midi from './midi.ts';
import * as seq from './seq.ts';
import * as chain from './chain.ts';
import * as serial from './serial.ts';
import * as link from './link.ts';
import { mountSound } from './sound.ts';
import { describe } from './describe.ts';
import type { Profile } from './profile.ts';
import type { Snapshot } from '@dotrodeo/rodeo-host';

const noteName = midi.noteName;

const $ = <T extends HTMLElement = HTMLElement>(id: string) => document.getElementById(id) as T;

type Kid = Node | string;
/** An element's own properties, plus `style` as text and `data` for dataset. */
type ElProps<K extends keyof HTMLElementTagNameMap> = Partial<
  Omit<HTMLElementTagNameMap[K], 'style'>
> & { style?: string; data?: Record<string, string> };

const el = <K extends keyof HTMLElementTagNameMap>(
  tag: K,
  props: ElProps<K> = {} as ElProps<K>,
  kids: Kid | Kid[] = []
): HTMLElementTagNameMap[K] => {
  const { data, ...rest } = props;
  const n = Object.assign(document.createElement(tag), rest);
  for (const [k, v] of Object.entries(data ?? {})) n.dataset[k] = v;
  for (const k of ([] as Kid[]).concat(kids)) n.append(k);
  return n;
};

const BLACK = new Set([1, 3, 6, 8, 10]);
/** Two rows of the computer keyboard, chromatic from the leftmost white key. */
const TYPING = ['a', 'w', 's', 'e', 'd', 'f', 't', 'g', 'y', 'h', 'u', 'j', 'k', 'o', 'l', 'p', ';'];

/** Schedule this far ahead, and wake up this often to do it. */
const LOOKAHEAD = 140;
const TICK = 25;

let sizer: ResizeObserver | null = null; // keeps the keyboard filling the width it is given

export type Toast = (message: string, bad?: boolean) => void;

type TimerId = ReturnType<typeof setTimeout>;
type IntervalId = ReturnType<typeof setInterval>;

interface Transport {
  running: boolean;
  bpm: number;
  /** Index into seq.DIVISIONS. */
  div: number;
  swing: number;
  gate: number;
  step: number;
  next: number;
  clock: IntervalId | null;
  /** The step on screen, or -1. */
  cursor: number;
}

interface Arp {
  mode: seq.ArpMode;
  octaves: number;
  latch: boolean;
  /** Held notes in the order played. */
  held: number[];
  /** Set once every key is up, so a latched arp starts a new chord. */
  newChord?: boolean;
}

/** The serial side: a log, and the profile's commands. */
interface Wire {
  port: serial.Port | null;
  lines: number;
}

interface LiveState {
  access: MIDIAccess | null;
  out: MIDIOutput | null;
  base: number;
  octaves: number;
  velocity: number;
  sustain: boolean;
  profile: Profile;
  down: Set<number>;
  timers: Set<TimerId>;
  tick: IntervalId | null;
  toast: Toast;
  t: Transport;
  arp: Arp;
  pattern: seq.Pattern;
  root: number;
  wire: Wire;
  /** Detent totals per encoder, from the last chain report, and the turn since. */
  enc: number[];
  turn: number[];
}

const state: LiveState = {
  access: null,
  out: null,
  base: 36, // leftmost key: C2, where rodeo-demo's kits start
  octaves: 4,
  velocity: 100,
  sustain: false,
  profile: {},
  down: new Set(), // notes the page is holding, so we can always lift them
  timers: new Set(),
  tick: null,
  toast: () => {},

  t: { running: false, bpm: 120, div: 3, swing: 0, gate: 0.5, step: 0, next: 0, clock: null, cursor: -1 },
  arp: { mode: 'off', octaves: 1, latch: false, held: [] },
  pattern: seq.emptyPattern(),
  root: 36,
  wire: { port: null, lines: 0 },
  enc: [],
  turn: [],

};

export function supported(): boolean {
  return typeof navigator !== 'undefined' && !!navigator.requestMIDIAccess;
}

// ------------------------------------------------------------------- sending

function send(bytes: number[], at?: number) {
  if (!state.out) return;
  try {
    state.out.send(bytes, at);
  } catch (e) {
    state.toast(`the MIDI port refused that: ${(e as Error).message}`, true);
  }
}

/** A timeout that forgets itself, so a running transport does not leak them. */
function later(fn: () => void, delay: number) {
  const id = setTimeout(() => {
    state.timers.delete(id);
    fn();
  }, Math.max(0, delay));
  state.timers.add(id);
  return id;
}

function clearTimers() {
  for (const id of state.timers) clearTimeout(id);
  state.timers.clear();
}

function play(note: number, velocity = state.velocity) {
  if (!state.out) return;
  noteHeld(note);
  // With the arp running the keyboard feeds it instead of sounding directly.
  if (arpLive()) return paint();
  send(midi.noteOn(note, velocity));
  state.profile.voices?.noteOn(note, velocity, performance.now());
  paint();
}

function lift(note: number) {
  if (!state.down.has(note)) return;
  state.down.delete(note);
  if (!state.arp.latch) state.arp.held = state.arp.held.filter((n) => n !== note);
  if (!arpLive()) {
    send(midi.noteOff(note));
    state.profile.voices?.noteOff(note, performance.now());
  }
  paint();
}

/** Held notes in the order played, which is what `as played` arpeggiates. */
function noteHeld(note: number) {
  state.down.add(note);
  // Latched: a key pressed with nothing else down starts a new chord.
  if (state.arp.latch && state.down.size === 1 && state.arp.newChord) state.arp.held = [];
  state.arp.newChord = false;
  if (!state.arp.held.includes(note)) state.arp.held.push(note);
}

function arpLive() {
  return state.arp.mode !== 'off' && state.t.running;
}

/**
 * Every note the page is holding, lifted, plus the firmware's own panic. Note
 * that `Ev::AllOff` calls `Release` (synth.cpp:218), so this releases the notes
 * rather than silencing the board: a long release keeps ringing afterwards.
 */
function panic() {
  stop();
  for (const n of [...state.down]) send(midi.noteOff(n));
  state.down.clear();
  state.arp.held = [];
  send(midi.allNotesOff());
  state.profile.voices?.allOff(performance.now());
  paint();
}

function setSustain(down: boolean) {
  if (state.sustain === down) return;
  state.sustain = down;
  send(midi.sustain(down));
  state.profile.voices?.setSustain(down, performance.now());
  $('live-sustain')?.classList.toggle('on', down);
  paint();
}

// ----------------------------------------------------------- the transport
//
// A lookahead scheduler: wake every TICK ms, hand the port everything due in
// the next LOOKAHEAD ms with a timestamp, and let Web MIDI place it. setTimeout
// alone would jitter audibly; timestamps do not.

function start() {
  if (!state.out || state.t.running) return;
  state.t.running = true;
  state.t.step = 0;
  state.t.next = performance.now() + 80;
  state.t.clock = setInterval(schedule, TICK);
  paintTransport();
}

function stop() {
  if (!state.t.running) return;
  state.t.running = false;
  clearInterval(state.t.clock ?? undefined);
  state.t.clock = null;
  state.t.cursor = -1;
  clearTimers();
  // Whatever the scheduler had already put on the wire is still coming.
  send(midi.allNotesOff());
  state.profile.voices?.allOff(performance.now());
  for (const n of state.down) send(midi.noteOff(n));
  paintTransport();
  paint();
}

function schedule() {
  const ms = seq.stepMs(state.t.bpm, seq.DIVISIONS[state.t.div].beats);
  const horizon = performance.now() + LOOKAHEAD;
  while (state.t.next < horizon) {
    fire(state.t.step, state.t.next + seq.swingOffset(state.t.step, ms, state.t.swing), ms);
    state.t.step++;
    state.t.next += ms; // swing shifts a step, it must not accumulate
  }
}

function fire(index: number, at: number, ms: number) {
  const notes = seq.stepNotes(state.pattern, index, state.root);
  if (arpLive()) {
    const cycle = seq.arpSequence(state.arp.held, state.arp.mode, state.arp.octaves);
    const n = seq.pickStep(cycle, index, state.arp.mode);
    if (n !== null) notes.push(n);
  }
  const hold = seq.gateMs(ms, state.t.gate);
  for (const n of notes) {
    send(midi.noteOn(n, state.velocity), at);
    send(midi.noteOff(n), at + hold);
  }
  // The voice panel is a model, so step it at the same times, once per step
  // rather than once per note.
  const now = performance.now();
  later(() => {
    state.t.cursor = index % seq.STEPS;
    for (const n of notes) state.profile.voices?.noteOn(n, state.velocity, performance.now());
    paint();
  }, at - now);
  if (notes.length)
    later(() => {
      for (const n of notes) state.profile.voices?.noteOff(n, performance.now());
      paint();
    }, at + hold - now);
}

// -------------------------------------------------------------------- ports

async function connect() {
  // SysEx for the protocol (rodeo/link.ts); if it is refused, notes still play
  try {
    state.access = await navigator.requestMIDIAccess({ sysex: true });
  } catch {
    try {
      state.access = await navigator.requestMIDIAccess({ sysex: false });
      state.toast('MIDI without SysEx: the keyboard plays, but the sound panel needs it');
    } catch (e) {
      state.toast(`no MIDI access: ${(e as Error).message}`, true);
      return;
    }
  }
  // Set just above; the try is what hides that from TypeScript.
  state.access!.onstatechange = () => {
    // Disk mode takes the MIDI interface with it (disk.h:7), so a port
    // vanishing here is usually that rather than a fault. Say which.
    if (state.out && !ports().some((p) => p.id === state.out!.id)) {
      state.out = null;
      stop();
      link.attach(null, null);
      state.toast('the Rodeo MIDI port went away — disk mode, or unplugged');
    }
    // and back again, after disk mode: pick it up without being asked
    if (!state.out) pickRodeo();
    renderPorts();
  };
  pickRodeo();
  renderPorts();
  paint();
}

/** The first port named like a Rodeo, if any, and talk to it. */
function pickRodeo() {
  const rodeo = ports().find((p) => midi.isRodeoPort(p.name));
  if (!rodeo || rodeo === state.out) return;
  state.out = rodeo;
  link.attach(state.access, rodeo);
}

function ports(): MIDIOutput[] {
  return state.access ? [...state.access.outputs.values()] : [];
}

// ----------------------------------------------------------------- rendering

/**
 * A dot instead of a sentence. The sentence still exists as the dot's title,
 * because a colour can say "something is wrong" but not which thing.
 *
 * off: nothing open. on: open. live: open and carrying traffic. bad: open and
 * not what you wanted.
 */
function setDot(id: string, level: string, why: string) {
  const dot = $(id);
  if (!dot) return;
  dot.className = `st ${level}`;
  dot.title = why;
}

function renderPorts() {
  const sel = $<HTMLSelectElement>('live-port');
  if (!sel) return;
  const list = ports();
  sel.replaceChildren(
    ...(list.length
      ? list.map((p) =>
          el('option', { value: p.id, textContent: p.name ?? '', selected: p.id === state.out?.id })
        )
      : [el('option', { value: '', textContent: 'no MIDI outputs' })])
  );
  sel.disabled = !list.length;
  if (!supported()) return setDot('st-midi', 'bad', 'this browser has no Web MIDI — Chrome or Edge');
  if (!state.access) return setDot('st-midi', 'off', 'not connected');
  if (!list.length)
    return setDot('st-midi', 'bad', 'nothing plugged in — a Rodeo’s port has “Rodeo” in its name');
  if (!state.out) return setDot('st-midi', 'off', 'pick a port');
  if (midi.isRodeoPort(state.out.name)) {
    const s = link.rodeo.snapshot;
    if (s.status === 'ready')
      return setDot('st-midi', 'live', `${state.out.name} — ${s.hello?.fw ?? 'a Rodeo'}, talking both ways`);
    if (!link.sysexAllowed())
      return setDot('st-midi', 'on', `${state.out.name} — notes only: SysEx was not allowed, so no panel`);
    if (s.status === 'connecting') return setDot('st-midi', 'on', `${state.out.name} — saying hello…`);
    if (s.status === 'not-rodeo')
      return setDot(
        'st-midi',
        'on',
        `${state.out.name} — plays notes, but did not answer hello: built with RODEO_HOST=0, or older firmware?`
      );
    return setDot('st-midi', 'on', `${state.out.name} — looks like a Rodeo`);
  }
  setDot('st-midi', 'bad', `${state.out.name} — not a Rodeo by name, sending anyway`);
}

function renderKeyboard() {
  const wrap = $('live-keys');
  if (!wrap) return;
  wrap.replaceChildren();
  const from = state.base;
  const to = from + state.octaves * 12;
  const whites = [];
  for (let n = from; n <= to; n++) if (!BLACK.has(n % 12)) whites.push(n);

  const row = el('div', { className: 'pk-row' });
  const index = new Map(); // note -> its white key's column, for the black keys
  whites.forEach((n, i) => {
    index.set(n, i);
    row.append(key(n, 'pk-white'));
  });
  for (let n = from; n <= to; n++) {
    if (!BLACK.has(n % 12)) continue;
    const left = index.get(n - 1);
    if (left === undefined) continue;
    const k = key(n, 'pk-black');
    k.style.left = `calc(${left + 1} * var(--pk-w) - var(--pk-w) * 0.3)`;
    row.append(k);
  }
  row.style.setProperty('--pk-count', String(whites.length));

  // A span (a kit's keys) draws as a bar rather than by recolouring the keys.
  const span = state.profile.keySpan?.();
  if (span && span.to >= span.from) {
    const at = (n: number): number | null => {
      for (let m = n; m >= from; m--) if (index.has(m)) return index.get(m)!; // has() said so
      return null;
    };
    const a = at(Math.max(span.from, from));
    const b = at(Math.min(span.to, to));
    if (a !== null && b !== null && span.to >= from && span.from <= to) {
      row.append(
        el('div', {
          className: 'pk-span',
          style: `left: calc(${a} * var(--pk-w)); width: calc(${b - a + 1} * var(--pk-w))`,
          textContent: span.label,
          title: span.title ?? '',
        })
      );
    }
  }
  wrap.append(row);
  fitKeyboard();
}

/**
 * The black keys are positioned in --pk-w units, so sizing the whole keyboard
 * is a matter of setting that one value: no key is laid out independently.
 * Clamped at both ends — big enough to hit on a narrow window (the wrapper
 * scrolls), small enough not to look silly on a wide one (the row centres).
 */
function fitKeyboard() {
  const wrap = $('live-keys');
  const row = wrap?.querySelector<HTMLElement>('.pk-row');
  if (!row || !wrap.clientWidth) return;
  const count = Number(row.style.getPropertyValue('--pk-count')) || 1;
  const w = Math.max(22, Math.min(54, Math.floor(wrap.clientWidth / count)));
  row.style.setProperty('--pk-w', `${w}px`);
}

function key(note: number, className: string) {
  const quiet = state.profile.silent?.(note) ?? false;
  const k = el('button', {
    className: `pk ${className}`,
    data: { note: String(note) },
    title: `${noteName(note)} · note ${note}${quiet ? ' · nothing on this key' : ''}`,
  });
  const mark = state.profile.marks?.find(([n]) => n === note);
  if (mark) k.append(el('span', { className: 'pk-mark', textContent: mark[1] }));
  if (note % 12 === 0) k.append(el('span', { className: 'pk-c', textContent: noteName(note) }));
  return k;
}

function renderGrid() {
  const grid = $('live-grid');
  if (!grid) return;
  grid.replaceChildren();
  for (let r = 0; r < state.pattern.length; r++) {
    grid.append(
      el('div', { className: 'sq-label', textContent: noteName(seq.rowNote(state.root, r)) })
    );
    for (let s = 0; s < seq.STEPS; s++) {
      grid.append(
        el('button', {
          className: `sq${state.pattern[r][s] ? ' on' : ''}${s % 4 === 0 ? ' beat' : ''}`,
          data: { r: String(r), s: String(s) },
          title: `${noteName(seq.rowNote(state.root, r))} · step ${s + 1}`,
        })
      );
    }
  }
}

function paintGrid() {
  for (const c of document.querySelectorAll<HTMLElement>('.sq')) {
    // Every .sq cell is drawn by renderGrid() with data-r and data-s.
    c.classList.toggle('on', !!state.pattern[+(c.dataset.r as string)][+(c.dataset.s as string)]);
    c.classList.toggle('now', +(c.dataset.s as string) === state.t.cursor);
  }
}

function paintTransport() {
  const b = $('live-play');
  if (!b) return;
  b.textContent = state.t.running ? '■' : '▶';
  b.classList.toggle('on', state.t.running);
}

function paint() {
  const now = performance.now();
  const voices = state.profile.voices;
  const slots = voices?.slots(now) ?? [];
  for (const k of document.querySelectorAll<HTMLElement>('.pk')) {
    const n = Number(k.dataset.note);
    k.classList.toggle('held', state.down.has(n));
    k.classList.toggle('ringing', !state.down.has(n) && slots.some((v) => v.note === n && v.state !== 'free'));
    // Latched notes are being arpeggiated with nothing held down, so the
    // keyboard is the only place that can show what the arp is chewing on.
    k.classList.toggle(
      'latched',
      state.arp.latch && state.arp.mode !== 'off' && !state.down.has(n) && state.arp.held.includes(n)
    );
  }
  const panel = $('live-voices');
  if (panel && voices) {
    panel.replaceChildren(
      ...slots.map((v) =>
        el('div', { className: `vc ${v.state}` }, [
          el('span', { textContent: v.state === 'free' ? '·' : noteName(v.note) }),
        ])
      )
    );
  }
  paintCount(now);
  paintGrid();
}

/** The line beside the voices, which the Rodeo's reports change as well. */
function paintCount(now = performance.now()) {
  const next = $('live-next');
  if (next && state.profile.voices) next.textContent = state.profile.voices.summary(now, link.rodeo.snapshot);
}

// A released voice goes quiet on its own, so the panel needs a clock as well
// as events. 10 Hz while anything is up, nothing when the board is silent.
function clock() {
  state.tick = setInterval(() => {
    const now = performance.now();
    if (state.down.size || (state.profile.voices?.sounding(now) ?? 0) || state.t.running) paint();
  }, 100);
}

// ------------------------------------------------------------------- the DOM

function markup(title: string, toolbar: Node[]) {
  const bar = el('header', { className: 'bar' });
  bar.innerHTML = `
    <h1></h1>
    <div class="conn">
      <button id="live-connect" class="primary">Connect MIDI</button>
      <select id="live-port" disabled aria-label="MIDI output"></select>
      <span class="st" id="st-midi" role="status" aria-label="MIDI"></span>
    </div>
    <span class="sep"></span>
    <div class="conn">
      <button id="live-serial">Connect serial</button>
      <span class="st" id="st-serial" role="status" aria-label="serial"></span>
    </div>
  `;
  bar.querySelector('h1')!.textContent = title; // the markup above has one
  bar.append(...toolbar);

  const panel = el('section', { className: 'live', id: 'live' });
  panel.innerHTML = `
    <p class="warn" id="live-unsupported" hidden>
      This browser has no Web MIDI API, so the keyboard cannot send anything. Chrome,
      Edge, or Safari 17.4 and later can.
    </p>
    <div class="pk-wrap" id="live-keys"></div>

    <div class="row live-controls">
      <button id="live-oct-down" title="octave down (z)">−</button>
      <button id="live-oct-up" title="octave up (x)">+</button>
      <label for="live-vel">vel</label>
      <input type="range" id="live-vel" min="1" max="127" value="100" />
      <output id="live-vel-out">100</output>
      <label for="live-bend">bend</label>
      <input type="range" id="live-bend" min="-200" max="200" value="0" title="±2 semitones on rodeo-demo" />
      <button id="live-sustain" title="CC 64 — or hold Shift">sus</button>
      <button id="live-panic" title="CC 123, and a note off for every key the page holds">panic</button>
    </div>

    <div class="row live-controls">
      <button id="live-play" class="play" title="space">▶</button>
      <label for="live-bpm">bpm</label>
      <input type="number" id="live-bpm" min="40" max="240" value="120" />
      <select id="live-div" title="step length"></select>
      <label for="live-swing">swing</label>
      <input type="range" id="live-swing" min="0" max="75" value="0" />
      <label for="live-gate">gate</label>
      <input type="range" id="live-gate" min="5" max="100" value="50" />
      <label for="live-arp">arp</label>
      <select id="live-arp"></select>
      <input type="number" id="live-arp-oct" min="1" max="4" value="1" title="arp octaves" />
      <button id="live-latch" title="keep arpeggiating after the keys are let go">latch</button>
    </div>

    <div class="sq-wrap">
      <div class="sq-grid" id="live-grid"></div>
      <div class="row sq-foot">
        <button id="live-root-down" title="pattern down an octave">−</button>
        <button id="live-root-up" title="pattern up an octave">+</button>
        <button id="live-clear">clear</button>
        <span class="grow"></span>
        <span class="small dim" id="live-voices-label">voices</span>
        <div class="voices" id="live-voices"></div>
        <span class="small dim" id="live-next"></span>
      </div>
    </div>

    <div class="sound" id="live-sound"></div>

    <div class="chain">
      <div class="row" id="live-cmds">
        <button id="live-dfu" title="the protocol's dfu: the Rodeo answers, then reboots into its bootloader">Reboot for flashing</button>
      </div>
      <p class="small dim said" id="live-said"></p>
      <div class="readout" id="live-readout"></div>
      <div class="sats" id="live-sats"></div>
    </div>
  `;
  const main = el('main', { id: 'main' }, [panel]);
  return [bar, main];
}

function wire() {
  $('live-connect').onclick = connect;
  $('live-port').onchange = (e) => {
    state.out = ports().find((p) => p.id === (e.target as HTMLSelectElement).value) ?? null;
    link.attach(state.access, state.out);
    renderPorts();
  };
  $('live-unsupported').hidden = supported();
  $('live-oct-down').onclick = () => shift(-12);
  $('live-oct-up').onclick = () => shift(12);
  $('live-vel').oninput = (e) => {
    state.velocity = Number((e.target as HTMLInputElement).value);
    $('live-vel-out').textContent = String(state.velocity);
  };

  const bend = $<HTMLInputElement>('live-bend');
  bend.oninput = (e) => send(midi.pitchBend(Number((e.target as HTMLInputElement).value) / 100));
  const centre = () => {
    bend.value = '0';
    send(midi.pitchBend(0));
  };
  bend.onchange = centre; // a wheel springs back; a slider does not
  bend.onpointerup = centre;

  $('live-sustain').onclick = () => setSustain(!state.sustain);
  $('live-panic').onclick = panic;

  // --- transport ---
  $('live-play').onclick = () => (state.t.running ? stop() : start());
  $('live-bpm').oninput = (e) => {
    state.t.bpm = Math.max(seq.BPM_MIN, Math.min(seq.BPM_MAX, Number((e.target as HTMLInputElement).value) || 120));
  };
  const div = $('live-div');
  div.replaceChildren(
    ...seq.DIVISIONS.map((d, i) =>
      el('option', { value: String(i), textContent: d.id, selected: i === state.t.div })
    )
  );
  div.onchange = (e) => (state.t.div = Number((e.target as HTMLSelectElement).value));
  $('live-swing').oninput = (e) => (state.t.swing = Number((e.target as HTMLInputElement).value) / 100);
  $('live-gate').oninput = (e) => (state.t.gate = Number((e.target as HTMLInputElement).value) / 100);
  const arp = $('live-arp');
  arp.replaceChildren(
    ...seq.ARP_MODES.map((m) => el('option', { value: m, textContent: m, selected: m === state.arp.mode }))
  );
  arp.onchange = (e) => {
    // The options are ARP_MODES, so the value is one of them.
    state.arp.mode = (e.target as HTMLSelectElement).value as seq.ArpMode;
    if (state.arp.mode === 'off') state.arp.held = [...state.down];
  };
  $('live-arp-oct').oninput = (e) => (state.arp.octaves = Number((e.target as HTMLInputElement).value) || 1);
  $('live-latch').onclick = (e) => {
    state.arp.latch = !state.arp.latch;
    (e.target as HTMLElement).classList.toggle('on', state.arp.latch);
    if (!state.arp.latch) state.arp.held = [...state.down];
  };

  // --- sequencer ---
  $('live-root-down').onclick = () => setRoot(state.root - 12);
  $('live-root-up').onclick = () => setRoot(state.root + 12);
  $('live-clear').onclick = () => {
    state.pattern = seq.emptyPattern();
    paintGrid();
  };
  for (const id of ['live-voices-label', 'live-voices', 'live-next']) $(id).hidden = !state.profile.voices;

  const grid = $('live-grid');
  let painting: number | null = null; // drag across the grid to draw a line of steps
  const toggle = (cell: HTMLElement, to?: number) => {
    const r = +(cell.dataset.r as string);
    const s = +(cell.dataset.s as string);
    state.pattern[r][s] = to ?? (state.pattern[r][s] ? 0 : 1);
    cell.classList.toggle('on', !!state.pattern[r][s]);
    return state.pattern[r][s];
  };
  grid.addEventListener('pointerdown', (e) => {
    const cell = (e.target as Element).closest<HTMLElement>('.sq');
    if (!cell) return;
    e.preventDefault();
    painting = toggle(cell);
  });
  grid.addEventListener('pointerover', (e) => {
    const cell = (e.target as Element).closest<HTMLElement>('.sq');
    if (cell && painting !== null && e.buttons) toggle(cell, painting);
  });
  addEventListener('pointerup', () => (painting = null));

  // --- the chain row: dfu over MIDI, the profile's commands over serial ---
  $('live-dfu').onclick = () => {
    if (!confirm('Reboot the Rodeo into its bootloader? It leaves USB until it is flashed or power-cycled.'))
      return;
    link.rodeo.dfu();
    state.toast('rebooting for flashing — the MIDI port will go');
  };
  const cmds = $('live-cmds');
  if (state.profile.commands?.length) cmds.append(el('span', { className: 'sep' }));
  for (const c of state.profile.commands ?? [])
    cmds.append(
      el('button', {
        className: 'cmd',
        textContent: c.label,
        title: `${c.title ? `${c.title} · ` : ''}sends ${c.send} on the serial port`,
        onclick: () => cmd(c.send, c.say),
      })
    );
  $('live-serial').onclick = connectSerial;
  $<HTMLButtonElement>('live-serial').disabled = !serial.supported();
  renderSerial();

  // Pointer: press, and glide across the keys with the button down.
  const keys = $('live-keys');
  keys.addEventListener('pointerdown', (e) => {
    const k = (e.target as Element).closest<HTMLElement>('.pk');
    if (!k) return;
    e.preventDefault();
    keys.setPointerCapture(e.pointerId);
    play(Number(k.dataset.note));
  });
  keys.addEventListener('pointermove', (e) => {
    if (!e.buttons) return;
    const k = document.elementFromPoint(e.clientX, e.clientY)?.closest<HTMLElement>('.pk');
    const note = k ? Number(k.dataset.note) : null;
    for (const n of [...state.down]) if (n !== note) lift(n);
    if (note !== null && !state.down.has(note)) play(note);
  });
  const release = () => {
    for (const n of [...state.down]) lift(n);
    state.arp.newChord = true;
  };
  keys.addEventListener('pointerup', release);
  keys.addEventListener('pointercancel', release);
}

function setRoot(next: number) {
  if (next < 0 || seq.rowNote(next, 0) > 127) return;
  state.root = next;
  renderGrid();
}

function shift(by: number) {
  const next = state.base + by;
  if (next < 0 || next + state.octaves * 12 > 127) return;
  for (const n of [...state.down]) lift(n);
  state.base = next;
  renderKeyboard();
  paint();
}

// ---------------------------------------------------------------- serial
//
// Web Serial, as a log: whatever the firmware prints, the last line shown. A
// line starting `#` is for machines (rodeo-demo's `#S` state line) and is not
// shown. The profile's commands go out here.

async function connectSerial() {
  if (state.wire.port) return state.wire.port.close();
  const port = new serial.Port({ onLine, onClose: onSerialClose });
  try {
    await port.connect();
  } catch (e) {
    const err = e as DOMException;
    if (err.name !== 'AbortError') state.toast(`could not open the port: ${err.message}`, true);
    return;
  }
  state.wire.port = port;
  state.wire.lines = 0;
  renderSerial();
}

function cmd(c: string, say?: string) {
  if (!state.wire.port) return;
  if (say) state.toast(say);
  state.wire.port.write(c);
}

function said(text: string) {
  const box = $('live-said');
  if (box) box.textContent = text;
}

function onLine(line: string) {
  const text = line.trim();
  if (!text || text.startsWith('#')) return;
  state.wire.lines++;
  said(text);
  renderSerial();
}

function onSerialClose(why: string | null | undefined) {
  state.wire.port = null;
  if (why) state.toast(why);
  renderSerial();
}

function renderSerial() {
  const b = $<HTMLButtonElement>('live-serial');
  if (!b) return;
  const on = !!state.wire.port;
  b.textContent = on ? 'Disconnect' : 'Connect serial';
  for (const c of document.querySelectorAll<HTMLButtonElement>('.cmd')) c.disabled = !on;
  const dfu = $<HTMLButtonElement>('live-dfu');
  if (dfu) dfu.disabled = link.rodeo.snapshot.status !== 'ready';
  if (!serial.supported())
    return setDot('st-serial', 'bad', 'this browser has no Web Serial — Chrome or Edge');
  if (!on) return setDot('st-serial', 'off', 'not connected — the firmware’s log');
  const lines = `${state.wire.lines} line${state.wire.lines === 1 ? '' : 's'}`;
  setDot('st-serial', state.wire.lines ? 'live' : 'on', `open · ${lines}`);
}

// --------------------------------------------------------------- the chain
//
// The chain report, streamed over MIDI with the state (host.cpp). Pot k is the
// k-th pot slot on the chain, in ring order, which is what a parameter's `pot`
// in the hello names: so a knob can be drawn beside the number it drives.

/**
 * The detent totals are cumulative from power-up, so the difference between
 * two reports is the only part that means anything on screen.
 */
function trackEncoders(c: chain.ChainState) {
  const now: number[] = [];
  for (const at of chain.places(c).encs) now[at.n] = c.s![at.sat].in[at.slot]; // places() found it there
  state.turn = now.map((v, i) => {
    const was = state.enc[i];
    return Number.isFinite(was) ? v - was : 0;
  });
  state.enc = now;
}

function renderReadout(s: Snapshot) {
  const box = $('live-readout');
  if (!box) return;
  if (s.status !== 'ready' || !s.hello) return box.replaceChildren();
  const c: chain.ChainState = s.chain ?? {};
  const health = chain.scanHealth(c);
  const chips: [string, string][] = [[s.hello.fw, s.hello.build ?? '']];
  if (s.state) chips.push(['cpu', `${s.state.cpu}%`]);
  chips.push(...(state.profile.readout?.(s) ?? []));
  if (s.hello.chain) {
    chips.push(['chain', c.state ? (chain.CHAIN_STATES[c.state] ?? c.state) : 'waiting for a report']);
    chips.push(['scans', health === null ? 'none yet' : `${health.toFixed(health > 99.9 ? 2 : 1)}% good`]);
  }
  box.replaceChildren(
    ...chips.map(([k, v]) =>
      el('span', { className: 'chip' }, [el('b', { textContent: k }), el('span', { textContent: v })])
    )
  );
}

function renderSats(s: Snapshot) {
  const box = $('live-sats');
  if (!box) return;
  const c = s.status === 'ready' ? s.chain : null;
  if (!c) return box.replaceChildren();
  const list = Array.isArray(c.s) ? c.s : [];
  const at = chain.places(c);
  const kids = list.map((sat, i) => {
    const pots = at.pots.filter((p) => p.sat === i);
    const encs = at.encs.filter((p) => p.sat === i);
    if (pots.length && pots.some((p) => potParam(s, p.n))) return potCard(sat, i, pots, s);
    if (encs.length) return encCard(sat, i, encs, s);
    return satCard(sat, i);
  });
  const broken = chain.breakAfter(c);
  if (broken !== null)
    kids.push(
      el('div', { className: 'sat broken' }, [
        el('div', { className: 'sat-head', textContent: 'break' }),
        el('div', {
          className: 'small',
          textContent: `the ring does not close after ${broken} satellite${broken === 1 ? '' : 's'}`,
        }),
      ])
    );
  if (!kids.length)
    kids.push(el('p', { className: 'small dim', textContent: 'no satellites on the chain' }));
  box.replaceChildren(...kids);
}

/** The parameter on pot n (from 0), if the firmware put one there. */
function potParam(s: Snapshot, n: number) {
  return Object.values(s.params).find((p) => p.pot === n + 1) ?? null;
}

/**
 * Pots: each knob beside the number it is driving.
 *
 * The pair is the point. Loading a preset sets the parameters and the pots
 * keep their hands off until moved, so a knob can sit at 70% while cutoff is
 * 41%: the panel is not broken, the preset still has it. The state's hold bit
 * says so, and this is the only place it shows.
 *
 * The card is the grid, not each row, so the columns line up down the card.
 */
function potCard(sat: chain.Satellite, i: number, at: chain.Place[], s: Snapshot) {
  const first = at[0].n + 1;
  const card = el('div', { className: 'sat panel pots' }, [
    bankHead(i, `pots ${first}–${first + at.length - 1}`, sat),
  ]);
  const slots = chain.slots(sat);
  for (const place of at) {
    const slot = slots[place.slot];
    const k = place.n + 1;
    if (slot.type === 'fault') {
      card.append(faultRow(slot, `pot ${k}`));
      continue;
    }
    const p = potParam(s, place.n);
    const pct = chain.potPercent(slot.value);
    const v = p ? s.state?.p[p.key] : undefined;
    const held = !!p && !!s.state && !!(s.state.hold & (1 << (k - 1)));
    card.append(
      el('span', { className: 'cell-name', textContent: p ? p.key : `pot ${k}` }),
      el('span', {
        className: `cell-value${held ? ' stale' : ''}`,
        textContent: p && v !== undefined ? describe(p, v, s, state.profile) : '—',
      }),
      el('div', { className: `meter${held ? ' stale' : ''}` }, [el('span', { style: `width:${pct}%` })]),
      el('span', { className: 'cell-knob', textContent: `${pct.toFixed(0)}%` }),
      el('span', {
        className: held ? 'cell-note stale' : 'cell-note',
        textContent: held ? 'preset holds this' : '',
        title: held
          ? 'The preset set this parameter and the knob has not moved since, so the knob is ' +
            'not what you are hearing. Turn it to take it back.'
          : '',
      })
    );
  }
  return card;
}

/**
 * Encoders: no position to compare, so the row is what each one turns (when
 * the profile knows) and whether it is turning now.
 *
 * The report carries a detent total counted from power-up, which on its own
 * says nothing: "+14" is not a state. What is worth seeing is whether the
 * encoder is moving right now, so the total is differenced between reports and
 * shown as the turn that just happened.
 */
function encCard(sat: chain.Satellite, i: number, at: chain.Place[], s: Snapshot) {
  const first = at[0].n + 1;
  const card = el('div', { className: 'sat panel encs' }, [
    bankHead(i, `encoders ${first}–${first + at.length - 1}`, sat),
  ]);
  const slots = chain.slots(sat);
  for (const place of at) {
    const slot = slots[place.slot];
    const k = place.n + 1;
    if (slot.type === 'fault') {
      card.append(faultRow(slot, `E${k}`));
      continue;
    }
    const label = state.profile.encoder?.(place.n, s) ?? null;
    const turn = state.turn[place.n] ?? 0;
    card.append(
      el('span', { className: 'cell-name', textContent: `E${k}${label ? ` ${label.name}` : ''}` }),
      el('span', { className: label ? 'cell-value' : 'cell-value absent' }, [
        el('span', { textContent: label ? label.value : `${slot.value > 0 ? '+' : ''}${slot.value}` }),
        label?.extra ? el('span', { className: 'cell-extra', textContent: label.extra }) : '',
      ]),
      el('span', {
        className: `cell-turn${turn ? ' on' : ''}`,
        textContent: turn ? `${turn > 0 ? '▲' : '▼'} ${Math.abs(turn)}` : '',
        title: 'detents in the last report',
      }),
      el('span', {
        className: `cell-press${slot.pressed ? ' on' : ''}`,
        textContent: 'press',
        title: label?.tap ? `tap: ${label.tap}` : 'its push switch',
      })
    );
  }
  return card;
}

function bankHead(i: number, what: string, sat: chain.Satellite) {
  return el('div', { className: 'sat-head' }, [
    el('span', { textContent: `${i + 1}. ${what}` }),
    el('span', { className: 'small dim', textContent: `fw ${sat.fw}` }),
  ]);
}

function faultRow(slot: chain.Slot, label: string) {
  // Only for a slot whose fault is set (chain.slots).
  return el('div', { className: 'cell-fault bad' }, [
    el('span', { textContent: `${label}: ${chain.faultText(slot.fault as chain.Fault)}` }),
  ]);
}

function satCard(sat: chain.Satellite, i: number) {
  const head = el('div', { className: 'sat-head' }, [
    el('span', { textContent: `${i + 1}. ${chain.kindName(sat.kind)}` }),
    el('span', { className: 'small dim', textContent: `fw ${sat.fw}` }),
  ]);
  const card = el('div', { className: 'sat' }, [head]);
  if (!chain.kindSupported(sat.kind))
    card.append(
      el('div', { className: 'small bad', textContent: 'the stock satellite firmware does not run this kind' })
    );
  for (const slot of chain.slots(sat)) card.append(slotRow(slot));
  return card;
}

function slotRow(slot: chain.Slot) {
  const label = el('span', { className: 'slot-n', textContent: String(slot.index + 1) });
  if (slot.type === 'fault')
    return el('div', { className: 'slot bad' }, [
      label,
      // A 'fault' slot is one whose fault is set (chain.slots).
      el('span', { textContent: chain.faultText(slot.fault as chain.Fault) }),
    ]);
  if (slot.type === 'pot') {
    const pct = chain.potPercent(slot.value);
    return el('div', { className: 'slot' }, [
      label,
      el('div', { className: 'meter' }, [el('span', { style: `width:${pct}%` })]),
      el('span', { className: 'slot-v', textContent: `${pct.toFixed(1)}%` }),
    ]);
  }
  if (slot.type === 'encoder')
    return el('div', { className: `slot${slot.pressed ? ' pressed' : ''}` }, [
      label,
      el('span', { className: 'slot-v wide', textContent: `${slot.value > 0 ? '+' : ''}${slot.value}` }),
      el('span', { className: 'dot' }),
    ]);
  if (slot.type === 'button')
    return el('div', { className: `slot${slot.value ? ' pressed' : ''}` }, [
      label,
      el('span', { className: 'dot big' }),
    ]);
  if (slot.type === 'switch')
    return el('div', { className: 'slot' }, [
      label,
      el('span', { className: 'slot-v', textContent: `position ${slot.value}` }),
    ]);
  // An unknown or custom board: the report cannot say what its slots are, so
  // show the number rather than draw it as something it might not be.
  return el('div', { className: 'slot' }, [label, el('span', { className: 'slot-v', textContent: String(slot.value) })]);
}

// ---------------------------------------------------------------- the window

function onKeyDown(e: KeyboardEvent) {
  if ((e.target as Element).matches('input, select, textarea')) return;
  if (e.key === 'Shift' && !e.repeat) return setSustain(true);
  if (e.metaKey || e.ctrlKey || e.altKey || e.repeat) return;
  if (e.key === ' ') {
    e.preventDefault();
    return state.t.running ? stop() : start();
  }
  if (e.key === 'z') return shift(-12);
  if (e.key === 'x') return shift(12);
  const i = TYPING.indexOf(e.key.toLowerCase());
  if (i < 0) return;
  e.preventDefault();
  play(state.base + i);
}

function onKeyUp(e: KeyboardEvent) {
  if (e.key === 'Shift') return setSustain(false);
  const i = TYPING.indexOf(e.key.toLowerCase());
  if (i < 0) return;
  lift(state.base + i);
  if (!state.down.size) state.arp.newChord = true;
}

/**
 * The page is holding notes down on real hardware. Anything that takes the
 * keyboard away — hiding the tab, closing it — has to lift them, or the Rodeo
 * drones until someone power-cycles it.
 */
function onHide() {
  if (document.visibilityState === 'hidden') panic();
}

let unsound: (() => void) | null = null;
let unrodeo: (() => void) | null = null;
let rodeoWas: string | null = null;
let chainWas: unknown = null;
let saidWas: unknown = null;

/** The MIDI side, many times a second: the port list only when it changes. */
function onRodeo(s: Snapshot) {
  const now = `${s.status}:${!!s.state}`;
  if (now !== rodeoWas) {
    rodeoWas = now;
    renderPorts();
    renderSerial();
  }
  if (s.said && s.said !== saidWas) {
    saidWas = s.said;
    said(s.said.text);
  }
  if (s.chain !== chainWas) {
    chainWas = s.chain;
    if (s.chain) trackEncoders(s.chain);
  }
  renderReadout(s);
  renderSats(s);
  paintCount();
}

export interface Console {
  /** Redraw what the profile decides: the key span, the marks, the voices. */
  refresh(): void;
  unmount(): void;
}

/**
 * Draws the console into `container`: the bar with its connections (and
 * `toolbar` after them), the keyboard, the sequencer, the parameter panel and
 * the chain.
 */
export function mount(
  container: HTMLElement,
  {
    profile = {},
    toast = () => {},
    toolbar = [],
  }: { profile?: Profile; toast?: Toast; toolbar?: Node[] } = {}
): Console {
  state.profile = profile;
  state.toast = toast;
  container.replaceChildren(...markup(profile.title ?? 'rodeo-console', toolbar));
  wire();
  renderPorts();
  renderKeyboard();
  renderGrid();
  paint();
  clock();
  sizer = new ResizeObserver(fitKeyboard);
  sizer.observe($('live-keys'));
  link.rodeo.streamChain(true);
  unsound = mountSound($('live-sound'), profile);
  unrodeo = link.rodeo.subscribe(onRodeo);
  onRodeo(link.rodeo.snapshot);
  addEventListener('keydown', onKeyDown);
  addEventListener('keyup', onKeyUp);
  addEventListener('pagehide', panic);
  document.addEventListener('visibilitychange', onHide);
  return {
    refresh() {
      renderKeyboard();
      paint();
    },
    unmount,
  };
}

function unmount(): void {
  panic();
  sizer?.disconnect();
  sizer = null;
  state.wire.port?.close();
  unsound?.();
  unrodeo?.();
  unsound = unrodeo = null;
  link.attach(null, null);
  clearInterval(state.tick ?? undefined);
  state.tick = null;
  removeEventListener('keydown', onKeyDown);
  removeEventListener('keyup', onKeyUp);
  removeEventListener('pagehide', panic);
  document.removeEventListener('visibilitychange', onHide);
}
