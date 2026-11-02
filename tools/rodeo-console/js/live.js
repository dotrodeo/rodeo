// Live mode: play the Rodeo from the page over USB-MIDI.
//
// Phase 2 of planning/RODEO_CONSOLE_PLAN.md, first half. Needs no firmware
// change — Rodeo already enumerates as USB-MIDI (bring-up test 08) — and it is
// deliberately the whole of what the firmware listens to and no more, so the
// page cannot imply a control Rodeo does not have.
//
// The chain monitor is the other half, and it waits on the `#S` state line.

import { noteName } from './card.js';
import { LAYERS } from './preset.js';
import * as midi from './midi.js';

const $ = (id) => document.getElementById(id);
const el = (tag, props = {}, kids = []) => {
  const { data, ...rest } = props;
  const n = Object.assign(document.createElement(tag), rest);
  // `dataset` is a getter, so it cannot go through Object.assign.
  for (const [k, v] of Object.entries(data ?? {})) n.dataset[k] = v;
  for (const k of [].concat(kids)) n.append(k);
  return n;
};

const BLACK = new Set([1, 3, 6, 8, 10]);
/** Asking `allocate` about a note nothing can be holding. */
const NO_NOTE = -99;
/** Two rows of the computer keyboard, chromatic from the leftmost white key. */
const TYPING = ['a', 'w', 's', 'e', 'd', 'f', 't', 'g', 'y', 'h', 'u', 'j', 'k', 'o', 'l', 'p', ';'];

const state = {
  access: null,
  out: null,
  base: midi.KIT_FIRST_NOTE, // leftmost key, C2 so a kit's range is on screen
  octaves: 4,
  velocity: 100,
  sustain: false,
  voices: new midi.Voices({ release: 300 }),
  layer: 'synth', // what the Rodeo is set to; the page cannot read it
  down: new Set(), // notes the page is holding, so we can always lift them
  timers: [],
  tick: null,
  library: null,
  toast: () => {},
};

export function supported() {
  return typeof navigator !== 'undefined' && !!navigator.requestMIDIAccess;
}

// ------------------------------------------------------------------- sending

function send(bytes, at) {
  if (!state.out) return;
  try {
    state.out.send(bytes, at);
  } catch (e) {
    state.toast(`the MIDI port refused that: ${e.message}`, true);
  }
}

function play(note, velocity = state.velocity) {
  if (!state.out) return;
  send(midi.noteOn(note, velocity));
  state.down.add(note);
  state.voices.noteOn(note, velocity, performance.now(), sampleOn(note));
  paint();
}

function lift(note) {
  if (!state.down.has(note)) return;
  send(midi.noteOff(note));
  state.down.delete(note);
  state.voices.noteOff(note, performance.now());
  paint();
}

/**
 * Every note the page is holding, lifted, plus the firmware's own panic. Note
 * that `Ev::AllOff` calls `Release` (synth.cpp:218), which drops the gate and
 * lets the amp envelope run — so this releases the notes rather than silencing
 * the board, and a preset with a long release keeps ringing afterwards.
 */
function panic() {
  for (const n of [...state.down]) send(midi.noteOff(n));
  state.down.clear();
  send(midi.allNotesOff());
  state.voices.allOff(performance.now());
  stopPattern();
  paint();
}

function setSustain(down) {
  if (state.sustain === down) return;
  state.sustain = down;
  send(midi.sustain(down));
  state.voices.setSustain(down, performance.now());
  const b = $('live-sustain');
  if (b) b.classList.toggle('on', down);
  paint();
}

/**
 * Whether a key has a sample under it, in the one case where that decides
 * whether the note does anything at all: `Start` returns before `Allocate`
 * only when the layer is Sampler and there is no sample on the key
 * (synth.cpp:158-161). With the oscillator in the layer, every key allocates.
 *
 * Needs two things the page cannot read — the layer and the selected sound —
 * so the layer is asked for and the kit comes from card mode. Null means "do
 * not claim", which is the answer whenever either is unknown.
 */
