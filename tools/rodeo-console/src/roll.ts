// The piano roll: a piano's 88 keys, A0 to C8, down the side, 2 to 8 bars of sixteenths across.
//
// A canvas, not a grid of buttons: 61 rows by up to 128 steps is nearly eight
// thousand cells, and the play head moves across them eight times a second.
// What the roll holds is seq.ts's Roll; this draws it and edits it.
//
//   click an empty cell    add a note, as long as the last one drawn
//   drag from there        make it longer or shorter
//   drag a note            move it, in time and pitch
//   drag a note's end      change its length
//   click a note           remove it (or right-click it)
//   click a key            hear that note

import * as seq from './seq.ts';
import { noteName } from './midi.ts';

const ROW_H = 14;
const LABEL_W = 44;
const MIN_CELL = 6;
const BLACK = new Set([1, 3, 6, 8, 10]);

export interface RollOptions {
  roll: () => seq.Roll;
  /** The step under the play head, or -1. */
  cursor: () => number;
  audition: (note: number) => void;
  /** Rows with nothing on them for this firmware, drawn dimmer. */
  silent?: (note: number) => boolean;
  /** After a note is added, moved, resized or removed. */
  changed?: () => void;
}

export interface RollView {
  paint(): void;
  /** After the bar count or the box's width changes. */
  resize(): void;
  destroy(): void;
}

interface Drag {
  mode: 'new' | 'move' | 'size';
  n: seq.Note;
  step0: number;
  row0: number;
  start0: number;
  note0: number;
  moved: boolean;
}

