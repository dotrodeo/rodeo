// The phone app's MIDI client (instruments/rodeo-demo/app/src/midi on the
// rodeo-mobile-app branch), copied here until the app is merged and they share it.
// A conversation with one Rodeo: hello, the lists, the state stream and its
// keepalive, parameter sets, notes. It knows nothing of CoreMIDI or React; it is
// handed a way to send and is fed whole messages (useRodeo.ts does both), so it
// can be tested with a fake.

import { MidiParser } from './parser.ts';
import {
  Cmd,
  type Hello,
  type Param,
  type Sound,
  type State,
  allNotesOff,
  message,
  noteOff,
  noteOn,
  parseReply,
  pitchBend,
  setMessage,
  sustain,
  u14,
} from './protocol.ts';

export type Status =
  | 'no-device' // nothing called Rodeo is plugged in
  | 'connecting' // saying hello
  | 'not-rodeo' // something answered to the name but not to hello
  | 'ready';

export type Snapshot = {
  status: Status;
  hello: Hello | null;
  params: Record<string, Param>;
  state: State | null;
  presets: string[];
  sounds: Sound[];
  said: { text: string; at: number } | null; // the last thing Rodeo said changed
};

export type Transport = {
  send(bytes: number[], at?: number): boolean;
  now(): number;
};

export type Timers = {
  setInterval(f: () => void, ms: number): unknown;
  clearInterval(t: unknown): void;
  setTimeout(f: () => void, ms: number): unknown;
  clearTimeout(t: unknown): void;
};

const HELLO_EVERY_MS = 500;
const HELLO_TRIES = 6;
const KEEPALIVE_MS = 2000; // Rodeo stops streaming 5 s after the last
const SET_EVERY_MS = 20; // per parameter, at most

const realTimers: Timers = {
  setInterval: (f, ms) => setInterval(f, ms),
  clearInterval: (t) => clearInterval(t as ReturnType<typeof setInterval>),
  setTimeout: (f, ms) => setTimeout(f, ms),
  clearTimeout: (t) => clearTimeout(t as ReturnType<typeof setTimeout>),
};

export class RodeoClient {
  private snap: Snapshot = {
    status: 'no-device',
    hello: null,
    params: {},
    state: null,
    presets: [],
    sounds: [],
    said: null,
  };
  private listeners = new Set<(s: Snapshot) => void>();
  private parser = new MidiParser((m) => this.message(m));
  private transport: Transport | null = null;
  private helloTimer: unknown = null;
  private keepalive: unknown = null;
  private active = true; // the app is in the foreground
  private pending = new Map<number, number>(); // param id -> value not sent yet
  private lastSet = new Map<number, number>(); // param id -> when it was last sent
  private flushTimer: unknown = null;
  private held = new Set<number>(); // notes we've sent on and not off
  private readonly timers: Timers;

  constructor(timers: Timers = realTimers) {
    this.timers = timers;
  }

  // ---- for the UI

  get snapshot(): Snapshot {
    return this.snap;
  }

  subscribe(f: (s: Snapshot) => void): () => void {
    this.listeners.add(f);
    return () => this.listeners.delete(f);
  }

  /** A parameter, by its key, in its units. Throttled: drags send at most every 20 ms. */
  set(key: string, value: number) {
    const p = this.snap.params[key];
    if (!p || this.snap.status !== 'ready') return;
    this.pending.set(p.id, value);
    this.flush();
  }

  loadPreset(i: number) {
    this.send(message(Cmd.load, u14(i)));
  }

  noteOn(note: number, velocity: number) {
    this.held.add(note);
    this.send(noteOn(note, velocity));
  }

  noteOff(note: number) {
    this.held.delete(note);
    this.send(noteOff(note));
  }

  bend(semitones: number) {
    this.send(pitchBend(semitones));
  }

  sustain(down: boolean) {
    this.send(sustain(down));
  }

  // ---- for useRodeo

