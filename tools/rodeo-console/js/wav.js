// Reading WAV headers the way the firmware does, and writing the one format
// it always accepts.
//
// `readWavHeader` is deliberately a header walk rather than a decode: listing
// a card of 300 files should not decode 300 files, and what we need — frames,
// channels, rate, whether the Rodeo will take it — is all in the first few
// chunks. It mirrors the chunk loop in samples.cpp:193.
//
// `encodeWav` always writes PCM format 1, 16-bit. That is not laziness: the
// firmware rejects WAVE_FORMAT_EXTENSIBLE (0xFFFE) outright, and that is what
// a lot of tools emit for 24-bit and multichannel files. Writing the plainest
// possible WAV is the whole point of passing audio through this console.

import { acceptsWavFormat } from './card.js';

const str = (view, off) => String.fromCharCode(...new Uint8Array(view.buffer, view.byteOffset + off, 4));

/**
 * Walk a WAV's chunks. Returns what the firmware would conclude about it.
 * Pure — give it an ArrayBuffer, it touches nothing else.
 */
export function readWavHeader(buffer) {
  const bad = (why) => ({ accepted: false, why });
  if (buffer.byteLength < 12) return bad('too short to be a WAV');
  const view = new DataView(buffer);
  if (str(view, 0) !== 'RIFF' || str(view, 8) !== 'WAVE') return bad('not a RIFF/WAVE file');

  let fmt = null;
  let dataBytes = null;
  let off = 12;
  while (off + 8 <= buffer.byteLength) {
    const id = str(view, off);
    const size = view.getUint32(off + 4, true);
    const body = off + 8;
    if (id === 'fmt ' && size >= 16) {
      fmt = {
        format: view.getUint16(body, true),
        channels: view.getUint16(body + 2, true),
        rate: view.getUint32(body + 4, true),
        blockAlign: view.getUint16(body + 12, true),
        bits: view.getUint16(body + 14, true),
      };
    } else if (id === 'data') {
      // A size past the end of the file happens with truncated downloads; the
      // firmware reads what is there and stops, so report what is there.
      dataBytes = Math.min(size, buffer.byteLength - body);
      break;
    }
    off = body + size + (size & 1); // chunks are word-aligned
  }

  if (!fmt) return bad('no fmt chunk');
  if (dataBytes === null) return bad('no data chunk');

  const info = {
    format: fmt.format,
    bits: fmt.bits,
    channels: fmt.channels,
    rate: fmt.rate,
    blockAlign: fmt.blockAlign,
    bytes: buffer.byteLength,
  };

  if (!acceptsWavFormat(fmt)) {
    const named =
      fmt.format === 0xfffe
        ? 'WAVE_FORMAT_EXTENSIBLE, which the Rodeo rejects — re-save it through the console'
        : `format ${fmt.format} at ${fmt.bits}-bit, which the Rodeo cannot read`;
    return { ...info, accepted: false, frames: 0, why: named };
  }

  const frames = fmt.blockAlign ? Math.floor(dataBytes / fmt.blockAlign) : 0;
  return { ...info, accepted: true, frames };
}

/**
 * Canonical 16-bit PCM WAV. `channels` is an array of Float32Array, all the
 * same length; samples outside [-1, 1] are clipped, as the Rodeo's own
 * conversion would.
 */
export function encodeWav(channels, rate) {
  const n = channels[0]?.length ?? 0;
  const ch = channels.length;
  const dataBytes = n * ch * 2;
  const buffer = new ArrayBuffer(44 + dataBytes);
  const view = new DataView(buffer);
  const ascii = (off, s) => {
    for (let i = 0; i < s.length; i++) view.setUint8(off + i, s.charCodeAt(i));
  };

  ascii(0, 'RIFF');
  view.setUint32(4, 36 + dataBytes, true);
  ascii(8, 'WAVE');
  ascii(12, 'fmt ');
  view.setUint32(16, 16, true); // a 16-byte fmt chunk: plain PCM, no extension
  view.setUint16(20, 1, true); // format 1
  view.setUint16(22, ch, true);
  view.setUint32(24, rate, true);
  view.setUint32(28, rate * ch * 2, true); // byte rate
  view.setUint16(32, ch * 2, true); // block align
  view.setUint16(34, 16, true); // bits
  ascii(36, 'data');
  view.setUint32(40, dataBytes, true);

  let off = 44;
  for (let i = 0; i < n; i++) {
    for (let c = 0; c < ch; c++) {
      const x = Math.max(-1, Math.min(1, channels[c][i]));
      // Asymmetric on purpose: -1 maps to -32768 and +1 to 32767, so full
      // scale either way is exact and nothing wraps.
      view.setInt16(off, x < 0 ? x * 0x8000 : x * 0x7fff, true);
      off += 2;
    }
  }
  return buffer;
}

/** Peak of every channel, for the normalise option. */
export function peak(channels) {
  let p = 0;
  for (const c of channels) for (let i = 0; i < c.length; i++) p = Math.max(p, Math.abs(c[i]));
  return p;
}

/** Mix to mono by averaging; the Rodeo would keep only the first two anyway. */
export function toMono(channels) {
  if (channels.length === 1) return channels;
  const n = channels[0].length;
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    let s = 0;
    for (const c of channels) s += c[i];
    out[i] = s / channels.length;
  }
  return [out];
}

/** Drop everything past the first two channels, as the firmware does. */
export function firstTwo(channels) {
  return channels.length > 2 ? channels.slice(0, 2) : channels;
}

/**
 * Trim leading and trailing silence below `threshold` (linear), keeping a few
 * milliseconds of lead-in so a transient is not clipped off its own attack.
 */
export function trimSilence(channels, rate, threshold = 0.001) {
  const n = channels[0].length;
  const loud = (i) => channels.some((c) => Math.abs(c[i]) > threshold);
  let start = 0;
  while (start < n && !loud(start)) start++;
  let end = n;
  while (end > start && !loud(end - 1)) end--;
  if (start === 0 && end === n) return channels;
  if (start >= end) return channels.map(() => new Float32Array(0));
  const pad = Math.min(start, Math.round(rate * 0.002));
  return channels.map((c) => c.slice(start - pad, end));
}

export function scale(channels, gain) {
  if (gain === 1) return channels;
  return channels.map((c) => {
    const out = new Float32Array(c.length);
    for (let i = 0; i < c.length; i++) out[i] = c[i] * gain;
    return out;
  });
}

/**
 * Decode anything the browser can — MP3, AIFF, FLAC, M4A, Ogg, WAV in formats
 * the Rodeo would refuse — into float channels. Browser only; WebAudio does the
 * work and there is no point reimplementing it.
 */
export async function decodeAudio(arrayBuffer) {
  // 48 kHz so no resampling happens twice: the context decodes to its own rate.
  const Ctx = window.OfflineAudioContext ?? window.webkitOfflineAudioContext;
  const probe = new Ctx(1, 1, 48000);
  const audio = await probe.decodeAudioData(arrayBuffer);
  const channels = [];
  for (let c = 0; c < audio.numberOfChannels; c++) channels.push(audio.getChannelData(c));
  return { channels, rate: audio.sampleRate, frames: audio.length };
}
