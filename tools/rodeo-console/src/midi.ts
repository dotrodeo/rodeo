// MIDI out: the messages the console's keyboard, arp and piano roll send.
//
// Channel voice messages, any firmware's business to act on or not. The pitch
// bend range is the SDK examples' and rodeo-demo's ±2 semitones; a firmware
// with another range hears the same wheel, scaled its own way.

/** Semitones at full bend, as rodeo-demo takes it (main.cpp:83). */
export const BEND_SEMITONES = 2;

const PITCHES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

/** 60 is C4. */
export function noteName(note: number): string {
  return `${PITCHES[note % 12]}${Math.floor(note / 12) - 1}`;
}

// ------------------------------------------------------------------ messages

const clamp = (x: number, lo: number, hi: number) => (x < lo ? lo : x > hi ? hi : x);

/** One MIDI message, as bytes. */
export type Message = [number, number, number];

export function noteOn(note: number, velocity = 100, channel = 0): Message {
  return [0x90 | (channel & 0x0f), clamp(note, 0, 127), clamp(velocity, 0, 127)];
}

export function noteOff(note: number, channel = 0): Message {
  return [0x80 | (channel & 0x0f), clamp(note, 0, 127), 0];
}

/**
 * Semitones to the 14-bit value, the inverse of main.cpp:83. Note the top of
 * the range: raw 16383 is +1.9998 semitones, so exactly +2 cannot be sent.
 * That is the MIDI wheel's own asymmetry, not a firmware bug.
 */
export function pitchBend(semitones: number, channel = 0): Message {
  const raw = clamp(Math.round(8192 + (semitones * 8192) / BEND_SEMITONES), 0, 16383);
  return [0xe0 | (channel & 0x0f), raw & 0x7f, (raw >> 7) & 0x7f];
}

export function sustain(down: boolean, channel = 0): Message {
  return [0xb0 | (channel & 0x0f), 64, down ? 127 : 0];
}

export function allNotesOff(channel = 0): Message {
  return [0xb0 | (channel & 0x0f), 123, 0];
}

/**
 * A Rodeo's port has the word in it: rodeo-demo's are "Rodeo Demo" and
 * "Rodeo MIDI", the SDK host example's "Rodeo Host Example". Which string a
 * port is named by depends on the OS, so match the word.
 */
export function isRodeoPort(name: string | null = ''): boolean {
  // MIDIPort.name may be null, which test() reads as "null": not a Rodeo.
  return /rodeo/i.test(name as string);
}