  /** A device that may be a Rodeo was opened: say hello. null: it's gone. */
  attach(transport: Transport | null) {
    this.stopTimers();
    this.transport = transport;
    this.held.clear();
    if (!transport) {
      this.update({ status: 'no-device', state: null });
      return;
    }
    this.update({ status: 'connecting' });
    let tries = 0;
    const hello = () => {
      if (tries++ >= HELLO_TRIES) {
        this.timers.clearInterval(this.helloTimer);
        this.helloTimer = null;
        this.update({ status: 'not-rodeo' });
        return;
      }
      this.send(message(Cmd.hello));
    };
    hello();
    this.helloTimer = this.timers.setInterval(hello, HELLO_EVERY_MS);
  }

  /** Bytes from the device, whole messages or not. */
  receive(bytes: ArrayLike<number>) {
    this.parser.push(bytes);
  }

  /** The app came to the foreground (true) or left it (false). */
  setActive(active: boolean) {
    if (active === this.active) return;
    this.active = active;
    if (!active) {
      // Nothing can let go of a key while we're away, and the sequencer stops:
      // silence what's sounding, and stop the stream rather than let it run out
      this.send(allNotesOff());
      this.held.clear();
      this.send(message(Cmd.stream, [0]));
      this.stopKeepalive();
    } else if (this.snap.status === 'ready') {
      this.startStream();
    }
  }

  dispose() {
    this.stopTimers();
    this.listeners.clear();
  }

  // ---- inside

  private send(bytes: number[], at = 0): boolean {
    return this.transport?.send(bytes, at) ?? false;
  }

  private update(change: Partial<Snapshot>) {
    this.snap = { ...this.snap, ...change };
    for (const f of this.listeners) f(this.snap);
  }

  private message(m: number[]) {
    const reply = parseReply(m);
    if (!reply) return;
    switch (reply.cmd) {
      case Cmd.helloReply: {
        if (this.helloTimer !== null) this.timers.clearInterval(this.helloTimer);
        this.helloTimer = null;
        const params: Record<string, Param> = {};
        for (const p of reply.body.params) params[p.key] = p;
        this.update({ status: 'ready', hello: reply.body, params });
        this.send(message(Cmd.presets));
        this.send(message(Cmd.sounds));
        if (this.active) this.startStream();
        else this.send(message(Cmd.state));
        break;
      }
      case Cmd.stateReply:
        this.update({ state: reply.body });
        break;
      case Cmd.said:
        this.update({ said: { text: reply.body.said, at: Date.now() } });
        break;
      case Cmd.presetsReply:
        this.update({ presets: reply.body.presets });
        break;
      case Cmd.soundsReply:
        this.update({ sounds: reply.body.sounds });
        break;
      case Cmd.error:
        this.update({ said: { text: `Rodeo: ${reply.body.error}`, at: Date.now() } });
        break;
    }
  }

  private startStream() {
    this.stopKeepalive();
    const ask = () => this.send(message(Cmd.stream, [1]));
    ask();
    this.keepalive = this.timers.setInterval(ask, KEEPALIVE_MS);
  }

  private stopKeepalive() {
    if (this.keepalive !== null) this.timers.clearInterval(this.keepalive);
    this.keepalive = null;
  }

  private stopTimers() {
    if (this.helloTimer !== null) this.timers.clearInterval(this.helloTimer);
    this.helloTimer = null;
    if (this.flushTimer !== null) this.timers.clearTimeout(this.flushTimer);
    this.flushTimer = null;
    this.stopKeepalive();
  }

  // Sends every pending parameter that hasn't been sent in the last 20 ms, in one
  // message, and comes back for the rest
  private flush() {
    if (this.flushTimer !== null) return;
    const now = this.transport?.now() ?? 0;
    const due: [Param, number][] = [];
    let wait = Infinity;
    const byId = new Map(Object.values(this.snap.params).map((p) => [p.id, p]));
    for (const [id, value] of this.pending) {
      const since = now - (this.lastSet.get(id) ?? -Infinity);
      const p = byId.get(id);
      if (!p) {
        this.pending.delete(id);
      } else if (since >= SET_EVERY_MS) {
        due.push([p, value]);
        this.pending.delete(id);
        this.lastSet.set(id, now);
      } else {
        wait = Math.min(wait, SET_EVERY_MS - since);
      }
    }
    if (due.length) this.send(setMessage(due));
    if (this.pending.size) {
      this.flushTimer = this.timers.setTimeout(() => {
        this.flushTimer = null;
        this.flush();
      }, wait);
    }
  }
}
