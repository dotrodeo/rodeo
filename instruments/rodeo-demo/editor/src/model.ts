// The library on a card, and what the Rodeo will make of it.
//
// Everything here is pure: it takes a description of the card's files and
// returns what the firmware would do with them. That means it runs under
// `node --test` without a browser, which is the only reason to trust it —
// these rules are a copy of the firmware's and copies drift.

import {
  CARD_READ_BYTES_PER_S,
  KIT_FIRST_NOTE,
  MAX_FILENAME,
  MAX_FILES,
  MAX_KIT_FILES,
  MAX_NAME,
  MAX_PRESETS,
  MAX_SOUNDS,
  POOL_SAMPLES,
  cardOrder,
  isWav,
  loadedChannels,
  noteName,
  poolCost,
  soundName,
} from './card.ts';
import type { Card, CardFile } from './card.ts';
import type { Preset } from './preset.ts';

/** What the loader did with one file: in the pool, or skipped and why. */
export interface Placed {
  loaded: boolean;
  why?: string;
  /** int16 slots it takes in the pool. */
  cost?: number;
}

/** A file in the library: the card's view of it, what the loader did, its key. */
export type LibraryFile = CardFile &
  Placed & {
    /** The key a kit file plays on; null for a pitched sample. */
    note: number | null;
  };

/** One entry in the Rodeo's sound list. */
export interface Sound {
  name: string;
  kit: boolean;
  files: LibraryFile[];
}

export interface Library {
  sounds: Sound[];
  /** WAVs counted against `kMaxSamples`. */
  files: number;
  /** int16 slots used in the pool. */
  pool: number;
  /** Why the scan stopped early, if it did. */
  notes: string[];
}

export type IssueLevel = 'error' | 'warn' | 'info';

export interface Issue {
  level: IssueLevel;
  text: string;
  /** The sound or preset it is about, or null for the card as a whole. */
  where: string | null;
}

export interface PoolStats {
  samples: number;
  bytes: number;
  capacity: number;
  fraction: number;
  files: number;
  sounds: number;
  loadSeconds: number;
}

/**
 * Build the sound list exactly as `Scan()` does (samples.cpp): every WAV in
 * /samples first, then every folder in /kits, each in case-insensitive name
 * order, stopping at 64 sounds and 512 files.
 *
 * @param card {{ samples: File[], kits: Map<string, File[]> }} where a File is
 *        { name, frames, channels, rate, bytes, accepted, why }
 */
export function buildLibrary(card: Pick<Card, 'samples' | 'kits'>): Library {
  const sounds: Sound[] = [];
  let files = 0;
  let pool = 0;
  const notes: string[] = [];

  const stop = (reason: string) => notes.push(reason);

  // --- /samples: one pitched sound per WAV ---
  const pitched = cardOrder(card.samples.map((f) => f.name));
  for (const name of pitched) {
    // Every name came from this list, so the find cannot miss.
    const file = card.samples.find((f) => f.name === name) as CardFile;
    if (sounds.length >= MAX_SOUNDS) {
      stop(`${pitched.length - sounds.length} more sounds than the Rodeo lists; it stops at ${MAX_SOUNDS}`);
      break;
    }
    if (files >= MAX_FILES) {
      stop(`more than ${MAX_FILES} WAVs on the card; the rest are not loaded`);
      break;
    }
    const placed = place(file, pool);
    pool = placed.pool;
    files++;
    sounds.push({
      name: soundName(name),
      kit: false,
      files: [{ ...file, ...placed.result, note: null }],
    });
  }

  // --- /kits: one sound per folder, one key per WAV from C2 up ---
  const kitNames = cardOrder([...card.kits.keys()]);
  for (const kitName of kitNames) {
    if (sounds.length >= MAX_SOUNDS) {
      stop(`the kits after "${kitName}" are past the Rodeo's ${MAX_SOUNDS}-sound limit`);
      break;
    }
    const inKit = card.kits.get(kitName) ?? [];
    const ordered = cardOrder(inKit.map((f) => f.name));
    if (ordered.length === 0) continue; // `files == 0` -> the kit is skipped
    const entries: LibraryFile[] = [];
    for (let i = 0; i < ordered.length; i++) {
      if (files >= MAX_FILES) {
        stop(`more than ${MAX_FILES} WAVs on the card; "${kitName}" is cut short`);
        break;
      }
      const file = inKit.find((f) => f.name === ordered[i]) as CardFile; // as above
      const placed = place(file, pool);
      pool = placed.pool;
      files++;
      entries.push({ ...file, ...placed.result, note: KIT_FIRST_NOTE + i });
    }
    sounds.push({ name: soundName(kitName), kit: true, files: entries });
  }

  return { sounds, files, pool, notes };
}