function sampleOn(note) {
  if (state.layer !== 'sampler') return null;
  const kit = state.library?.kitSpan;
  if (!kit) return null;
  return note >= kit.from && note < kit.from + kit.files;
}

// ------------------------------------------------------------------ patterns

let running = null;

function runPattern(p) {
  if (!state.out) return;
  stopPattern();
  const events = p.build(
    p.id === 'kit' && state.library?.kitSpan ? { files: state.library.kitSpan.files } : {}
  );
  const t0 = performance.now();
  running = p.id;
  for (const e of events) {
    send(e.bytes, t0 + e.at);
    // The model is stepped on its own timer: close enough for a panel that is
    // a prediction anyway, and it keeps the sending path a single call.
    state.timers.push(
      setTimeout(() => {
        const status = e.bytes[0] & 0xf0;
        const now = performance.now();
        if (status === 0x90) state.voices.noteOn(e.bytes[1], e.bytes[2], now, sampleOn(e.bytes[1]));
        else if (status === 0x80) state.voices.noteOff(e.bytes[1], now);
        else if (status === 0xb0 && e.bytes[1] === 64) state.voices.setSustain(e.bytes[2] >= 64, now);
        paint();
      }, e.at)
    );
  }
  const last = events.reduce((m, e) => Math.max(m, e.at), 0);
  state.timers.push(
    setTimeout(() => {
      running = null;
      paint();
    }, last + 50)
  );
  paint();
}

function stopPattern() {
  for (const t of state.timers) clearTimeout(t);
  state.timers = [];
  if (running) {
    send(midi.allNotesOff());
    state.voices.allOff(performance.now());
    running = null;
  }
}

// -------------------------------------------------------------------- ports

async function connect() {
  try {
    state.access = await navigator.requestMIDIAccess({ sysex: false });
  } catch (e) {
    state.toast(`no MIDI access: ${e.message}`, true);
    return;
  }
  state.access.onstatechange = () => {
    // Disk mode takes the MIDI interface away with it (disk.h:7), so a port
    // vanishing here is usually that rather than a fault. Say which.
    const gone = state.out && !ports().some((p) => p.id === state.out.id);
    if (gone) {
      state.out = null;
      state.toast('the Rodeo MIDI port went away — disk mode, or unplugged');
    }
    renderPorts();
  };
  const rodeo = ports().find((p) => midi.isRodeoPort(p.name));
  if (rodeo) state.out = rodeo;
  renderPorts();
  paint();
}

function ports() {
  return state.access ? [...state.access.outputs.values()] : [];
}

// ----------------------------------------------------------------- rendering

function renderPorts() {
  const sel = $('live-port');
  if (!sel) return;
  const list = ports();
  sel.replaceChildren(
    ...(list.length
      ? list.map((p) => el('option', { value: p.id, textContent: p.name, selected: p.id === state.out?.id }))
      : [el('option', { value: '', textContent: 'no MIDI outputs' })])
  );
  sel.disabled = !list.length;
  const hint = $('live-port-hint');
  if (!hint) return;
  if (!state.access) hint.textContent = 'not connected yet';
  else if (!list.length) hint.textContent = 'nothing plugged in — Rodeo shows up as “Rodeo MIDI”';
  else if (state.out && midi.isRodeoPort(state.out.name)) hint.textContent = 'that looks like a Rodeo';
  else if (state.out) hint.textContent = 'not a Rodeo by name — sending anyway';
  else hint.textContent = 'pick a port';
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
  const index = new Map(); // note -> its white key's position, for the black keys
  whites.forEach((n, i) => {
    index.set(n, i);
    row.append(key(n, 'pk-white', i));
  });
  for (let n = from; n <= to; n++) {
    if (!BLACK.has(n % 12)) continue;
    const left = index.get(n - 1);
    if (left === undefined) continue;
    const k = key(n, 'pk-black');
    k.style.left = `calc(${left + 1} * var(--pk-w) - var(--pk-w) * 0.3)`;
    row.append(k);
  }
  row.style.setProperty('--pk-count', whites.length);

  const kit = state.library?.kitSpan;
  if (kit?.files) {
    const last = kit.from + kit.files - 1;
    const at = (n) => {
      // The nearest white key at or below, since a black key has no column.
      for (let m = n; m >= from; m--) if (index.has(m)) return index.get(m);
      return null;
    };
    const a = at(Math.max(kit.from, from));
    const b = at(Math.min(last, to));
    if (a !== null && b !== null && last >= from && kit.from <= to) {
      row.append(
        el('div', {
          className: 'pk-span',
          style: `left: calc(${a} * var(--pk-w)); width: calc(${b - a + 1} * var(--pk-w))`,
          textContent: `${kit.name} · ${noteName(kit.from)}–${noteName(last)}`,
        })
      );
    }
  }
  wrap.append(row);
}

