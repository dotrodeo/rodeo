// A knob: what the parameter panel turns, drawn the way the Rodeo's own are.
//
//   drag up or down     turn it (Shift with an encoder that has a hold: held, turn)
//   wheel, arrow keys   one step
//   click               the tap, on an encoder that has one
//
// Where the knob points is `toPos` of the value, so a time or a frequency moves
// the way the firmware's pots move it (panel.cpp): evenly in ratio, not in ms.

export interface Taper {
  min: number;
  max: number;
  /** Evenly in ratio rather than in units. */
  log?: boolean;
  /** With log and min 0: the first value above 0 (0 is "off", at the bottom). */
  lo?: number;
}

/** The bottom few degrees of a log knob that has an "off". */
const OFF = 0.03;

const clamp = (x: number, lo: number, hi: number) => (x < lo ? lo : x > hi ? hi : x);

/** A value's place on the knob, 0 to 1. */
export function toPos(v: number, t: Taper): number {
  if (t.max <= t.min) return 0;
  if (!t.log) return clamp((v - t.min) / (t.max - t.min), 0, 1);
  const off = t.min <= 0;
  const lo = off ? Math.max(t.lo ?? 1, 1e-6) : t.min;
  if (off && v <= 0) return 0;
  const x = Math.log(clamp(v, lo, t.max) / lo) / Math.log(t.max / lo);
  return off ? OFF + (1 - OFF) * x : x;
}

/** The value at a place on the knob, rounded to a whole unit. */
export function fromPos(pos: number, t: Taper): number {
  pos = clamp(pos, 0, 1);
  if (t.max <= t.min) return t.min;
  if (!t.log) return Math.round(t.min + pos * (t.max - t.min));
  const off = t.min <= 0;
  const lo = off ? Math.max(t.lo ?? 1, 1e-6) : t.min;
  if (off && pos < OFF / 2) return 0;
  const x = off ? Math.max(0, pos - OFF) / (1 - OFF) : pos;
  return clamp(Math.round(lo * Math.pow(t.max / lo, x)), off ? 0 : t.min, t.max);
}

/**
 * One wheel click or arrow press: the next whole value whose place on the knob
 * differs, at least 1/100 of a turn for big ranges, and always at least 1.
 */
export function nudge(v: number, dir: number, t: Taper): number {
  const span = t.max - t.min;
  if (span <= 0) return t.min;
  if (span <= 100 && !t.log) return clamp(v + dir, t.min, t.max);
  const next = fromPos(toPos(v, t) + dir * 0.01, t);
  const moved = next === v ? v + dir : next;
  return clamp(moved, t.min, t.max);
}

export interface KnobEvents {
  /** The value, as the user turns it. */
  turn(v: number): void;
  /** A click that did not turn: an encoder's tap. */
  tap?(): void;
  /** Turned with Shift held: an encoder's held turn, in steps. */
  holdTurn?(steps: number): void;
}

const SWEEP = 270; // degrees, from 7:30 round to 4:30
const PX_PER_TURN = 220; // drag this far for the whole sweep
const PX_PER_STEP = 14; // and this far for one held step

function arc(r: number, from: number, to: number): string {
  const p = (a: number) => {
    const rad = ((a - 90) * Math.PI) / 180;
    return `${(24 + r * Math.cos(rad)).toFixed(2)} ${(24 + r * Math.sin(rad)).toFixed(2)}`;
  };
  const large = to - from > 180 ? 1 : 0;
  return `M ${p(from)} A ${r} ${r} 0 ${large} 1 ${p(to)}`;
}

export class Knob {
  readonly el: HTMLElement;
  private value = 0;
  private taper: Taper = { min: 0, max: 1 };
  private readonly fill: SVGPathElement;
  private readonly needle: SVGLineElement;
  private readonly on: KnobEvents;

