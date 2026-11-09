// Web Serial, which is how the console reaches the Rodeo's CDC port.
//
// Chrome and Edge only, and only over a secure context. The port is the same
// one a terminal would open: the firmware's log, and the commands a profile adds
// (rodeo-demo's `s`, `d`, `n`).
//
// Disk mode closes this port — the Rodeo re-enumerates without CDC — so losing
// the port is a normal thing that happens, not an error to shout about.

import { takeLines } from './chain.ts';

export function supported(): boolean {
  return typeof navigator !== 'undefined' && 'serial' in navigator;
}

export interface PortEvents {
  /** Every whole line. */
  onLine?: (line: string) => void;
  /** The port closed: `why` is null for a close we asked for. */
  onClose?: (why: string | null | undefined) => void;
}

export class Port {
  onLine: (line: string) => void;
  onClose: (why: string | null | undefined) => void;
  port: SerialPort | null;
  reader: ReadableStreamDefaultReader<Uint8Array> | null;
  writer: WritableStreamDefaultWriter<Uint8Array> | null;
  buffer: string;

  constructor({ onLine = () => {}, onClose = () => {} }: PortEvents = {}) {
    this.onLine = onLine;
    this.onClose = onClose;
    this.port = null;
    this.reader = null;
    this.writer = null;
    this.buffer = '';
  }

  get open(): boolean {
    return !!this.port;
  }

  /** Shows the browser's port picker; throws AbortError if it is dismissed. */
  async connect(): Promise<void> {
    this.port = await navigator.serial.requestPort();
    // A CDC port's baud rate is a fiction — the device ignores it — but the
    // API demands one.
    await this.port.open({ baudRate: 115200 });
    this.read();
  }

  async read(): Promise<void> {
    const decoder = new TextDecoder();
    try {
      // connect() opened the port just before calling this, so both are set.
      this.reader = (this.port as SerialPort).readable!.getReader();
      for (;;) {
        const { value, done } = await this.reader.read();
        if (done) break;
        this.buffer += decoder.decode(value, { stream: true });
        const { lines, rest } = takeLines(this.buffer);
        this.buffer = rest;
        for (const line of lines) this.onLine(line);
        // A line this long is a line that will never end: something is
        // sending bytes without newlines, and keeping them is a leak.
        if (this.buffer.length > 16384) this.buffer = '';
      }
    } catch (e) {
      // Unplugging mid-read, or disk mode taking the port away.
      this.fail(e);
    } finally {
      this.release();
    }
  }

  async write(text: string): Promise<void> {
    if (!this.port?.writable) return;
    try {
      this.writer ??= this.port.writable.getWriter();
      await this.writer.write(new TextEncoder().encode(text));
    } catch (e) {
      this.fail(e);
    }
  }

  fail(e: unknown): void {
    if (!this.port) return; // already closing on purpose
    // Whatever was thrown; a DOMException in practice, so read it as an Error.
    const err = e as Error | undefined;
    const why = err?.name === 'NetworkError' ? 'the port went away — disk mode, or unplugged' : err?.message;
    this.port = null;
    this.onClose(why);
  }

  release(): void {
    try {
      this.reader?.releaseLock();
    } catch {
      /* already gone */
    }
    this.reader = null;
  }

  async close(): Promise<void> {
    const port = this.port;
    this.port = null; // so fail() stays quiet about a close we asked for
    try {
      await this.reader?.cancel();
    } catch {
      /* already gone */
    }
    this.release();
    try {
      this.writer?.releaseLock();
    } catch {
      /* already gone */
    }
    this.writer = null;
    try {
      await port?.close();
    } catch {
      /* already gone */
    }
    this.onClose(null);
  }
}
