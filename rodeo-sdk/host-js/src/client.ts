// A conversation with one Rodeo, whatever its firmware: hello, the state (and
// the chain) streamed with a keepalive, parameter sets, notes, the firmware's own
// commands. It knows nothing of Web MIDI, CoreMIDI or any UI: it is handed a way
// to send and is fed bytes, so it can be tested with a fake. A firmware's own
// commands go through request(), and their replies land in snapshot.replies;
// rodeo-demo.ts is rodeo-demo's.

import { MidiParser } from './parser.ts';
import {
  type ChainReport,
  Cmd,
  type Hello,
  type Param,
  REPLY,
  type State,
  Stream,
  allNotesOff,
  message,
  noteOff,
  noteOn,
  parseReply,
  pitchBend,
  setMessage,
  sustain,
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
  chain: ChainReport | null; // while the chain is streamed (streamChain)
  said: { text: string; at: number } | null; // the last thing Rodeo said changed
  /** The latest reply to each of the firmware's own commands, by reply number. */
  replies: Record<number, unknown>;
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
    chain: null,
    said: null,
    replies: {},
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
  private ready = new Set<(hello: Hello) => void>();
  private streamBits: number = Stream.state;
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

  /** One of the firmware's own commands (0x20..0x3E). Its reply, if any, lands in
   *  snapshot.replies under the command plus 0x40. */
  request(cmd: number, payload: number[] = []) {
    if (cmd < Cmd.firmwareFirst || cmd > Cmd.firmwareLast) throw new RangeError(`0x${cmd.toString(16)} is not a firmware's command`);
    this.send(message(cmd, payload));
  }

  /** Called with the hello each time a Rodeo is ready: where a firmware's own
   *  lists get asked for. Returns a function that stops it. */
  whenReady(f: (hello: Hello) => void): () => void {
    this.ready.add(f);
    if (this.snap.hello && this.snap.status === 'ready') f(this.snap.hello);
    return () => this.ready.delete(f);
  }

  /** Stream the chain too (true), or only the state. */
  streamChain(on: boolean) {
    const bits = on ? Stream.state | Stream.chain : Stream.state;
    if (bits === this.streamBits) return;
    this.streamBits = bits;
    if (!on) this.update({ chain: null });
    if (this.keepalive !== null) this.startStream();
  }

  /** Reboot it into the bootloader for flashing. It answers, then leaves USB. */
  dfu() {
    this.send(message(Cmd.dfu));
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
      this.update({ status: 'no-device', state: null, chain: null, replies: {} });
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
    this.ready.clear();
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
    const body = reply.body as never;
    switch (reply.cmd) {
      case Cmd.helloReply: {
        if (this.helloTimer !== null) this.timers.clearInterval(this.helloTimer);
        this.helloTimer = null;
        const hello = body as Hello;
        const params: Record<string, Param> = {};
        for (const p of hello.params) params[p.key] = p;
        this.update({ status: 'ready', hello, params, replies: {} });
        for (const f of this.ready) f(hello);
        if (this.active) this.startStream();
        else this.send(message(Cmd.state));
        break;
      }
      case Cmd.stateReply:
        this.update({ state: body as State });
        break;
      case Cmd.chainReply:
        this.update({ chain: body as ChainReport });
        break;
      case Cmd.said:
        this.update({ said: { text: (body as { said: string }).said, at: Date.now() } });
        break;
      case Cmd.error:
        this.update({ said: { text: `Rodeo: ${(body as { error: string }).error}`, at: Date.now() } });
        break;
      default:
        if (reply.cmd >= Cmd.firmwareFirst + REPLY && reply.cmd <= Cmd.firmwareLast + REPLY)
          this.update({ replies: { ...this.snap.replies, [reply.cmd]: reply.body } });
        break;
    }
  }

  private startStream() {
    this.stopKeepalive();
    const ask = () => this.send(message(Cmd.stream, [this.streamBits]));
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