/** One file against the pool, the way `AddSample`/the loader do. */
function place(file: CardFile, pool: number): { pool: number; result: Placed } {
  if (!file.accepted) {
    return { pool, result: { loaded: false, why: file.why ?? 'the Rodeo cannot read this WAV' } };
  }
  const cost = poolCost(file.frames, file.channels);
  if (file.frames === 0) {
    return { pool, result: { loaded: false, why: 'no audio in it' } };
  }
  if (pool + cost > POOL_SAMPLES) {
    return { pool, result: { loaded: false, why: 'the sample pool is full', cost } };
  }
  return { pool: pool + cost, result: { loaded: true, cost } };
}

/** Everything wrong with a card, worst first. */
export function validate(library: Library, presets: Preset[]): Issue[] {
  const issues: Issue[] = [];
  const add = (level: IssueLevel, text: string, where: string | null) =>
    issues.push({ level, text, where });

  const names = new Map<string, 'kit' | 'sample'>();
  for (const sound of library.sounds) {
    // A sound the Rodeo will not play at all.
    const dead = sound.files.filter((f) => !f.loaded);
    if (dead.length === sound.files.length) {
      add('error', `"${sound.name}" will not play: ${dead[0].why}`, sound.name);
    } else if (dead.length) {
      const which = sound.kit
        ? dead.map((f) => noteName(f.note as number)).join(', ') // a kit file always has a key
        : dead.map((f) => f.name).join(', ');
      add('warn', `"${sound.name}" is missing ${dead.length} of ${sound.files.length} samples (${which})`, sound.name);
    }

    // Two sounds with the same name: `sound =` finds the first, silently.
    const key = sound.name.toLowerCase();
    if (names.has(key)) {
      add('error', `two sounds are both called "${sound.name}" — a preset naming it gets the ${names.get(key)} one`, sound.name);
    } else {
      names.set(key, sound.kit ? 'kit' : 'sample');
    }

    if (sound.kit && sound.files.length > MAX_KIT_FILES) {
      add('warn', `kit "${sound.name}" has ${sound.files.length} files; keys stop at ${noteName(127)}, so the last ${sound.files.length - MAX_KIT_FILES} have none`, sound.name);
    }

    // Two filenames that differ only in case compare equal under strcasecmp,
    // so the insertion sort leaves them in whatever order f_readdir gave —
    // the directory's, which we cannot predict. For a kit that decides which
    // key each one lands on, so it is worth more than a shrug.
    const byLower = new Map<string, string>();
    for (const f of sound.files) {
      const k = f.name.toLowerCase();
      if (byLower.has(k)) {
        add(
          sound.kit ? 'error' : 'warn',
          `"${byLower.get(k)}" and "${f.name}" differ only in case, so the Rodeo cannot order them` +
            (sound.kit ? ` — which key each gets in "${sound.name}" is luck. Rename one.` : ''),
          sound.name
        );
      } else {
        byLower.set(k, f.name);
      }
    }

    for (const f of sound.files) {
      if (f.name.length > MAX_FILENAME) {
        add('error', `"${f.name}" is ${f.name.length} characters; the Rodeo skips anything over ${MAX_FILENAME}`, sound.name);
      }
      if (!isWav(f.name)) {
        add('error', `"${f.name}" is not a .wav, so it is not loaded`, sound.name);
      }
      if (f.accepted && f.channels > 2) {
        add('warn', `"${f.name}" has ${f.channels} channels; the Rodeo keeps the first two`, sound.name);
      }
    }

    if (soundName(sound.name).length === MAX_NAME) {
      add('warn', `"${sound.name}" is cut to ${MAX_NAME} characters — name a preset's sound by the cut version`, sound.name);
    }
  }

  if (library.sounds.length > MAX_SOUNDS) {
    add('error', `${library.sounds.length} sounds; the Rodeo lists ${MAX_SOUNDS}`, null);
  }
  if (library.files > MAX_FILES) {
    add('error', `${library.files} WAVs; the Rodeo loads ${MAX_FILES}`, null);
  }
  for (const note of library.notes) add('error', note, null);

  // --- presets ---
  if (presets.length > MAX_PRESETS) {
    add('error', `${presets.length} presets; the Rodeo reads ${MAX_PRESETS}`, null);
  }
  for (const preset of presets) {
    for (const p of preset.problems) {
      add('error', `preset "${preset.name}" line ${p.line}: ${p.why} — "${p.text}"`, preset.name);
    }
    for (const w of preset.warnings) {
      add('warn', `preset "${preset.name}": ${w}`, preset.name);
    }
    const wanted = preset.values.sound;
    if (wanted !== undefined) {
      // `Apply` looks the name up in the library as it is now; a miss is only
      // reported on the serial port, at play time.
      if (!names.has(wanted.toLowerCase())) {
        const near = closest(wanted, [...names.keys()]);
        add(
          'error',
          `preset "${preset.name}" wants sound "${wanted}", which is not on the card` +
            (near ? ` — did you mean "${near}"?` : ''),
          preset.name
        );
      }
    }
  }

  const rank = { error: 0, warn: 1, info: 2 };
  return issues.sort((a, b) => rank[a.level] - rank[b.level]);
}

