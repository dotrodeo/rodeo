// What Rodeo does with MIDI, and nothing more.
//
// A second copy of `instruments/rodeo-demo/firmware/src/main.cpp`'s `Midi()`
// and `synth.cpp`'s voice handling, same rule as card.ts: the firmware is
// authoritative, every claim below cites the line that makes it true. The point
// of writing it twice is that the page can then say what Rodeo *should* do with
// what it just sent, which is the difference between hearing something odd and
// knowing it is wrong.
//
// Rodeo is MIDI-in only. There is no `tud_midi_write` anywhere in the firmware,
// so nothing comes back and the page never draws an incoming panel.

/** `Midi()` masks the channel off: `status & 0xF0` (main.cpp:79). Any channel. */
export const CHANNEL_IGNORED = true;

/** `kVoices` (synth.h:19). */
export const VOICE_COUNT = 8;

/** `PitchBend(2.f * (raw - 8192) / 8192.f)` — ±2 semitones (main.cpp:83). */
export const BEND_SEMITONES = 2;

/** Kit files start here, a pitched sample plays at pitch here (samples.h:41-42). */
export const KIT_FIRST_NOTE = 36;
export const ROOT_NOTE = 60;

/**
 * Every message the firmware acts on. Anything absent from this list is read
 * off the wire and dropped by the `default:` case — no mod wheel, no CC for
 * cutoff, no program change, no aftertouch. The panel's pots are the only way
 * to shape the sound, and the TRS jack arrives through the same handler
 * (main.cpp:106-109).
 */
export const UNDERSTOOD: [what: string, status: string, note: string][] = [
  ['note on', '0x90', 'velocity 0 is a note off (synth.cpp:294)'],
  ['note off', '0x80', 'the velocity byte is ignored'],
  ['pitch bend', '0xE0', `±${BEND_SEMITONES} semitones, no way to change the range`],
  ['sustain', 'CC 64', '≥ 64 is down (main.cpp:85)'],
  ['all notes off', 'CC 120, CC 123', 'both release the voices — a long release still rings'],
];

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
 * The USB product string is "Rodeo Demo" and the MIDI interface is "Rodeo
 * MIDI" (usb_descriptors.c:100,104); which of the two a port is named depends
 * on the OS, so match the word.
 */
export function isRodeoPort(name: string | null = ''): boolean {
  // MIDIPort.name may be null, which test() reads as "null": not a Rodeo.
  return /rodeo/i.test(name as string);
}

// ------------------------------------------------------------ voice model
//
// `Allocate()` (synth.cpp:136) in order: the same note again, then any voice
// whose envelope has finished, then the oldest released voice, then the oldest
// voice of all. Two consequences that surprise people, both reproduced here:
//
//   - A voice keeps `note` after it stops sounding, so pressing a key you
//     played before takes that same voice back rather than a free one.
//   - In sampler-only layer a key with no sample allocates nothing at all
//     (`Start` returns early, synth.cpp:161), so it cannot steal.

/** One of the firmware's voices, as far as `Allocate()` can see it. */
export interface Voice {
  /** Kept after the voice goes quiet, which is why a key gets its old voice back. */
  note: number;
  /** The key is still down. */
  key: boolean;
  /** The amp envelope's gate: down for a held key, or one the pedal holds. */
  gate: boolean;
  /** Allocation order; 0 for a voice that has never played. */
  started: number;
  releasedAt: number;
}

export type Allocation = {
  index: number;
  how: 'retrigger' | 'free' | 'stole a released voice' | 'stole a sounding voice';
};

export interface NoteOffResult {
  off: number[];
  note: number;
  held: boolean;
}

/**
 * What `noteOn` did: allocated a voice (`index`, `how`, and the note it `took`
 * if that voice was still sounding), allocated nothing (`silent`), or — at
 * velocity 0 — a note off. One flat shape rather than a union, because the
 * callers read whichever fields the case they set up will have filled.
 */