function key(note, className) {
  const quiet = sampleOn(note) === false;
  const k = el('button', {
    className: `pk ${className}`,
    data: { note: String(note) },
    title: `${noteName(note)} · note ${note}${quiet ? ' · no file on this key' : ''}`,
  });
  if (note === midi.ROOT_NOTE)
    k.append(el('span', { className: 'pk-mark', textContent: 'root' }));
  if (note % 12 === 0) k.append(el('span', { className: 'pk-c', textContent: noteName(note) }));
  return k;
}

/**
 * The greyed keys come from a kit read in card mode, which may be minutes stale
 * and is certainly not what Rodeo has selected. Name it rather than imply the
 * page knows.
 */
function renderKitNote() {
  const line = $('live-kit');
  if (!line) return;
  const kit = state.library?.kitSpan;
  if (!kit) {
    line.textContent = '';
    return;
  }
  const bites = state.layer === 'sampler';
  line.textContent =
    `The bar is "${kit.name}" — ${kit.files} files from ${noteName(kit.from)}, from the card ` +
    'you read here, so it only holds if that is the kit the Rodeo has selected. ' +
    (bites
      ? 'In sampler layer a key outside it allocates no voice at all.'
      : `In ${state.layer} layer every key still sounds, because the oscillator is in it.`);
}

function paint() {
  const now = performance.now();
  for (const k of document.querySelectorAll('.pk')) {
    const n = Number(k.dataset.note);
    k.classList.toggle('held', state.down.has(n));
    k.classList.toggle('ringing', !state.down.has(n) && state.voices.slots.some(
      (v) => v.note === n && state.voices.active(v, now)
    ));
  }
  const panel = $('live-voices');
  if (panel) {
    panel.replaceChildren(
      ...state.voices.slots.map((v, i) => {
        const live = state.voices.active(v, now);
        const cls = !live ? 'vc free' : v.gate ? 'vc held' : 'vc ringing';
        return el('div', { className: cls }, [
          el('span', { className: 'vc-n', textContent: String(i + 1) }),
          el('span', {
            className: 'vc-note',
            textContent: live ? noteName(v.note) : '—',
          }),
          el('span', {
            className: 'vc-state',
            textContent: !live ? 'free' : v.gate ? (v.key ? 'held' : 'pedal') : 'release',
          }),
        ]);
      })
    );
  }
  const next = $('live-next');
  if (next) {
    const sounding = state.voices.sounding(now);
    next.textContent =
      sounding < midi.VOICE_COUNT
        ? `${sounding} of ${midi.VOICE_COUNT} sounding`
        : `all ${midi.VOICE_COUNT} sounding — the next note steals ${
            noteName(state.voices.slots[state.voices.allocate(NO_NOTE, now).index].note)
          }`;
  }
  for (const b of document.querySelectorAll('[data-pattern]'))
    b.classList.toggle('on', b.dataset.pattern === running);
  const stop = $('live-stop');
  if (stop) stop.disabled = !running;
}

