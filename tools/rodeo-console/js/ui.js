// The page. Everything decided lives in card.js / model.js / preset.js; this
// file only renders it and wires the events.

import { KIT_FIRST_NOTE, MAX_NAME, noteName, playbackSpeed, soundName } from './card.js';
import { buildLibrary, formatBytes, poolStats, validate } from './model.js';
import { KEYS, SHAPES, formatPreset, parsePreset } from './preset.js';
import * as fs from './fs.js';
import { demoCard } from './demo.js';
import * as live from './live.js';
import { decodeAudio, encodeWav, firstTwo, peak, scale, toMono, trimSilence } from './wav.js';

const $ = (id) => document.getElementById(id);
const el = (tag, props = {}, kids = []) => {
  const n = Object.assign(document.createElement(tag), props);
  for (const k of [].concat(kids)) n.append(k);
  return n;
};

/** `?demo` loads a made-up card: no hardware, and nothing can be written. */
const DEMO = new URLSearchParams(location.search).has('demo');

const state = {
  root: null, // the card's directory handle
  card: null, // what readCard gave us
  library: null,
  presets: [],
  selected: null, // { type: 'sound' | 'preset', name }
};

// ------------------------------------------------------------------ toast

let toastTimer;
function toast(message, bad = false) {
  const t = $('toast');
  t.textContent = message;
  t.classList.toggle('bad', bad);
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (t.hidden = true), bad ? 8000 : 4000);
}

// ------------------------------------------------------------- open / read

async function openCard() {
  try {
    state.root = await fs.pickCard();
  } catch (e) {
    if (e.name !== 'AbortError') toast(e.message, true);
    return;
  }
  await reread();
  $('reload').hidden = false;
}

async function reread() {
  if (DEMO) {
    state.card = demoCard();
    state.library = buildLibrary(state.card);
    state.presets = state.card.presets.map((p) => parsePreset(p.text, p.name));
    $('intro').hidden = true;
    $('cols').hidden = false;
    render();
    return;
  }
  if (!state.root) return;
  try {
    state.card = await fs.readCard(state.root, (done, total) => {
      if (total > 20 && done % 10 === 0) toast(`reading ${done} of ${total}…`);
    });
  } catch (e) {
    toast(`could not read the card: ${e.message}`, true);
    return;
  }
  state.library = buildLibrary(state.card);
  state.presets = state.card.presets.map((p) => parsePreset(p.text, p.name));
  for (const u of state.card.unreadable) toast(`${u.name}: ${u.why}`, true);
  $('intro').hidden = true;
  $('cols').hidden = false;
  render();
}

// ---------------------------------------------------------------- rendering

function render() {
  renderPool();
  renderSidebar();
  renderDetail();
  renderIssues();
  paintCardStatus();
  // The keyboard marks the selected kit's range, so it follows the selection.
  live.setLibrary({ kitSpan: kitSpan() });
}

function renderPool() {
  const s = poolStats(state.library);
  $('pool').hidden = false;
  const bar = $('pool').querySelector('.pool-bar');
  bar.classList.toggle('over', s.fraction > 1);
  $('pool-fill').style.width = `${Math.min(100, s.fraction * 100).toFixed(1)}%`;
  // This sits in the one toolbar now, so it has to earn its width.
  $('pool-text').textContent =
    `${formatBytes(s.bytes)}/${formatBytes(s.capacity)} · ${s.files} files · ` +
    `${s.loadSeconds < 1 ? '<1' : s.loadSeconds.toFixed(0)}s`;
  $('pool').title =
    `${formatBytes(s.bytes)} of the Rodeo's ${formatBytes(s.capacity)} sample pool, in ` +
    `${s.files} files, about ${s.loadSeconds < 1 ? 'under a' : s.loadSeconds.toFixed(0)} ` +
    'second(s) to load off the card at power-up';
}

