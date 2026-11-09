// The host protocol, the computer's half: rodeo-sdk/docs/guides/host.mdx (the
// firmware's half is rodeo-sdk/common/host.h). Any firmware on the SDK speaks
// it. Messages are F0 7D 52 44 02 <cmd> <payload> F7; Rodeo answers in JSON.

export const HEADER = [0xf0, 0x7d, 0x52, 0x44] as const;
export const VERSION = 2;
/** A reply's command is the request's plus this. */
export const REPLY = 0x40;

export const Cmd = {
  hello: 0x01,
  state: 0x02,
  stream: 0x03, // payload: Stream bits; 0 stops
  set: 0x04,
  chain: 0x08,
  dfu: 0x09,
  // 0x20..0x3E are a firmware's own; their replies 0x60..0x7E
  firmwareFirst: 0x20,
  firmwareLast: 0x3e,
  // from Rodeo
  helloReply: 0x41,
  stateReply: 0x42,
  said: 0x43,
  chainReply: 0x48,
  dfuReply: 0x49,
  error: 0x7f,
} as const;

export const Stream = { state: 1, chain: 2 } as const;

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

/** What a firmware says it is. A firmware adds fields of its own. */
export type Hello = {
  fw: string;
  proto: number;
  build: string;
  sdk: string;
  chain: number; // 1 if it has a satellite chain to report
  params: Param[];
  [own: string]: unknown;
};

/** Every parameter's value. A firmware adds fields of its own. */
export type State = {
  p: Record<string, number>;
  hold: number; // bit per pot (bit 0 pot 1) whose parameter a preset or the computer holds
  cpu: number;
  [own: string]: unknown;
};

export type ChainReport = {
  state: string; // "scan", "partial", "discover", ...
  sats: number;
  broken: number; // satellites before a break, or -1
  s: {
    kind: number;
    fw: string;
    in: number[]; // pot per mille, encoder detents, button 0/1, switch position
    btn?: number[]; // an encoder's push switch
    fault?: ([number, number] | null)[];
  }[];
  good: number;
  bad: number;
};

export type Reply =
  | { cmd: typeof Cmd.helloReply; body: Hello }
  | { cmd: typeof Cmd.stateReply; body: State }
  | { cmd: typeof Cmd.said; body: { said: string } }
  | { cmd: typeof Cmd.chainReply; body: ChainReport }
  | { cmd: typeof Cmd.dfuReply; body: { dfu: number } }
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

/** One of Rodeo's replies, the protocol's or a firmware's own, or null for
 *  anything else (another device's SysEx, another version, a note). */
export function parseReply(m: number[]): { cmd: number; body: unknown } | null {
  if (m.length < 7 || m[0] !== 0xf0 || m[m.length - 1] !== 0xf7) return null;
  for (let i = 1; i < HEADER.length; i++) if (m[i] !== HEADER[i]) return null;
  if (m[4] !== VERSION) return null;
  let text = '';
  for (let i = 6; i < m.length - 1; i++) text += String.fromCharCode(m[i]!);
  try {
    return { cmd: m[5]!, body: JSON.parse(text) };
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
/** semitones: -2..2, the bend range rodeo-demo uses */
export function pitchBend(semitones: number): number[] {
  const v = Math.round(8192 + (Math.max(-2, Math.min(2, semitones)) / 2) * 8191);
  return [0xe0, v & 0x7f, (v >> 7) & 0x7f];
}
