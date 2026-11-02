// The card, through the File System Access API.
//
// In disk mode the Rodeo's SD card is an ordinary volume, so the user picks it
// once and everything after that reads and writes in place. No zip, no copy
// step, no "now drag these into the right folders".
//
// Chrome and Edge only. `supported()` is what the UI checks before offering any
// of it.

import { isTxt, isWav, safeFilename } from './card.js';
import { readWavHeader } from './wav.js';

export const supported = () => typeof window !== 'undefined' && 'showDirectoryPicker' in window;

/** Ask for the card. Throws AbortError if the user cancels — let it through. */
export async function pickCard() {
  return window.showDirectoryPicker({ id: 'rodeo-card', mode: 'readwrite' });
}

async function dirHandle(root, name, { create = false } = {}) {
  try {
    return await root.getDirectoryHandle(name, { create });
  } catch (e) {
    if (e.name === 'NotFoundError') return null;
    throw e;
  }
}

/**
 * Read a card into the shape `buildLibrary` wants. Reads every WAV's header,
 * not its audio: a 300-file card is 300 small reads, not 300 decodes.
 *
 * `onProgress(done, total)` so the UI can show something during a slow card.
 */
export async function readCard(root, onProgress = () => {}) {
  const samples = [];
  const kits = new Map();
  const presets = [];
  const unreadable = [];

  const samplesDir = await dirHandle(root, 'samples');
  const kitsDir = await dirHandle(root, 'kits');
  const presetsDir = await dirHandle(root, 'presets');

  // Count first so progress means something.
  const jobs = [];
  if (samplesDir) {
    for await (const [name, h] of samplesDir.entries()) {
      if (h.kind === 'file' && isWav(name)) jobs.push({ kind: 'sample', name, handle: h });
    }
  }
  if (kitsDir) {
    for await (const [kitName, kitHandle] of kitsDir.entries()) {
      if (kitHandle.kind !== 'directory' || kitName.startsWith('.')) continue;
      kits.set(kitName, []);
      for await (const [name, h] of kitHandle.entries()) {
        if (h.kind === 'file' && isWav(name)) jobs.push({ kind: 'kit', kit: kitName, name, handle: h });
      }
    }
  }
  if (presetsDir) {
    for await (const [name, h] of presetsDir.entries()) {
      if (h.kind === 'file' && isTxt(name)) jobs.push({ kind: 'preset', name, handle: h });
    }
  }

  let done = 0;
  for (const job of jobs) {
    try {
      const file = await job.handle.getFile();
      if (job.kind === 'preset') {
        presets.push({ name: job.name.replace(/\.txt$/i, ''), text: await file.text() });
      } else {
        const header = readWavHeader(await file.arrayBuffer());
        const entry = { name: job.name, bytes: file.size, ...header };
        if (job.kind === 'sample') samples.push(entry);
        else kits.get(job.kit).push(entry);
      }
    } catch (e) {
      unreadable.push({ name: job.name, why: e.message });
    }
    onProgress(++done, jobs.length);
  }

  return { samples, kits, presets, unreadable };
}

async function write(dir, name, data) {
  const handle = await dir.getFileHandle(name, { create: true });
  const w = await handle.createWritable();
  await w.write(data);
  await w.close();
}

/** Write a WAV into /samples, returning the name actually used. */
export async function writeSample(root, name, buffer) {
  const dir = await dirHandle(root, 'samples', { create: true });
  const safe = ensureWav(safeFilename(name));
  await write(dir, safe, buffer);
  return safe;
}

/** Write a WAV into /kits/<kit>/. */
export async function writeKitFile(root, kit, name, buffer) {
  const kitsDir = await dirHandle(root, 'kits', { create: true });
  const dir = await kitsDir.getDirectoryHandle(safeFilename(kit), { create: true });
  const safe = ensureWav(safeFilename(name));
  await write(dir, safe, buffer);
  return safe;
}

export async function writePreset(root, name, text) {
  const dir = await dirHandle(root, 'presets', { create: true });
  const safe = safeFilename(name).replace(/\.txt$/i, '') + '.txt';
  await write(dir, safe, text);
  return safe;
}

export async function removeSample(root, name) {
  const dir = await dirHandle(root, 'samples');
  if (dir) await dir.removeEntry(name);
}

export async function removeKit(root, kit) {
  const dir = await dirHandle(root, 'kits');
  if (dir) await dir.removeEntry(kit, { recursive: true });
}