/** Cheap nearest name, for "did you mean". Null unless it is clearly close. */
function closest(want: string, haves: string[]): string | null {
  const w = want.toLowerCase();
  let best: string | null = null;
  let bestD = Infinity;
  for (const h of haves) {
    const d = distance(w, h);
    if (d < bestD) {
      bestD = d;
      best = h;
    }
  }
  return best !== null && bestD <= Math.max(2, Math.floor(w.length / 3)) ? best : null;
}

function distance(a: string, b: string): number {
  const prev = Array.from({ length: b.length + 1 }, (_, i) => i);
  for (let i = 1; i <= a.length; i++) {
    let last = prev[0];
    prev[0] = i;
    for (let j = 1; j <= b.length; j++) {
      const t = prev[j];
      prev[j] = Math.min(prev[j] + 1, prev[j - 1] + 1, last + (a[i - 1] === b[j - 1] ? 0 : 1));
      last = t;
    }
  }
  return prev[b.length];
}

/** What the pool meter shows. */
export function poolStats(library: Library): PoolStats {
  const bytes = library.pool * 2;
  const capacity = POOL_SAMPLES * 2;
  // The card is read at about 1.24 MB/s (bring-up 07), and that read is the
  // whole of the wait at power-up.
  const onCard = library.sounds
    .flatMap((s) => s.files)
    .filter((f) => f.loaded)
    .reduce((n, f) => n + (f.bytes ?? 0), 0);
  return {
    samples: library.pool,
    bytes,
    capacity,
    fraction: bytes / capacity,
    files: library.files,
    sounds: library.sounds.length,
    loadSeconds: onCard / CARD_READ_BYTES_PER_S,
  };
}

export function formatBytes(n: number): string {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(0)} KB`;
  return `${(n / (1024 * 1024)).toFixed(1)} MB`;
}

export { noteName, loadedChannels };