function renderSidebar() {
  const sounds = $('sounds');
  sounds.replaceChildren();
  $('sound-count').textContent = state.library.sounds.length;
  for (const sound of state.library.sounds) {
    const broken = sound.files.every((f) => !f.loaded);
    sounds.append(
      el('li', {}, [
        el(
          'button',
          {
            onclick: () => select('sound', sound.name),
            ariaCurrent: String(state.selected?.type === 'sound' && state.selected.name === sound.name),
          },
          [
            el('span', { className: 'kind', textContent: sound.kit ? 'kit' : 'smp' }),
            el('span', { className: broken ? 'bad' : '', textContent: sound.name }),
          ]
        ),
      ])
    );
  }

  const presets = $('presets');
  presets.replaceChildren();
  $('preset-count').textContent = state.presets.length;
  for (const preset of state.presets) {
    presets.append(
      el('li', {}, [
        el(
          'button',
          {
            onclick: () => select('preset', preset.name),
            ariaCurrent: String(state.selected?.type === 'preset' && state.selected.name === preset.name),
          },
          [el('span', { className: preset.problems.length ? 'bad' : '', textContent: preset.name })]
        ),
      ])
    );
  }
}

function select(type, name) {
  state.selected = { type, name };
  renderSidebar();
  renderDetail();
}

function renderDetail() {
  const box = $('detail');
  box.replaceChildren();
  if (!state.selected) {
    box.append(el('p', { className: 'empty', textContent: 'Pick something on the left.' }));
    return;
  }
  if (state.selected.type === 'sound') renderSound(box);
  else renderPreset(box);
}

// ------------------------------------------------------------------- sound

function renderSound(box) {
  const sound = state.library.sounds.find((s) => s.name === state.selected.name);
  if (!sound) return;

  box.append(el('h2', { textContent: sound.name }));
  box.append(
    el('p', {
      className: 'sub',
      textContent: sound.kit
        ? `kit — ${sound.files.length} files, from ${noteName(KIT_FIRST_NOTE)} up in name order`
        : `pitched sample — plays at its own pitch at ${noteName(60)}`,
    })
  );

  const actions = el('div', { className: 'row' });
  actions.append(
    el('button', {
      textContent: sound.kit ? 'Add files…' : 'Replace…',
      onclick: () => pickFiles((files) => addAudio(files, sound.kit ? sound.name : null)),
    })
  );
  if (sound.kit) {
    actions.append(
      el('button', {
        textContent: 'Renumber to this order',
        onclick: () => renumber(sound),
      })
    );
  }
  actions.append(
    el('button', {
      textContent: 'Delete',
      onclick: () => remove(sound),
    })
  );
  box.append(actions);

  if (sound.kit) box.append(renderKeys(sound));

  const rows = sound.files.map((f) =>
    el('tr', { className: f.loaded ? '' : 'skipped' }, [
      el('td', { className: 'note', textContent: f.note === null ? '—' : noteName(f.note) }),
      el('td', { textContent: f.name }),
      el('td', {
        className: 'dim',
        textContent: f.accepted
          ? `${f.channels === 1 ? 'mono' : `${f.channels} ch`} · ${f.rate} Hz` +
            (f.rate !== 48000 ? ` (×${playbackSpeed(f.rate).toFixed(3)})` : '')
          : '—',
      }),
      el('td', { className: 'dim', textContent: f.frames ? `${(f.frames / f.rate).toFixed(2)} s` : '' }),
      el('td', { className: 'dim', textContent: f.loaded ? formatBytes((f.cost ?? 0) * 2) : (f.why ?? '') }),
    ])
  );
  box.append(
    el('table', {}, [
      el('thead', {}, [
        el('tr', {}, [
          el('th', { textContent: 'key' }),
          el('th', { textContent: 'file' }),
          el('th', { textContent: 'as loaded' }),
          el('th', { textContent: 'length' }),
          el('th', { textContent: 'in pool' }),
        ]),
      ]),
      el('tbody', {}, rows),
    ])
  );
}

