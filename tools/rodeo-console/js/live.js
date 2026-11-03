// Live mode: play the Rodeo from the page over USB-MIDI.
//
// Phase 2 of planning/RODEO_CONSOLE_PLAN.md, first half. Needs no firmware
// change — Rodeo already enumerates as USB-MIDI (bring-up test 08).
//
// What the firmware acts on is in js/midi.js, which is a copy of `Midi()` and
// `Allocate()`; the clock, the sequencer and the arp are in js/seq.js, which is
// a copy of nothing, because Rodeo has neither. The README says what the page
// can and cannot do and why; this file does not repeat it on screen.
//
// The chain monitor is the other half, and it waits on the `#S` state line
// (planning/RODEO_CONSOLE_STATE_LINE.md).

import { noteName } from './card.js';
import { LAYERS } from './preset.js';
import * as midi from './midi.js';
import * as seq from './seq.js';
import * as chain from './chain.js';
import * as serial from './serial.js';

const $ = (id) => document.getElementById(id);
const el = (tag, props = {}, kids = []) => {
  const { data, ...rest } = props;
  const n = Object.assign(document.createElement(tag), rest);
  for (const [k, v] of Object.entries(data ?? {})) n.dataset[k] = v;
  for (const k of [].concat(kids)) n.append(k);
  return n;
};

const BLACK = new Set([1, 3, 6, 8, 10]);
/** Asking `allocate` about a note nothing can be holding. */
const NO_NOTE = -99;
/** Two rows of the computer keyboard, chromatic from the leftmost white key. */
const TYPING = ['a', 'w', 's', 'e', 'd', 'f', 't', 'g', 'y', 'h', 'u', 'j', 'k', 'o', 'l', 'p', ';'];

/** Schedule this far ahead, and wake up this often to do it. */
const LOOKAHEAD = 140;
const TICK = 25;