  constructor(on: KnobEvents, label: string) {
    this.on = on;
    this.el = document.createElement('div');
    this.el.className = `knob${on.tap ? ' pressable' : ''}`;
    this.el.tabIndex = 0;
    this.el.setAttribute('role', 'slider');
    this.el.setAttribute('aria-label', label);
    const ns = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(ns, 'svg');
    svg.setAttribute('viewBox', '0 0 48 48');
    const start = -SWEEP / 2;
    const track = document.createElementNS(ns, 'path');
    track.setAttribute('class', 'knob-track');
    track.setAttribute('d', arc(20, start, start + SWEEP));
    this.fill = document.createElementNS(ns, 'path');
    this.fill.setAttribute('class', 'knob-fill');
    const cap = document.createElementNS(ns, 'circle');
    cap.setAttribute('class', 'knob-cap');
    cap.setAttribute('cx', '24');
    cap.setAttribute('cy', '24');
    cap.setAttribute('r', '14');
    this.needle = document.createElementNS(ns, 'line');
    this.needle.setAttribute('class', 'knob-needle');
    svg.append(track, this.fill, cap, this.needle);
    this.el.append(svg);
    this.wire();
  }

  set(v: number, taper: Taper) {
    this.value = v;
    this.taper = taper;
    const pos = toPos(v, taper);
    const start = -SWEEP / 2;
    const a = start + pos * SWEEP;
    this.fill.setAttribute('d', pos > 0.002 ? arc(20, start, a) : '');
    const rad = ((a - 90) * Math.PI) / 180;
    this.needle.setAttribute('x1', String(24 + 5 * Math.cos(rad)));
    this.needle.setAttribute('y1', String(24 + 5 * Math.sin(rad)));
    this.needle.setAttribute('x2', String(24 + 13 * Math.cos(rad)));
    this.needle.setAttribute('y2', String(24 + 13 * Math.sin(rad)));
    this.el.setAttribute('aria-valuemin', String(taper.min));
    this.el.setAttribute('aria-valuemax', String(taper.max));
    this.el.setAttribute('aria-valuenow', String(v));
    this.el.classList.toggle('disabled', taper.max <= taper.min);
  }

  private emit(v: number) {
    if (v === this.value) return;
    this.set(v, this.taper);
    this.on.turn(v);
  }

  private wire() {
    let drag: { y: number; pos: number; moved: boolean; held: boolean; steps: number } | null = null;
    this.el.addEventListener('pointerdown', (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      this.el.focus();
      this.el.setPointerCapture(e.pointerId);
      drag = { y: e.clientY, pos: toPos(this.value, this.taper), moved: false, held: !!this.on.holdTurn && e.shiftKey, steps: 0 };
      this.el.classList.add('active');
    });
    this.el.addEventListener('pointermove', (e) => {
      if (!drag) return;
      const dy = drag.y - e.clientY;
      if (Math.abs(dy) > 3) drag.moved = true;
      if (!drag.moved) return;
      if (drag.held) {
        const steps = Math.trunc(dy / PX_PER_STEP);
        if (steps !== drag.steps) this.on.holdTurn!(steps - drag.steps);
        drag.steps = steps;
        return;
      }
      const fine = e.altKey ? 0.2 : 1;
      this.emit(fromPos(drag.pos + (dy / PX_PER_TURN) * fine, this.taper));
    });
    const up = () => {
      if (drag && !drag.moved) this.on.tap?.();
      drag = null;
      this.el.classList.remove('active');
    };
    this.el.addEventListener('pointerup', up);
    this.el.addEventListener('pointercancel', () => {
      drag = null;
      this.el.classList.remove('active');
    });
    this.el.addEventListener(
      'wheel',
      (e) => {
        e.preventDefault();
        const dir = e.deltaY < 0 ? 1 : -1;
        if (e.shiftKey && this.on.holdTurn) return this.on.holdTurn(dir);
        this.emit(nudge(this.value, dir, this.taper));
      },
      { passive: false }
    );
    this.el.addEventListener('keydown', (e) => {
      const dir = e.key === 'ArrowUp' || e.key === 'ArrowRight' ? 1 : e.key === 'ArrowDown' || e.key === 'ArrowLeft' ? -1 : 0;
      if (dir) {
        e.preventDefault();
        e.stopPropagation(); // the console plays notes on keys
        if (e.shiftKey && this.on.holdTurn) return this.on.holdTurn(dir);
        return this.emit(nudge(this.value, dir, this.taper));
      }
      if ((e.key === 'Enter' || e.key === ' ') && this.on.tap) {
        e.preventDefault();
        e.stopPropagation();
        this.on.tap();
      }
    });
  }
}