/** The keyboard. Drag a key onto another to reorder; the order is the mapping. */
function renderKeys(sound) {
  const wrap = el('div', { className: 'keys' });
  sound.files.forEach((f, i) => {
    const key = el('button', { className: 'key', draggable: true, title: f.name }, [
      el('span', { className: 'n', textContent: noteName(f.note) }),
      el('span', { className: 'f', textContent: f.name }),
    ]);
    key.dataset.index = String(i);
    key.addEventListener('dragstart', (e) => e.dataTransfer.setData('text/plain', String(i)));
    key.addEventListener('dragover', (e) => {
      e.preventDefault();
      key.classList.add('dragover');
    });
    key.addEventListener('dragleave', () => key.classList.remove('dragover'));
    key.addEventListener('drop', (e) => {
      e.preventDefault();
      key.classList.remove('dragover');
      const from = Number(e.dataTransfer.getData('text/plain'));
      if (Number.isNaN(from) || from === i) return;
      const order = sound.files.map((x) => x.name);
      const [moved] = order.splice(from, 1);
      order.splice(i, 0, moved);
      renumber(sound, order);
    });
    wrap.append(key);
  });
  return wrap;
}

async function renumber(sound, order) {
  if (readOnly()) return;
  const names = order ?? sound.files.map((f) => f.name);
  try {
    const done = await fs.renumberKit(state.root, sound.name, names);
    const changed = done.filter((d) => d.from !== d.to).length;
    toast(changed ? `renamed ${changed} file(s) so the keys follow this order` : 'already in order');
    await reread();
    select('sound', sound.name);
  } catch (e) {
    toast(`could not renumber: ${e.message}`, true);
  }
}

async function remove(sound) {
  if (readOnly()) return;
  if (!confirm(`Delete "${sound.name}" from the card? This cannot be undone.`)) return;
  try {
    if (sound.kit) await fs.removeKit(state.root, sound.name);
    else await fs.removeSample(state.root, sound.files[0].name);
    state.selected = null;
    await reread();
    toast(`deleted "${sound.name}"`);
  } catch (e) {
    toast(`could not delete: ${e.message}`, true);
  }
}

// ------------------------------------------------------------------ preset

function renderPreset(box) {
  const preset = state.presets.find((p) => p.name === state.selected.name);
  if (!preset) return;
  const values = { ...preset.values };

  box.append(el('h2', { textContent: preset.name }));
  box.append(el('p', { className: 'sub', textContent: `presets/${preset.name}.txt` }));

  const form = el('div', { className: 'form' });
  const soundNames = state.library.sounds.map((s) => s.name);

  for (const [key, spec] of Object.entries(KEYS)) {
    const where = spec.pot ? `pot ${spec.pot}` : spec.enc ?? 'preset only';
    form.append(
      el('label', { htmlFor: `f-${key}` }, [
        key,
        el('span', { className: 'small dim', textContent: ` · ${where}` }),
      ])
    );
    let input;
    if (key === 'sound') {
      input = el('select', { id: `f-${key}` });
      input.append(el('option', { value: '', textContent: '— pot/encoder chooses —' }));
      for (const n of soundNames) input.append(el('option', { value: n, textContent: n }));
      // A name on the card but not in the list would silently vanish from the
      // form, so keep it and let the validator shout.
      if (values[key] && !soundNames.includes(values[key])) {
        input.append(el('option', { value: values[key], textContent: `${values[key]} (missing)` }));
      }
      input.value = values[key] ?? '';
      input.onchange = () => setValue(preset, key, input.value || undefined);
      form.append(input);
    } else if (spec.kind === 'enum') {
      input = el('select', { id: `f-${key}` });
      input.append(el('option', { value: '', textContent: '— default —' }));
      for (const v of spec.values) input.append(el('option', { value: v, textContent: v }));
      input.value = values[key] ?? '';
      input.onchange = () => setValue(preset, key, input.value || undefined);
      form.append(input);
    } else {
      const pair = el('div', { className: 'pair' });
      const range = el('input', {
        id: `f-${key}`,
        type: 'range',
        min: String(spec.lo),
        max: String(spec.hi),
        step: spec.round ? '1' : String((spec.hi - spec.lo) / 200),
        value: String(values[key] ?? spec.lo),
      });
      const out = el('output', { textContent: values[key] === undefined ? '—' : `${values[key]} ${spec.unit}` });
      range.oninput = () => {
        const v = spec.round ? Math.round(+range.value) : +(+range.value).toFixed(2);
        out.textContent = `${v} ${spec.unit}`;
      };
      range.onchange = () => {
        const v = spec.round ? Math.round(+range.value) : +(+range.value).toFixed(2);
        setValue(preset, key, v);
      };
      const clear = el('button', {
        textContent: '×',
        title: 'leave this to the pot / the default',
        onclick: () => setValue(preset, key, undefined),
      });
      pair.append(range, out, clear);
      form.append(pair);
    }
  }
  box.append(form);

  const actions = el('div', { className: 'row' });
  actions.append(el('button', { textContent: 'Delete', onclick: () => removePreset(preset) }));
  box.append(el('div', { style: 'height:16px' }), actions);

  box.append(
    el('p', {
      className: 'sub',
      textContent:
        'Saved as you change it. A pot parameter left blank stays on its pot; ' +
        'one that is set holds until that pot is moved.',
    })
  );
}