const state = {
  access: null,
  out: null,
  base: midi.KIT_FIRST_NOTE, // leftmost key, C2 so a kit's range is on screen
  octaves: 4,
  velocity: 100,
  sustain: false,
  layer: 'synth', // what the Rodeo is set to; the page cannot read it
  voices: new midi.Voices({ release: 300 }),
  down: new Set(), // notes the page is holding, so we can always lift them
  timers: new Set(),
  tick: null,
  library: null,
  toast: () => {},

  t: { running: false, bpm: 120, div: 3, swing: 0, gate: 0.5, step: 0, next: 0, clock: null, cursor: -1 },
  arp: { mode: 'off', octaves: 1, latch: false, held: [] },
  pattern: seq.emptyPattern(),
  root: midi.KIT_FIRST_NOTE,
  testRunning: null, // a test pattern, not the sequencer
  wire: { port: null, state: null, streaming: false, lines: 0, skipped: 0 },

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

/** A timeout that forgets itself, so a running transport does not leak them. */
function later(fn, delay) {
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

function play(note, velocity = state.velocity) {
  if (!state.out) return;
  noteHeld(note);
  // With the arp running the keyboard feeds it instead of sounding directly.
  if (arpLive()) return paint();
  send(midi.noteOn(note, velocity));
  state.voices.noteOn(note, velocity, performance.now(), sampleOn(note));
  paint();
}

function lift(note) {
  if (!state.down.has(note)) return;
  state.down.delete(note);
  if (!state.arp.latch) state.arp.held = state.arp.held.filter((n) => n !== note);
  if (!arpLive()) {
    send(midi.noteOff(note));
    state.voices.noteOff(note, performance.now());
  }
  paint();
}

/** Held notes in the order played, which is what `as played` arpeggiates. */
function noteHeld(note) {
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
  state.voices.allOff(performance.now());
  paint();
}

function setSustain(down) {
  if (state.sustain === down) return;
  state.sustain = down;
  send(midi.sustain(down));
  state.voices.setSustain(down, performance.now());
  $('live-sustain')?.classList.toggle('on', down);
  paint();
}

/**
 * Whether a key has a sample under it, in the one case where that decides
 * whether the note does anything: `Start` returns before `Allocate` only when
 * the layer is Sampler and the key has nothing on it (synth.cpp:158-161).
 * Null means "do not claim", which is the answer whenever the layer or the
 * selected sound is unknown — and the sound always is.
 */
function sampleOn(note) {
  if (state.layer !== 'sampler') return null;
  const kit = state.library?.kitSpan;
  if (!kit) return null;
  return note >= kit.from && note < kit.from + kit.files;
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
  clearInterval(state.t.clock);
  state.t.clock = null;
  state.t.cursor = -1;
  clearTimers();
  // Whatever the scheduler had already put on the wire is still coming.
  send(midi.allNotesOff());
  state.voices.allOff(performance.now());
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

function fire(index, at, ms) {
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
    for (const n of notes) state.voices.noteOn(n, state.velocity, performance.now(), sampleOn(n));
    paint();
  }, at - now);
  if (notes.length)
    later(() => {
      for (const n of notes) state.voices.noteOff(n, performance.now());
      paint();
    }, at + hold - now);
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
    // Disk mode takes the MIDI interface with it (disk.h:7), so a port
    // vanishing here is usually that rather than a fault. Say which.
    if (state.out && !ports().some((p) => p.id === state.out.id)) {
      state.out = null;
      stop();
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
      ? list.map((p) =>
          el('option', { value: p.id, textContent: p.name, selected: p.id === state.out?.id })
        )
      : [el('option', { value: '', textContent: 'no MIDI outputs' })])
  );
  sel.disabled = !list.length;
  const hint = $('live-port-hint');
  if (!hint) return;
  if (!state.access) hint.textContent = '';
  else if (!list.length) hint.textContent = 'nothing plugged in — Rodeo is “Rodeo MIDI”';
  else if (state.out && midi.isRodeoPort(state.out.name)) hint.textContent = 'looks like a Rodeo';
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
  row.style.setProperty('--pk-count', whites.length);

  // A kit's range is always contiguous from C2, so it draws as a bar rather
  // than by recolouring two thirds of the keys.
  const kit = state.library?.kitSpan;
  if (kit?.files) {
    const last = kit.from + kit.files - 1;
    const at = (n) => {
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
          title:
            `${kit.files} files, from the card read here. In sampler layer a key ` +
            'outside the bar allocates no voice at all.',
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
  if (note === midi.ROOT_NOTE) k.append(el('span', { className: 'pk-mark', textContent: 'root' }));
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
  for (const c of document.querySelectorAll('.sq')) {
    c.classList.toggle('on', !!state.pattern[+c.dataset.r][+c.dataset.s]);
    c.classList.toggle('now', +c.dataset.s === state.t.cursor);
  }
}

function paintTransport() {
  const b = $('live-play');
  if (!b) return;
  b.textContent = state.t.running ? '■' : '▶';
  b.classList.toggle('on', state.t.running);
}

/**
 * The Rodeo reports `voices` in the state line; the model only predicts it.
 * Where both exist the Rodeo wins, and the chips stay a prediction of *which*
 * notes are where — the part nothing reports. Separate from paint() because a
 * state line arrives 20 times a second and only this depends on it.
 */
function paintCount() {
  const next = $('live-next');
  if (!next) return;
  const now = performance.now();
  const real = state.wire.state?.voices;
  const predicted = state.voices.sounding(now);
  const steal =
    predicted >= midi.VOICE_COUNT
      ? ` — next steals ${noteName(state.voices.slots[state.voices.allocate(NO_NOTE, now).index].note)}`
      : '';
  next.textContent = Number.isFinite(real)
    ? `${real}/${midi.VOICE_COUNT}${steal}`
    : `${predicted}/${midi.VOICE_COUNT} predicted${steal}`;
}

function paint() {
  const now = performance.now();
  for (const k of document.querySelectorAll('.pk')) {
    const n = Number(k.dataset.note);
    k.classList.toggle('held', state.down.has(n));
    k.classList.toggle(
      'ringing',
      !state.down.has(n) && state.voices.slots.some((v) => v.note === n && state.voices.active(v, now))
    );
    // Latched notes are being arpeggiated with nothing held down, so the
    // keyboard is the only place that can show what the arp is chewing on.
    k.classList.toggle(
      'latched',
      state.arp.latch && state.arp.mode !== 'off' && !state.down.has(n) && state.arp.held.includes(n)
    );
  }
  const panel = $('live-voices');
  if (panel) {
    panel.replaceChildren(
      ...state.voices.slots.map((v) => {
        const live = state.voices.active(v, now);
        return el(
          'div',
          { className: !live ? 'vc free' : v.gate ? 'vc held' : 'vc ringing' },
          [el('span', { textContent: live ? noteName(v.note) : '·' })]
        );
      })
    );
  }
  paintCount();
  for (const b of document.querySelectorAll('[data-pattern]'))
    b.classList.toggle('on', b.dataset.pattern === state.testRunning);
  paintGrid();
}

// A released voice goes quiet on its own, so the panel needs a clock as well
// as events. 10 Hz while anything is up, nothing when the board is silent.
function clock() {
  state.tick = setInterval(() => {
    if (state.down.size || state.voices.sounding(performance.now()) || state.t.running) paint();
  }, 100);
}

// ------------------------------------------------------------------- the DOM

function markup() {
  const panel = el('section', { className: 'live', id: 'live' });
  panel.innerHTML = `
    <div class="row">
      <select id="live-port" disabled></select>
      <button id="live-connect" class="primary">Connect MIDI</button>
      <span class="small dim" id="live-port-hint"></span>
    </div>

    <div class="pk-wrap" id="live-keys"></div>

    <div class="row live-controls">
      <button id="live-oct-down" title="octave down (z)">−</button>
      <button id="live-oct-up" title="octave up (x)">+</button>
      <label for="live-vel">vel</label>
      <input type="range" id="live-vel" min="1" max="127" value="100" />
      <output id="live-vel-out">100</output>
      <label for="live-bend">bend</label>
      <input type="range" id="live-bend" min="-200" max="200" value="0" title="±2 semitones, all Rodeo has" />
      <label for="live-layer">layer</label>
      <select id="live-layer" title="what the Rodeo is set to — the page cannot read it"></select>
      <button id="live-sustain" title="CC 64 — or hold Shift">sus</button>
      <button id="live-panic" title="CC 123 — releases the notes, so a long release still rings">panic</button>
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
        <span class="small dim">voices</span>
        <div class="voices" id="live-voices"></div>
        <span class="small dim" id="live-next"></span>
        <label for="live-rel" class="small dim" title="how long a released voice keeps sounding; the only guess in the voice panel">rel</label>
        <input type="number" id="live-rel" min="5" max="4000" value="300" />
      </div>
    </div>

    <div class="chain">
      <div class="row">
        <button id="live-serial">Connect serial</button>
        <span class="small dim" id="live-serial-hint">the chain, live — Chrome only</span>
        <span class="grow"></span>
        <span class="small dim">send</span>
        <button class="cmd" data-cmd="s" title="state, as prose for a terminal">s</button>
        <button class="cmd" data-cmd="j" title="one state line">j</button>
        <button class="cmd" data-cmd="J" id="live-stream" title="stream state lines, ~20/s">J</button>
        <button class="cmd" data-cmd="d" title="disk mode — this closes the serial port">d</button>
        <button class="cmd" data-cmd="n" title="leave disk mode without ejecting">n</button>
        <button class="cmd" data-cmd="B" title="reboot into the DFU bootloader">B</button>
      </div>
      <div class="readout" id="live-readout"></div>
      <div class="sats" id="live-sats"></div>
    </div>

    <div class="row live-pats" id="live-patterns">
      <span class="small dim">test</span>
    </div>
    <p class="small dim">
      Rodeo takes notes, velocity, bend over its fixed ±2, CC 64 and CC 120/123. Nothing else,
      and it sends nothing back.
    </p>
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
  bend.oninput = (e) => send(midi.pitchBend(Number(e.target.value) / 100));
  const centre = () => {
    bend.value = '0';
    send(midi.pitchBend(0));
  };
  bend.onchange = centre; // a wheel springs back; a slider does not
  bend.onpointerup = centre;

  const layer = $('live-layer');
  layer.replaceChildren(
    ...LAYERS.map((l) => el('option', { value: l, textContent: l, selected: l === state.layer }))
  );
  layer.onchange = (e) => {
    state.layer = e.target.value;
    renderKeyboard();
    paint();
  };
  $('live-sustain').onclick = () => setSustain(!state.sustain);
  $('live-panic').onclick = panic;

  // --- transport ---
  $('live-play').onclick = () => (state.t.running ? stop() : start());
  $('live-bpm').oninput = (e) => {
    state.t.bpm = Math.max(seq.BPM_MIN, Math.min(seq.BPM_MAX, Number(e.target.value) || 120));
  };
  const div = $('live-div');
  div.replaceChildren(
    ...seq.DIVISIONS.map((d, i) =>
      el('option', { value: String(i), textContent: d.id, selected: i === state.t.div })
    )
  );
  div.onchange = (e) => (state.t.div = Number(e.target.value));
  $('live-swing').oninput = (e) => (state.t.swing = Number(e.target.value) / 100);
  $('live-gate').oninput = (e) => (state.t.gate = Number(e.target.value) / 100);
  const arp = $('live-arp');
  arp.replaceChildren(
    ...seq.ARP_MODES.map((m) => el('option', { value: m, textContent: m, selected: m === state.arp.mode }))
  );
  arp.onchange = (e) => {
    state.arp.mode = e.target.value;
    if (state.arp.mode === 'off') state.arp.held = [...state.down];
  };
  $('live-arp-oct').oninput = (e) => (state.arp.octaves = Number(e.target.value) || 1);
  $('live-latch').onclick = (e) => {
    state.arp.latch = !state.arp.latch;
    e.target.classList.toggle('on', state.arp.latch);
    if (!state.arp.latch) state.arp.held = [...state.down];
  };

  // --- sequencer ---
  $('live-root-down').onclick = () => setRoot(state.root - 12);
  $('live-root-up').onclick = () => setRoot(state.root + 12);
  $('live-clear').onclick = () => {
    state.pattern = seq.emptyPattern();
    paintGrid();
  };
  $('live-rel').oninput = (e) => {
    state.voices.release = Math.max(5, Math.min(4000, Number(e.target.value) || 300));
    paint();
  };

  const grid = $('live-grid');
  let painting = null; // drag across the grid to draw a line of steps
  const toggle = (cell, to) => {
    const r = +cell.dataset.r;
    const s = +cell.dataset.s;
    state.pattern[r][s] = to ?? (state.pattern[r][s] ? 0 : 1);
    cell.classList.toggle('on', !!state.pattern[r][s]);
    return state.pattern[r][s];
  };
  grid.addEventListener('pointerdown', (e) => {
    const cell = e.target.closest('.sq');
    if (!cell) return;
    e.preventDefault();
    painting = toggle(cell);
  });
  grid.addEventListener('pointerover', (e) => {
    const cell = e.target.closest('.sq');
    if (cell && painting !== null && e.buttons) toggle(cell, painting);
  });
  addEventListener('pointerup', () => (painting = null));

  $('live-serial').onclick = connectSerial;
  $('live-serial').disabled = !serial.supported();
  for (const b of document.querySelectorAll('.cmd')) b.onclick = () => cmd(b.dataset.cmd);
  renderSerial();

  const pats = $('live-patterns');
  for (const p of midi.PATTERNS) {
    pats.append(
      el('button', {
        data: { pattern: p.id },
        textContent: p.name,
        title: p.says,
        onclick: () => runPattern(p),
      })
    );
  }
  pats.append(el('button', { id: 'live-stop', textContent: 'stop', onclick: stopPattern }));

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
    state.arp.newChord = true;
  };
  keys.addEventListener('pointerup', release);
  keys.addEventListener('pointercancel', release);
}

function setRoot(next) {
  if (next < 0 || seq.rowNote(next, 0) > 127) return;
  state.root = next;
  renderGrid();
}

function shift(by) {
  const next = state.base + by;
  if (next < 0 || next + state.octaves * 12 > 127) return;
  for (const n of [...state.down]) lift(n);
  state.base = next;
  renderKeyboard();
  paint();
}

// --------------------------------------------------------------- the chain
//
// Web Serial, reading the `#S` line the firmware emits (main.cpp's
// StateLine()). The line also carries two things the page was previously
// guessing at — the layer and the release time — so once the port is open
// those controls stop being inputs and start being readouts.

async function connectSerial() {
  if (state.wire.port) return state.wire.port.close();
  const port = new serial.Port({ onLine, onClose: onSerialClose });
  try {
    await port.connect();
  } catch (e) {
    if (e.name !== 'AbortError') state.toast(`could not open the port: ${e.message}`, true);
    return;
  }
  state.wire.port = port;
  state.wire.lines = 0;
  state.wire.skipped = 0;
  // `J` toggles, and the firmware clears it when the port closes, so a freshly
  // opened port is never streaming.
  state.wire.streaming = false;
  cmd('J');
  renderSerial();
}

function cmd(c) {
  if (!state.wire.port) return;
  if (c === 'J') state.wire.streaming = !state.wire.streaming;
  if (c === 'd') state.toast('disk mode — the serial port will close as the Rodeo re-enumerates');
  state.wire.port.write(c);
  renderSerial();
}

function onLine(line) {
  const next = chain.parseStateLine(line);
  if (!next) {
    // Prose, the greeting, or a line the firmware gave up on halfway. All
    // three are normal; none of them is state.
    if (line.startsWith(chain.PREFIX)) state.wire.skipped++;
    return;
  }
  state.wire.lines++;
  state.wire.state = next;
  adoptFromRodeo(next);
  renderSerial(); // the line count, and the controls the Rodeo has taken over
  renderReadout();
  renderSats();
  paintCount();
}

/** What the Rodeo tells us beats what the page was told to assume. */
function adoptFromRodeo(st) {
  if (typeof st.layer === 'string' && LAYERS.includes(st.layer) && st.layer !== state.layer) {
    state.layer = st.layer;
    const sel = $('live-layer');
    if (sel) sel.value = st.layer;
    renderKeyboard();
  }
  if (Number.isFinite(st.rel) && st.rel !== state.voices.release) {
    state.voices.release = st.rel;
    const rel = $('live-rel');
    if (rel) rel.value = String(st.rel);
  }
}

function onSerialClose(why) {
  state.wire.port = null;
  state.wire.streaming = false;
  state.wire.state = null;
  if (why) state.toast(why);
  renderSerial();
  renderReadout();
  renderSats();
}

function renderSerial() {
  const b = $('live-serial');
  if (!b) return;
  const on = !!state.wire.port;
  b.textContent = on ? 'Disconnect' : 'Connect serial';
  b.classList.toggle('on', false);
  $('live-stream')?.classList.toggle('on', state.wire.streaming);
  for (const c of document.querySelectorAll('.cmd')) c.disabled = !on;
  // The Rodeo is the authority on these two once it is talking.
  const owned = on && !!state.wire.state;
  for (const [id, own] of [
    ['live-layer', 'what the Rodeo is set to — the page cannot read it'],
    ['live-rel', 'how long a released voice keeps sounding; the only guess in the voice panel'],
  ]) {
    const e = $(id);
    if (!e) continue;
    e.disabled = owned;
    e.title = owned ? 'read from the Rodeo' : own;
  }
  const hint = $('live-serial-hint');
  if (!hint) return;
  if (!serial.supported()) hint.textContent = 'this browser has no Web Serial — Chrome or Edge';
  else if (!on) hint.textContent = 'the chain, live';
  else if (!state.wire.lines) hint.textContent = 'open — press j or J';
  else
    hint.textContent =
      `${state.wire.streaming ? 'streaming' : 'open'} · ${state.wire.lines} ` +
      `line${state.wire.lines === 1 ? '' : 's'}` +
      (state.wire.skipped ? `, ${state.wire.skipped} truncated` : '');
}

function renderReadout() {
  const box = $('live-readout');
  if (!box) return;
  const st = state.wire.state;
  if (!st) return box.replaceChildren();
  const card = { 0: 'no card', 1: 'card mounted', 2: 'card is the computer\u2019s' }[st.card] ?? '?';
  const c = st.chain ?? {};
  const health = chain.scanHealth(c);
  const chips = [
    [st.preset >= 0 ? `preset ${st.preset + 1}` : 'no preset', st.name || ''],
    ['layer', st.layer],
    ['shape', st.shape],
    ['sound', `${st.sound + 1} of ${st.nsounds}`],
    ['cutoff', `${st.cut}%`],
    ['res', `${st.res}%`],
    ['env', `${st.env}%`],
    ['attack', `${st.atk} ms`],
    ['release', `${st.rel} ms`],
    ['cpu', `${st.cpu}% (peak ${st.peak}%)`],
    ['card', card],
    ['loaded', `${st.loaded} of ${st.samples}${st.skipped ? `, ${st.skipped} skipped` : ''}`],
    ['chain', chain.CHAIN_STATES[c.state] ?? c.state],
    ['scans', health === null ? 'none yet' : `${health.toFixed(health > 99.9 ? 2 : 1)}% good`],
  ];
  box.replaceChildren(
    ...chips.map(([k, v]) =>
      el('span', { className: 'chip' }, [
        el('b', { textContent: k }),
        el('span', { textContent: String(v) }),
      ])
    )
  );
}

function renderSats() {
  const box = $('live-sats');
  if (!box) return;
  const c = state.wire.state?.chain;
  if (!c) return box.replaceChildren();
  if (c.error) return box.replaceChildren(el('p', { className: 'bad', textContent: c.error }));
  const list = Array.isArray(c.s) ? c.s : [];
  const kids = list.map((sat, i) => satCard(sat, i));
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

function satCard(sat, i) {
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

function slotRow(slot) {
  const label = el('span', { className: 'slot-n', textContent: String(slot.index + 1) });
  if (slot.type === 'fault')
    return el('div', { className: 'slot bad' }, [
      label,
      el('span', { textContent: chain.faultText(slot.fault) }),
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
  // An unknown or custom board: the line cannot say what its slots are, so
  // show the number rather than draw it as something it might not be.
  return el('div', { className: 'slot' }, [label, el('span', { className: 'slot-v', textContent: String(slot.value) })]);
}

// ------------------------------------------------------------- test patterns

function runPattern(p) {
  if (!state.out) return;
  stopPattern();
  const events = p.build(
    p.id === 'kit' && state.library?.kitSpan ? { files: state.library.kitSpan.files } : {}
  );
  const t0 = performance.now();
  state.testRunning = p.id;
  for (const e of events) {
    send(e.bytes, t0 + e.at);
    later(() => {
      const status = e.bytes[0] & 0xf0;
      const now = performance.now();
      if (status === 0x90) state.voices.noteOn(e.bytes[1], e.bytes[2], now, sampleOn(e.bytes[1]));
      else if (status === 0x80) state.voices.noteOff(e.bytes[1], now);
      else if (status === 0xb0 && e.bytes[1] === 64) state.voices.setSustain(e.bytes[2] >= 64, now);
      paint();
    }, e.at);
  }
  const last = events.reduce((m, e) => Math.max(m, e.at), 0);
  later(() => {
    state.testRunning = null;
    paint();
  }, last + 50);
  paint();
}

function stopPattern() {
  if (!state.testRunning) return;
  clearTimers();
  send(midi.allNotesOff());
  state.voices.allOff(performance.now());
  state.testRunning = null;
  paint();
}

// ---------------------------------------------------------------- the window

function onKeyDown(e) {
  if (e.target.matches('input, select, textarea')) return;
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

function onKeyUp(e) {
  if (e.key === 'Shift') return setSustain(false);
  const i = TYPING.indexOf(e.key.toLowerCase());
  if (i < 0) return;
  lift(state.base + i);
  if (!state.down.size) state.arp.newChord = true;
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
  renderGrid();
  paint();
  clock();
  addEventListener('keydown', onKeyDown);
  addEventListener('keyup', onKeyUp);
  addEventListener('pagehide', panic);
  document.addEventListener('visibilitychange', onHide);
}

export function unmount() {
  panic();
  state.wire.port?.close();
  clearInterval(state.tick);
  state.tick = null;
  removeEventListener('keydown', onKeyDown);
  removeEventListener('keyup', onKeyUp);
  removeEventListener('pagehide', panic);
  document.removeEventListener('visibilitychange', onHide);
}