export interface NoteOnResult extends Partial<Allocation>, Partial<Omit<NoteOffResult, 'note'>> {
  note: number;
  took?: number | null;
  silent?: true;
}

export class Voices {
  count: number;
  release: number;
  sustainDown: boolean;
  order: number;
  slots: Voice[];

  /**
   * @param release ms a voice keeps sounding after key-up. The firmware asks
   *   the amp ADSR (`Voice::Active`), so this is the preset's `release` — the
   *   one number here that is a model rather than a copy. Pass the preset's.
   */
  constructor({ count = VOICE_COUNT, release = 300 }: { count?: number; release?: number } = {}) {
    this.count = count;
    this.release = release;
    this.sustainDown = false;
    this.order = 0;
    this.slots = Array.from({ length: count }, () => ({
      note: -1,
      key: false,
      gate: false,
      started: 0,
      releasedAt: 0,
    }));
  }

  /** `Voice::Active()`: the amp envelope is not idle. */
  active(v: Voice, now: number): boolean {
    if (v.started === 0) return false;
    if (v.gate) return true;
    return now - v.releasedAt < this.release;
  }

  /** Which slot `Allocate()` would pick, and why. */
  allocate(note: number, now: number): Allocation {
    const same = this.slots.findIndex((v) => v.note === note);
    if (same >= 0) return { index: same, how: 'retrigger' };
    const free = this.slots.findIndex((v) => !this.active(v, now));
    if (free >= 0) return { index: free, how: 'free' };
    let pick = -1;
    for (let i = 0; i < this.slots.length; i++) {
      const v = this.slots[i];
      if (v.gate) continue;
      if (pick < 0 || v.started < this.slots[pick].started) pick = i;
    }
    if (pick >= 0) return { index: pick, how: 'stole a released voice' };
    pick = 0;
    for (let i = 1; i < this.slots.length; i++)
      if (this.slots[i].started < this.slots[pick].started) pick = i;
    return { index: pick, how: 'stole a sounding voice' };
  }

  /**
   * @param hasSample null when the layer does not matter, false when this is
   *   sampler-only and the key has nothing on it.
   */
  noteOn(note: number, velocity: number, now: number, hasSample: boolean | null = null): NoteOnResult {
    if (velocity === 0) return this.noteOff(note, now); // synth.cpp:294
    if (hasSample === false) return { silent: true, note };
    const { index, how } = this.allocate(note, now);
    const v = this.slots[index];
    const took = v.note !== note && this.active(v, now) ? v.note : null;
    Object.assign(v, { note, key: true, gate: true, started: ++this.order, releasedAt: 0 });
    return { index, how, took, note };
  }

  noteOff(note: number, now: number): NoteOffResult {
    const touched: number[] = [];
    for (let i = 0; i < this.slots.length; i++) {
      const v = this.slots[i];
      if (v.note !== note || !v.key) continue; // only a voice still holding the key
      v.key = false;
      if (!this.sustainDown) {
        v.gate = false;
        v.releasedAt = now;
      }
      touched.push(i);
    }
    return { off: touched, note, held: this.sustainDown };
  }

  setSustain(down: boolean, now: number): { sustain: boolean; released?: number[] } {
    this.sustainDown = down;
    if (down) return { sustain: true };
    const released: number[] = [];
    for (let i = 0; i < this.slots.length; i++) {
      const v = this.slots[i];
      if (this.active(v, now) && !v.key) {
        v.gate = false;
        v.releasedAt = now;
        released.push(i);
      }
    }
    return { sustain: false, released };
  }

  allOff(now: number): { allOff: true } {
    for (const v of this.slots) {
      v.key = false;
      v.gate = false;
      v.releasedAt = now;
    }
    return { allOff: true };
  }

  sounding(now: number): number {
    return this.slots.filter((v) => this.active(v, now)).length;
  }
}
