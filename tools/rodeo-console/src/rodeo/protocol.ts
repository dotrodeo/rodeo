// The host protocol, the computer's half: rodeo-sdk/docs/guides/host.mdx, which
// any firmware on the SDK speaks, and rodeo-demo's own commands on top
// (planning/RODEO_DEMO_MIDI_PROTOCOL.md). Messages are F0 7D 52 44 02 <cmd>
// <payload> F7. Rodeo answers in JSON.

export const HEADER = [0xf0, 0x7d, 0x52, 0x44] as const;
export const VERSION = 2;

export const Cmd = {
  hello: 0x01,
  state: 0x02,
  stream: 0x03, // payload: 1 state, 2 chain, 3 both, 0 stop
  set: 0x04,
  chain: 0x08,
  dfu: 0x09,
  // rodeo-demo's own (0x20..0x3E are a firmware's)
  presets: 0x20,
  load: 0x21,
  sounds: 0x22,
  // from Rodeo
  helloReply: 0x41,
  stateReply: 0x42,
  said: 0x43,
  chainReply: 0x48,
  dfuReply: 0x49,
  presetsReply: 0x60,
  soundsReply: 0x62,
  error: 0x7f,
} as const;

export type Param = {
  id: number;
  key: string;
  min: number;
  max: number;
  def: number; // what the firmware starts with, and a preset leaving it out goes back to
  unit?: string;
  pot?: number; // the pot it's on, from 1
  names?: string[]; // a choice
};

export type Hello = {
  fw: string; // "rodeo-demo"
  proto: number;
  build: string;
  sdk: string;
  chain: number; // 1 if it has a satellite chain to report
  params: Param[];
  voices?: number; // rodeo-demo's
};

export type State = {
  p: Record<string, number>;
  hold: number; // bit per pot (bit 0 pot 1) whose parameter a preset or the app holds
  cpu: number;
  // rodeo-demo's
  preset?: number;
  name?: string;
  voices?: number;
  card?: number; // 0 none, 1 mounted, 2 disk mode
  sounds?: number;
  loaded?: number;
  samples?: number;
  panel?: number; // 1 when pot bank 1 and encoder bank 1 are on the chain
};

export type ChainReport = {
  state: string; // "scan", "partial", ...
  sats: number;
  broken: number; // satellites before a break, or -1
  s: { kind: number; fw: string; in: number[]; btn?: number[]; fault?: ([number, number] | null)[] }[];
  good: number;
  bad: number;
};

export type Sound = { name: string; kit: number; ready: number };

export type Reply =
  | { cmd: typeof Cmd.helloReply; body: Hello }
  | { cmd: typeof Cmd.stateReply; body: State }
  | { cmd: typeof Cmd.said; body: { said: string } }
  | { cmd: typeof Cmd.chainReply; body: ChainReport }
  | { cmd: typeof Cmd.dfuReply; body: { dfu: number } }
  | { cmd: typeof Cmd.presetsReply; body: { presets: string[]; current: number } }
  | { cmd: typeof Cmd.soundsReply; body: { sounds: Sound[] } }
  | { cmd: typeof Cmd.error; body: { error: string } };

export function u14(v: number): [number, number] {
  return [v & 0x7f, (v >> 7) & 0x7f];
}

export function message(cmd: number, payload: number[] = []): number[] {
  return [...HEADER, VERSION, cmd, ...payload.map((b) => b & 0x7f), 0xf7];
}

/** A set of parameters, each value in the parameter's own units. */
export function setMessage(values: [Param, number][]): number[] {
  const payload: number[] = [];
  for (const [p, v] of values) {
    const clamped = Math.round(Math.min(p.max, Math.max(p.min, v)));
    payload.push(p.id, ...u14(clamped - p.min));
  }
  return message(Cmd.set, payload);
}

/** One of Rodeo's replies, or null for anything else (another device's SysEx, a note). */
export function parseReply(m: number[]): Reply | null {
  if (m.length < 7 || m[0] !== 0xf0 || m[m.length - 1] !== 0xf7) return null;
  for (let i = 1; i < HEADER.length; i++) if (m[i] !== HEADER[i]) return null;
  if (m[4] !== VERSION) return null;
  let text = '';
  for (let i = 6; i < m.length - 1; i++) text += String.fromCharCode(m[i]);
  try {
    return { cmd: m[5], body: JSON.parse(text) } as Reply;
  } catch {
    return null;
  }
}

// Channel messages. Rodeo ignores the channel; we send on 1.
export const noteOn = (note: number, velocity: number) => [0x90, note & 0x7f, velocity & 0x7f];
export const noteOff = (note: number) => [0x80, note & 0x7f, 0];
export const cc = (n: number, v: number) => [0xb0, n & 0x7f, v & 0x7f];
export const allNotesOff = () => cc(123, 0);
export const sustain = (down: boolean) => cc(64, down ? 127 : 0);
/** semitones: -2..2, Rodeo's bend range */
export function pitchBend(semitones: number): number[] {
  const v = Math.round(8192 + (Math.max(-2, Math.min(2, semitones)) / 2) * 8191);
  return [0xe0, v & 0x7f, (v >> 7) & 0x7f];
}