// A released voice goes quiet on its own, so the panel needs a clock as well
// as events. 10 Hz while anything is up, nothing when the board is silent.
function clock() {
  state.tick = setInterval(() => {
    if (state.down.size || state.voices.sounding(performance.now()) || running) paint();
  }, 100);
}

// ------------------------------------------------------------------- the DOM

function markup() {
  const panel = el('section', { className: 'live', id: 'live' });
  panel.innerHTML = `
    <div class="live-head">
      <div class="row">
        <label for="live-port">Port</label>
        <select id="live-port" disabled></select>
        <button id="live-connect" class="primary">Connect MIDI</button>
        <span class="small dim" id="live-port-hint">not connected yet</span>
      </div>
    </div>

    <div class="pk-wrap" id="live-keys"></div>
    <p class="small dim" id="live-kit"></p>

    <div class="row live-controls">
      <button id="live-oct-down" title="z">− oct</button>
      <button id="live-oct-up" title="x">+ oct</button>
      <label for="live-vel">Velocity</label>
      <input type="range" id="live-vel" min="1" max="127" value="100" />
      <output id="live-vel-out">100</output>
      <label for="live-bend">Bend</label>
      <input type="range" id="live-bend" min="-200" max="200" value="0" />
      <output id="live-bend-out">0.00</output>
      <label for="live-layer">Layer</label>
      <select id="live-layer"></select>
      <button id="live-sustain" title="hold Shift">Sustain</button>
      <button id="live-panic" title="releases every note — a long release still rings">Panic</button>
    </div>

    <div class="live-cols">
      <div>
        <h3>Voices <span class="small dim" id="live-next"></span></h3>
        <div class="voices" id="live-voices"></div>
        <p class="small dim">
          What <code>Allocate()</code> should have done with what the page just sent: the
          same key again takes its old voice back, then any finished voice, then the oldest
          released one, then the oldest of all. A prediction, not a reading — Rodeo does not
          report its voices. The release time below is the only guess in it.
          <label for="live-rel">release</label>
          <input type="number" id="live-rel" min="5" max="4000" value="300" /> ms
        </p>
      </div>
      <div>
        <h3>Test patterns</h3>
        <div class="patterns" id="live-patterns"></div>
        <button id="live-stop" disabled>Stop</button>
      </div>
    </div>

    <div class="understood">
      <h3>Everything Rodeo listens to</h3>
      <table>
        <tbody id="live-understood"></tbody>
      </table>
      <p class="small dim">
        The <code>default:</code> case in <code>Midi()</code> drops the rest — no mod wheel, no
        CC for cutoff, no program change, no aftertouch. The channel is masked off, so any
        channel works. Nothing comes back the other way: the firmware never writes MIDI, so
        this page can send and cannot listen.
      </p>
    </div>
  `;
  return panel;
}