export function mountRoll(box: HTMLElement, o: RollOptions): RollView {
  const canvas = document.createElement('canvas');
  box.replaceChildren(canvas);
  const ctx = canvas.getContext('2d')!;
  const css = getComputedStyle(box);
  const v = (name: string, or: string) => css.getPropertyValue(name).trim() || or;
  const C = {
    white: '#1c1c1c',
    black: '#151515',
    quiet: '#101010',
    step: '#262626',
    beat: '#3a3a3a',
    bar: '#5a5a5a',
    octave: '#444',
    keyWhite: v('--paper', '#e0e0e0'),
    keyBlack: '#2a2a2a',
    note: v('--yellow', '#fc0'),
    playing: v('--legend', '#fff'),
    head: v('--cyan', '#0cf'),
  };

  let cell = MIN_CELL;
  let steps = seq.rollSteps(o.roll());
  let lastLen = 2;
  let drag: Drag | null = null;

  function resize() {
    steps = seq.rollSteps(o.roll());
    const avail = box.clientWidth - LABEL_W;
    cell = Math.max(MIN_CELL, Math.floor(avail / steps));
    const w = LABEL_W + cell * steps;
    const h = seq.ROWS * ROW_H;
    const dpr = devicePixelRatio || 1;
    canvas.style.width = `${w}px`;
    canvas.style.height = `${h}px`;
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    paint();
  }

  function paint() {
    const roll = o.roll();
    const w = LABEL_W + cell * steps;
    const h = seq.ROWS * ROW_H;
    const head = o.cursor();

    for (let r = 0; r < seq.ROWS; r++) {
      const note = seq.rowNote(r);
      ctx.fillStyle = o.silent?.(note) ? C.quiet : BLACK.has(note % 12) ? C.black : C.white;
      ctx.fillRect(LABEL_W, r * ROW_H, w - LABEL_W, ROW_H);
    }
    for (let s = 0; s <= steps; s++) {
      ctx.fillStyle = s % seq.STEPS_PER_BAR === 0 ? C.bar : s % 4 === 0 ? C.beat : C.step;
      ctx.fillRect(LABEL_W + s * cell - (s === steps ? 1 : 0), 0, 1, h);
    }
    ctx.fillStyle = C.octave;
    for (let r = 0; r < seq.ROWS; r++) if (seq.rowNote(r) % 12 === 0) ctx.fillRect(LABEL_W, (r + 1) * ROW_H - 1, w, 1);

    if (head >= 0) {
      ctx.globalAlpha = 0.16;
      ctx.fillStyle = C.head;
      ctx.fillRect(LABEL_W + head * cell, 0, cell, h);
      ctx.globalAlpha = 1;
    }

    for (const n of roll.notes) {
      const len = seq.playedLen(roll, n);
      if (!len || !seq.onRoll(n.note)) continue;
      const on = head >= n.start && head < n.start + len;
      ctx.fillStyle = on ? C.playing : C.note;
      ctx.fillRect(LABEL_W + n.start * cell + 1, seq.noteRow(n.note) * ROW_H + 1, len * cell - 1, ROW_H - 2);
    }

    // the keys down the side
    ctx.font = '9px ui-monospace, Menlo, monospace';
    ctx.textBaseline = 'middle';
    for (let r = 0; r < seq.ROWS; r++) {
      const note = seq.rowNote(r);
      const black = BLACK.has(note % 12);
      ctx.globalAlpha = o.silent?.(note) ? 0.45 : 1;
      ctx.fillStyle = black ? C.keyBlack : C.keyWhite;
      ctx.fillRect(0, r * ROW_H, LABEL_W - 2, ROW_H - 1);
      if (note % 12 === 0) {
        ctx.fillStyle = '#111';
        ctx.fillText(noteName(note), 4, r * ROW_H + ROW_H / 2);
      }
    }
    ctx.globalAlpha = 1;
  }

  function at(e: PointerEvent | MouseEvent) {
    const b = canvas.getBoundingClientRect();
    const x = e.clientX - b.left;
    const y = e.clientY - b.top;
    const row = Math.max(0, Math.min(seq.ROWS - 1, Math.floor(y / ROW_H)));
    const step = Math.max(0, Math.min(steps - 1, Math.floor((x - LABEL_W) / cell)));
    return { x, row, step };
  }

  /** The note under the pointer, and whether the pointer is on its end. */
  function hit(p: ReturnType<typeof at>) {
    const roll = o.roll();
    const n = seq.noteAt(roll, seq.rowNote(p.row), p.step);
    if (!n || n.start >= steps) return null;
    const right = LABEL_W + (n.start + seq.playedLen(roll, n)) * cell;
    const grab = Math.min(5, (seq.playedLen(roll, n) * cell) / 4);
    return { n, end: p.x > right - grab };
  }

  canvas.addEventListener('pointerdown', (e) => {
    if (e.button !== 0) return;
    const p = at(e);
    e.preventDefault();
    if (p.x < LABEL_W) return o.audition(seq.rowNote(p.row));
    const roll = o.roll();
    const h = hit(p);
    let n: seq.Note;
    let mode: Drag['mode'];
    if (h) {
      n = h.n;
      mode = h.end ? 'size' : 'move';
    } else {
      n = { note: seq.rowNote(p.row), start: p.step, len: Math.min(lastLen, steps - p.step) };
      roll.notes.push(n);
      mode = 'new';
      o.audition(n.note);
    }
    drag = { mode, n, step0: p.step, row0: p.row, start0: n.start, note0: n.note, moved: false };
    canvas.setPointerCapture(e.pointerId);
    paint();
  });

  canvas.addEventListener('pointermove', (e) => {
    const p = at(e);
    if (!drag) {
      const h = p.x >= LABEL_W ? hit(p) : null;
      canvas.style.cursor = !h ? 'crosshair' : h.end ? 'ew-resize' : 'grab';
      return;
    }
    const { n } = drag;
    if (drag.mode === 'move') {
      const start = Math.max(0, Math.min(steps - 1, drag.start0 + p.step - drag.step0));
      const note = Math.max(seq.NOTE_LO, Math.min(seq.NOTE_HI, drag.note0 + drag.row0 - p.row));
      if (note !== n.note) o.audition(note);
      if (start !== n.start || note !== n.note) drag.moved = true;
      n.start = start;
      n.note = note;
    } else if (drag.moved || p.step !== drag.step0) {
      const len = Math.max(1, Math.min(steps - n.start, p.step - n.start + 1));
      if (len !== n.len) drag.moved = true;
      n.len = len;
    }
    paint();
  });

  const drop = () => {
    if (!drag) return;
    const roll = o.roll();
    const { n } = drag;
    if (drag.mode !== 'new' && !drag.moved) roll.notes = roll.notes.filter((x) => x !== n);
    else {
      seq.place(roll, n);
      lastLen = n.len;
    }
    drag = null;
    paint();
    o.changed?.();
  };
  canvas.addEventListener('pointerup', drop);
  canvas.addEventListener('pointercancel', drop);
  canvas.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    const p = at(e);
    const h = p.x >= LABEL_W ? hit(p) : null;
    if (!h) return;
    const roll = o.roll();
    roll.notes = roll.notes.filter((x) => x !== h.n);
    paint();
    o.changed?.();
  });

  const sizer = new ResizeObserver(() => resize());
  sizer.observe(box);
  resize();
  // Open on C2 to C4: where rodeo-demo's kits and its synth's middle live.
  box.scrollTop = seq.noteRow(60) * ROW_H;

  return {
    paint,
    resize,
    destroy() {
      sizer.disconnect();
      box.replaceChildren();
    },
  };
}