export async function removeKitFile(root, kit, name) {
  const kitsDir = await dirHandle(root, 'kits');
  if (!kitsDir) return;
  const dir = await kitsDir.getDirectoryHandle(kit);
  await dir.removeEntry(name);
}

export async function removePreset(root, name) {
  const dir = await dirHandle(root, 'presets');
  if (dir) await dir.removeEntry(`${name}.txt`);
}

/**
 * Renumber a kit so its files land on the keys the user arranged. Kit files map
 * to notes in name order, so the order IS the mapping: writing `01_`, `02_`
 * prefixes is how you say "this one is C2".
 *
 * Two passes through temporary names, because renaming `02` to `01` while an
 * `01` still exists would clobber it.
 */
export async function renumberKit(root, kit, orderedNames) {
  const kitsDir = await dirHandle(root, 'kits');
  if (!kitsDir) return [];
  const dir = await kitsDir.getDirectoryHandle(kit);

  const width = String(orderedNames.length).length;
  const stripPrefix = (n) => n.replace(/^\d+[_-]/, '');
  const final = orderedNames.map((name, i) => ({
    from: name,
    to: `${String(i + 1).padStart(width, '0')}_${stripPrefix(name)}`,
  }));
  if (final.every(({ from, to }) => from === to)) return final;

  const buffers = new Map();
  for (const { from } of final) {
    const h = await dir.getFileHandle(from);
    buffers.set(from, await (await h.getFile()).arrayBuffer());
  }
  for (const { from } of final) await dir.removeEntry(from);
  for (const { from, to } of final) await write(dir, to, buffers.get(from));
  return final;
}

function ensureWav(name) {
  return /\.wav$/i.test(name) ? name : `${name}.wav`;
}

/** Fallback for browsers without the API: hand back a zip the user copies. */
export async function downloadZip(files, filename = 'rodeo-card.zip') {
  const blob = new Blob([buildZip(files)], { type: 'application/zip' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = filename;
  a.click();
  URL.revokeObjectURL(a.href);
}

/**
 * A stored (uncompressed) zip. Small enough to inline and it avoids a
 * dependency that needs updating in a year; samples are already compressed
 * audio or raw PCM that would not shrink much in deflate anyway.
 */
function buildZip(files) {
  const enc = new TextEncoder();
  const chunks = [];
  const central = [];
  let offset = 0;

  const crcTable = (() => {
    const t = new Uint32Array(256);
    for (let i = 0; i < 256; i++) {
      let c = i;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      t[i] = c >>> 0;
    }
    return t;
  })();
  const crc32 = (bytes) => {
    let c = 0xffffffff;
    for (let i = 0; i < bytes.length; i++) c = crcTable[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  };

  for (const { path, data } of files) {
    const name = enc.encode(path);
    const bytes = new Uint8Array(data instanceof ArrayBuffer ? data : enc.encode(data));
    const crc = crc32(bytes);
    const local = new DataView(new ArrayBuffer(30));
    local.setUint32(0, 0x04034b50, true);
    local.setUint16(4, 20, true);
    local.setUint16(8, 0, true); // stored
    local.setUint32(14, crc, true);
    local.setUint32(18, bytes.length, true);
    local.setUint32(22, bytes.length, true);
    local.setUint16(26, name.length, true);
    chunks.push(new Uint8Array(local.buffer), name, bytes);

    const dir = new DataView(new ArrayBuffer(46));
    dir.setUint32(0, 0x02014b50, true);
    dir.setUint16(4, 20, true);
    dir.setUint16(6, 20, true);
    dir.setUint32(16, crc, true);
    dir.setUint32(20, bytes.length, true);
    dir.setUint32(24, bytes.length, true);
    dir.setUint16(28, name.length, true);
    dir.setUint32(42, offset, true);
    central.push(new Uint8Array(dir.buffer), name);
    offset += 30 + name.length + bytes.length;
  }

  const centralSize = central.reduce((n, c) => n + c.length, 0);
  const end = new DataView(new ArrayBuffer(22));
  end.setUint32(0, 0x06054b50, true);
  end.setUint16(8, central.length / 2, true);
  end.setUint16(10, central.length / 2, true);
  end.setUint32(12, centralSize, true);
  end.setUint32(16, offset, true);
  return new Blob([...chunks, ...central, new Uint8Array(end.buffer)]);
}