async function setValue(preset, key, value) {
  if (readOnly()) return;
  const values = { ...preset.values };
  if (value === undefined) delete values[key];
  else values[key] = value;
  try {
    await fs.writePreset(state.root, preset.name, formatPreset(values));
    await reread();
    select('preset', preset.name);
  } catch (e) {
    toast(`could not save: ${e.message}`, true);
  }
}

async function removePreset(preset) {
  if (readOnly()) return;
  if (!confirm(`Delete preset "${preset.name}"?`)) return;
  await fs.removePreset(state.root, preset.name);
  state.selected = null;
  await reread();
}

// ------------------------------------------------------------------ issues

function renderIssues() {
  const issues = validate(state.library, state.presets);
  $('issues').hidden = false;
  $('issue-count').textContent = issues.length || '';
  const list = $('issue-list');
  list.replaceChildren();
  if (!issues.length) {
    list.append(
      el('li', { className: 'ok' }, [
        'Nothing the Rodeo would skip. Eject the drive and it will re-read the card.',
      ])
    );
    return;
  }
  for (const issue of issues) {
    list.append(
      el('li', {}, [
        el('span', { className: `badge ${issue.level}`, textContent: issue.level }),
        el('span', { textContent: issue.text }),
      ])
    );
  }
}

// ------------------------------------------------------------- adding audio

function pickFiles(then) {
  const input = el('input', { type: 'file', multiple: true, accept: 'audio/*,.wav,.aif,.aiff,.flac' });
  input.onchange = () => then([...input.files]);
  input.click();
}

/** Decode, convert and write. `kit` null means a pitched sample in /samples. */
async function addAudio(files, kit) {
  if (readOnly()) return;
  if (!files.length) return;
  let added = 0;
  for (const file of files) {
    try {
      const { channels, rate } = await decodeAudio(await file.arrayBuffer());
      let out = firstTwo(channels);
      // A kit hit is almost always better mono: half the pool for no audible
      // loss on a one-shot, and the pool is the scarce thing.
      if (kit) out = toMono(out);
      out = trimSilence(out, rate);
      if (!out[0].length) {
        toast(`${file.name} is silent — skipped`, true);
        continue;
      }
      const p = peak(out);
      if (p > 0 && p < 0.5) out = scale(out, 0.9 / p); // quiet files waste resolution
      const buffer = encodeWav(out, rate);
      const base = file.name.replace(/\.[^.]+$/, '');
      const name = `${base.slice(0, MAX_NAME)}.wav`;
      if (kit) await fs.writeKitFile(state.root, kit, name, buffer);
      else await fs.writeSample(state.root, name, buffer);
      added++;
    } catch (e) {
      toast(`${file.name}: ${e.message}`, true);
    }
  }
  if (added) {
    await reread();
    toast(`added ${added} file(s)${kit ? ` to "${kit}"` : ''}`);
  }
}

// --------------------------------------------------------------- new things

async function newKit() {
  const name = prompt('Name for the new kit?');
  if (!name) return;
  pickFiles(async (files) => {
    if (!files.length) return;
    await addAudio(files, name);
    select('sound', soundName(name));
  });
}

async function newPreset() {
  if (readOnly()) return;
  const name = prompt('Name for the new preset?');
  if (!name) return;
  if (state.presets.some((p) => p.name.toLowerCase() === name.toLowerCase())) {
    toast('a preset with that name is already on the card', true);
    return;
  }
  await fs.writePreset(state.root, name, formatPreset({ layer: 'synth', shape: SHAPES[0] }));
  await reread();
  select('preset', name);
}

