// MIDI bytes to whole messages. The iOS module hands over bytes packet by packet
// (a SysEx message can span several); Web MIDI hands over whole messages. Both go
// through this, so the rest of the app only ever sees whole messages.

const MAX_SYSEX = 64 * 1024;

/** How many bytes a message with this status byte has, status included. */
export function messageLength(status: number): number {
  if (status < 0xf0) return (status & 0xf0) === 0xc0 || (status & 0xf0) === 0xd0 ? 2 : 3;
  if (status === 0xf1 || status === 0xf3) return 2;
  if (status === 0xf2) return 3;
  return 1;
}

export class MidiParser {
  private message: number[] = [];
  private sysex = false;
  private running = 0; // running status: the last channel status byte
  private readonly onMessage: (message: number[]) => void;

  constructor(onMessage: (message: number[]) => void) {
    this.onMessage = onMessage;
  }

  push(bytes: ArrayLike<number>) {
    for (let i = 0; i < bytes.length; i++) this.byte(bytes[i] & 0xff);
  }

  private byte(b: number) {
    if (b >= 0xf8) {
      this.onMessage([b]); // real time: may come in the middle of anything
      return;
    }
    if (b === 0xf0) {
      this.sysex = true;
      this.message = [b];
      return;
    }
    if (this.sysex) {
      if (b === 0xf7) {
        this.message.push(b);
        this.onMessage(this.message);
        this.sysex = false;
        this.message = [];
        return;
      }
      if (b < 0x80) {
        if (this.message.length < MAX_SYSEX) this.message.push(b);
        return;
      }
      this.sysex = false; // a status byte ends a SysEx left unfinished: drop it
      this.message = [];
    }
    if (b >= 0x80) {
      if (b === 0xf7) return; // stray end of SysEx
      this.running = b < 0xf0 ? b : 0;
      this.message = [b];
    } else {
      if (this.message.length === 0) {
        if (!this.running) return; // a data byte with nothing to belong to
        this.message = [this.running];
      }
      this.message.push(b);
    }
    if (this.message.length === messageLength(this.message[0])) {
      this.onMessage(this.message);
      this.message = [];
    }
  }
}