function wire() {
  $('live-connect').onclick = connect;
  $('live-port').onchange = (e) => {
    state.out = ports().find((p) => p.id === e.target.value) ?? null;
    renderPorts();
  };
  $('live-oct-down').onclick = () => shift(-12);
  $('live-oct-up').onclick = () => shift(12);
  $('live-vel').oninput = (e) => {
    state.velocity = Number(e.target.value);
    $('live-vel-out').textContent = String(state.velocity);
  };
  const bend = $('live-bend');
  bend.oninput = (e) => {
    const semis = Number(e.target.value) / 100;
    $('live-bend-out').textContent = semis.toFixed(2);
    send(midi.pitchBend(semis));
  };
  // A wheel springs back; a slider does not, so do it for them.
  const centre = () => {
    bend.value = '0';
    $('live-bend-out').textContent = '0.00';
    send(midi.pitchBend(0));
  };
  bend.onchange = centre;
  bend.onpointerup = centre;
  const layer = $('live-layer');
  layer.replaceChildren(
    ...LAYERS.map((l) => el('option', { value: l, textContent: l, selected: l === state.layer }))
  );
  layer.title = 'what the Rodeo is set to — the page cannot read it, and it changes which keys sound';
  layer.onchange = (e) => {
    state.layer = e.target.value;
    renderKitNote();
    paint();
  };
  $('live-sustain').onclick = () => setSustain(!state.sustain);
  $('live-panic').onclick = panic;
  $('live-stop').onclick = () => {
    stopPattern();
    paint();
  };
  $('live-rel').oninput = (e) => {
    state.voices.release = Math.max(5, Math.min(4000, Number(e.target.value) || 300));
    paint();
  };

  const pats = $('live-patterns');
  for (const p of midi.PATTERNS) {
    pats.append(
      el('div', { className: 'pat' }, [
        el('button', { data: { pattern: p.id }, textContent: p.name, onclick: () => runPattern(p) }),
        el('span', { className: 'small dim', textContent: p.says }),
      ])
    );
  }

  const tb = $('live-understood');
  for (const [what, bytes, note] of midi.UNDERSTOOD) {
    tb.append(
      el('tr', {}, [
        el('td', { textContent: what }),
        el('td', {}, [el('code', { textContent: bytes })]),
        el('td', { className: 'dim', textContent: note }),
      ])
    );
  }

  // Pointer: press, and glide across the keys with the button down.
  const keys = $('live-keys');
  keys.addEventListener('pointerdown', (e) => {
    const k = e.target.closest('.pk');
    if (!k) return;
    e.preventDefault();
    keys.setPointerCapture(e.pointerId);
    play(Number(k.dataset.note));
  });
  keys.addEventListener('pointermove', (e) => {
    if (!e.buttons) return;
    const k = document.elementFromPoint(e.clientX, e.clientY)?.closest('.pk');
    const note = k ? Number(k.dataset.note) : null;
    for (const n of [...state.down]) if (n !== note) lift(n);
    if (note !== null && !state.down.has(note)) play(note);
  });
  const release = () => {
    for (const n of [...state.down]) lift(n);
  };
  keys.addEventListener('pointerup', release);
  keys.addEventListener('pointercancel', release);
}

function shift(by) {
  const next = state.base + by;
  if (next < 0 || next + state.octaves * 12 > 127) return;
  for (const n of [...state.down]) lift(n);
  state.base = next;
  renderKeyboard();
  paint();
}

function onKeyDown(e) {
  if (e.target.matches('input, select, textarea')) return;
  if (e.key === 'Shift' && !e.repeat) return setSustain(true);
  if (e.metaKey || e.ctrlKey || e.altKey || e.repeat) return;
  if (e.key === 'z') return shift(-12);
  if (e.key === 'x') return shift(12);
  const i = TYPING.indexOf(e.key.toLowerCase());
  if (i < 0) return;
  e.preventDefault();
  play(state.base + i);
}

function onKeyUp(e) {
  if (e.key === 'Shift') return setSustain(false);
  const i = TYPING.indexOf(e.key.toLowerCase());
  if (i >= 0) lift(state.base + i);
}

/**
 * The page is holding notes down on real hardware. Anything that takes the
 * keyboard away — switching modes, hiding the tab, closing it — has to lift
 * them, or the Rodeo drones until someone power-cycles it.
 */
function onHide() {
  if (document.visibilityState === 'hidden') panic();
}

export function mount(container, { library = null, toast = () => {} } = {}) {
  state.library = library;
  state.toast = toast;
  container.replaceChildren(markup());
  wire();
  renderPorts();
  renderKeyboard();
  renderKitNote();
  paint();
  clock();
  addEventListener('keydown', onKeyDown);
  addEventListener('keyup', onKeyUp);
  addEventListener('pagehide', panic);
  document.addEventListener('visibilitychange', onHide);
}

export function unmount() {
  panic();
  clearInterval(state.tick);
  state.tick = null;
  removeEventListener('keydown', onKeyDown);
  removeEventListener('keyup', onKeyUp);
  removeEventListener('pagehide', panic);
  document.removeEventListener('visibilitychange', onHide);
}