/** Writing needs a real card. In demo mode say so instead of throwing. */
function readOnly() {
  if (!DEMO) return false;
  toast('demo card — open a real one to change anything');
  return true;
}

// ------------------------------------------------------------- the card job
//
// One screen. The Rodeo still cannot have the card and the serial port open at
// once — entering disk mode closes CDC and takes USB-MIDI with it — but that
// is a fact about the hardware, not a reason to make the page two places. The
// card work happens in a dialog; the status dots say which connections are up.

/**
 * The keyboard can mark a kit's range, but only from a card read here and only
 * when a kit is what is selected. Rodeo's own selection is not knowable from
 * this page, so this is offered as "from the card you read", not as truth.
 */
function kitSpan() {
  if (state.selected?.type !== 'sound') return null;
  const sound = state.library?.sounds.find((s) => s.name === state.selected.name);
  if (!sound?.kit) return null;
  return { name: sound.name, from: KIT_FIRST_NOTE, files: sound.files.filter((f) => f.loaded).length };
}

/**
 * Grey: nothing open. Yellow: open, or open with only warnings. Red: open with
 * something the Rodeo will actually skip.
 *
 * The counts have to agree with "Before you eject", which lists warnings too —
 * a dot claiming 6 beside a list of 9 just looks broken.
 */
function cardStatus() {
  if (!state.library)
    return ['off', DEMO ? 'demo card — open a real one to change anything' : 'no card open'];
  const issues = validate(state.library, state.presets);
  const errors = issues.filter((i) => i.level === 'error').length;
  const what = `${state.library.sounds.length} sounds, ${state.presets.length} presets`;
  if (errors)
    return ['bad', `${what} — ${errors} the Rodeo would skip, ${issues.length} to look at`];
  if (issues.length) return ['on', `${what} — ${issues.length} to look at, none fatal`];
  return ['on', `${what}, nothing the Rodeo would skip`];
}

function paintCardStatus() {
  const [level, why] = cardStatus();
  const dot = $('st-card');
  dot.className = `st ${level}`;
  dot.title = why;
  $('reload').hidden = !state.root && !DEMO;
}

function openModal() {
  $('card-modal').showModal();
}

// ------------------------------------------------------------------- wiring

// The toolbar button opens the dialog; picking a volume happens inside it,
// since the browser's directory picker needs a gesture either way.
$('open').onclick = () => (state.library ? openModal() : openCard().then(openModal));
$('modal-open').onclick = openCard;
$('reload').onclick = reread;
$('modal-reload').onclick = reread;
$('modal-close').onclick = () => $('card-modal').close();
$('add-sample').onclick = () => pickFiles((f) => addAudio(f, null));
$('add-kit').onclick = newKit;
$('add-preset').onclick = newPreset;

if (DEMO) {
  $('mode').hidden = false;
  $('modal-mode').textContent = 'demo card';
  $('open').textContent = 'Demo card…';
  $('modal-open').textContent = 'Open a real card…';
  reread();
} else if (!fs.supported()) {
  $('unsupported').hidden = false;
  $('modal-open').disabled = true;
}
paintCardStatus();

// Live mode is the screen now, so it mounts once and stays.
if (live.supported()) {
  live.mount($('live-mount'), { library: { kitSpan: kitSpan() }, toast });
} else {
  $('live-intro').hidden = false;
}

// Drag audio anywhere on the page.
let dragDepth = 0;
document.addEventListener('dragenter', (e) => {
  if (!state.root || !e.dataTransfer?.types.includes('Files')) return;
  dragDepth++;
  $('drop').hidden = false;
});
document.addEventListener('dragover', (e) => e.preventDefault());
document.addEventListener('dragleave', () => {
  if (--dragDepth <= 0) {
    dragDepth = 0;
    $('drop').hidden = true;
  }
});
document.addEventListener('drop', async (e) => {
  e.preventDefault();
  dragDepth = 0;
  $('drop').hidden = true;
  if (!state.root) return;
  const files = [...(e.dataTransfer?.files ?? [])];
  const kit =
    state.selected?.type === 'sound' &&
    state.library.sounds.find((s) => s.name === state.selected.name)?.kit
      ? state.selected.name
      : null;
  await addAudio(files, kit);
});
