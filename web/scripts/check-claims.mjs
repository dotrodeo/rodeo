// Claim-accuracy guardrail — see web/docs/WEB_CLAIMS_AUDIT.md.
//
// Implemented here: check 1 (evidence resolves) and check 2 (TODO proximity),
// over src/data/boards.ts and over the repo paths cited in web/docs/*.md.
// Checks 3-6 (line-hash drift lockfile, banned phrases in built HTML,
// relatedSpecs cross-refs, revision scope) come with the log and specs work.
//
// Run from web/: node scripts/check-claims.mjs

import { readFile, readdir, writeFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, '../..');

const TODO_MARKERS = /\bTODO\b|\bstub\b|\bDRAFT\b|not implemented/i;
const PROXIMITY = 3;

/** Claims whose evidence deliberately sits beside unshipped work. */
const ADJACENCY_EXCEPTIONS = new Map();

const { boards } = await import(pathToFileURL(resolve(HERE, '../src/data/boards.ts')).href).catch(
  async () => {
    // boards.ts imports image assets, which plain node cannot resolve. Parse
    // the evidence strings out of the source instead — this check only needs
    // the citations, not the module.
    const src = await readFile(resolve(HERE, '../src/data/boards.ts'), 'utf8');
    const consts = Object.fromEntries(
      [...src.matchAll(/^const (\w+) = '([^']+)';$/gm)].map((m) => [m[1], m[2]])
    );
    // Three shapes appear in boards.ts and all three must be caught:
    //   evidence: `${R}:131`     template with a constant
    //   evidence: 'path:1-2'     a literal
    //   evidence: FAB            a bare constant — this one was missed for a
    //                            day, hiding seven citations from every check.
    const claims = [...src.matchAll(/evidence:\s*(?:`([^`]+)`|'([^']+)'|(\w+))/g)]
      .map((m) => {
        if (m[3] !== undefined) return consts[m[3]];
        const raw = m[1] ?? m[2];
        return raw.replace(/\$\{(\w+)\}/g, (_, k) => consts[k] ?? `\${${k}}`);
      })
      .filter(Boolean);
    return { boards: [{ slug: '(parsed)', specs: claims.map((evidence) => ({ evidence })) }] };
  }
);

// --- The lockfile -------------------------------------------------------
//
// Checks 1 and 2 prove a cited path exists and that its lines are not blank.
// Neither proves the lines still *say* what the claim says. That gap has been
// exploited twice: once when a document moved repositories, and once when
// freezing the Rev A SDK shifted a README by four lines, so a claim of
// "bench-confirmed on real hardware" came to point at a paragraph about
// control maps. The check stayed green both times.
//
// So the exact text under every cited line range is recorded here and compared
// on every run. The stored value is the text itself rather than a hash,
// because then `git diff` on the lockfile shows a reviewer what changed.
//
//   npm run claims           check against the lock
//   npm run claims:update    re-record, after re-reading what moved
//
// Updating is deliberate and the diff is meant to be read. That re-read is the
// whole point; a lockfile refreshed without one is ceremony.
const LOCK = resolve(HERE, 'claims.lock.json');
const UPDATE = process.argv.includes('--update');
/** cite -> the normalised text those lines currently hold. */
const quoted = new Map();

/** Collapse whitespace, so re-wrapping a paragraph is not a false alarm. */
const normalise = (s) => s.replace(/\s+/g, ' ').trim();

const problems = [];
let checked = 0;

/** One citation: the path must exist, and any line range must point at something. */
async function checkCite(who, cite, { proximity = true } = {}) {
  checked++;
  const [path, range] = String(cite).split(':');
  const abs = resolve(REPO, path);

  if (!existsSync(abs)) {
    problems.push(`${who}: evidence path does not exist — ${path}`);
    return;
  }
  if (!range) return; // a bare path is allowed for whole-file evidence

  const lines = (await readFile(abs, 'utf8')).split('\n');
  // Parts of this one citation (`:12,45-47`), collected locally. Accumulating
  // straight into `quoted` appended a fresh copy every time another source
  // cited the same range, which read as drift that was not there.
  const parts = [];
  for (const part of range.split(',')) {
    const [a, b] = part.split('-').map(Number);
    const start = a;
    const end = Number.isFinite(b) ? b : a;

    if (!Number.isFinite(start) || start < 1 || end > lines.length) {
      problems.push(`${who}: ${path}:${part} is out of range (file has ${lines.length})`);
      continue;
    }
    const body = lines.slice(start - 1, end).join('\n');
    if (!body.trim()) {
      problems.push(`${who}: ${path}:${part} is blank — cite a line that says something`);
      continue;
    }
    if (/^\|[\s|:-]+\|$/.test(body.trim())) {
      problems.push(`${who}: ${path}:${part} is a table separator, not evidence`);
      continue;
    }
    parts.push(body);

    if (!proximity) continue;

    const near = lines.slice(Math.max(0, start - 1 - PROXIMITY), end + PROXIMITY).join('\n');
    const hit = near.match(TODO_MARKERS);
    if (hit && !ADJACENCY_EXCEPTIONS.has(cite)) {
      problems.push(
        `${who}: ${path}:${part} sits within ${PROXIMITY} lines of "${hit[0]}" — ` +
          `re-verify, cite better evidence, or record an adjacency exception`
      );
    }
  }

  // What this citation says right now, for the lockfile comparison.
  if (parts.length) quoted.set(cite, normalise(parts.join(' ')));
}

for (const board of boards) {
  const cites = [
    ...(board.specs ?? []).map((s) => s.evidence),
    ...(board.caveats ?? []).map((c) => c.evidence),
  ].filter(Boolean);

  for (const cite of cites) await checkCite(board.slug, cite);
}

// The documents in web/docs/ cite the hardware tree too, and the claims audit
// cites it most of all. Those citations went unchecked until 2026-10-15, when
// merging the Rev B branch moved a file out from under nine of them and
// nothing noticed — the file that defines the truth rules had rotted silently.
//
// Markdown gets the path and range check but not TODO proximity: these docs
// discuss unfinished work on purpose, which is the opposite of a spec table
// claiming something is shipped.
const DOCS = resolve(HERE, '../docs');
// Longest extension first: `ts` before `tsx` would clip main.cpp to main.c
// and wrangler.jsonc to wrangler.json, inventing two files that do not exist.
const CITE_IN_PROSE =
  /(?:^|[\s(`[])((?:hardware|rev-a|instruments|rodeo-sdk|tools|web)\/[A-Za-z0-9._/-]+\.(?:kicad_pcb|kicad_sch|jsonc|json|mjs|tsx|ts|hpp|cpp|md|py|h|c))(:\d+(?:-\d+)?(?:,\d+(?:-\d+)?)*)?/g;

for (const name of (await readdir(DOCS)).filter((f) => f.endsWith('.md'))) {
  const text = await readFile(resolve(DOCS, name), 'utf8');
  const seen = new Set();
  for (const m of text.matchAll(CITE_IN_PROSE)) {
    const cite = m[1] + (m[2] ?? '');
    if (seen.has(cite)) continue;
    seen.add(cite);
    await checkCite(`docs/${name}`, cite, { proximity: false });
  }
}

// --- Compare against the lock -------------------------------------------
const sorted = Object.fromEntries([...quoted.entries()].sort(([a], [b]) => a.localeCompare(b)));

if (UPDATE) {
  await writeFile(LOCK, JSON.stringify(sorted, null, 2) + '\n');
  console.log(`claims: lockfile re-recorded — ${Object.keys(sorted).length} quoted citation(s)`);
  console.log('Read the diff before committing it. That is what the file is for.');
} else {
  let lock = null;
  try {
    lock = JSON.parse(await readFile(LOCK, 'utf8'));
  } catch {
    problems.push('no claims.lock.json — run `npm run claims:update` once to record it');
  }
  if (lock) {
    for (const [cite, text] of Object.entries(sorted)) {
      if (!(cite in lock)) {
        problems.push(`${cite} is not in the lockfile — run \`npm run claims:update\``);
      } else if (lock[cite] !== text) {
        problems.push(
          `${cite} no longer says what it said.\n` +
            `      was: ${lock[cite].slice(0, 110)}\n` +
            `      now: ${text.slice(0, 110)}\n` +
            '      Re-read it. If the claim still holds, `npm run claims:update`.'
        );
      }
    }
  }
}


if (problems.length) {
  console.error(`\nclaims: ${problems.length} problem(s) in ${checked} citation(s)\n`);
  for (const p of problems) console.error(`  ${p}`);
  console.error('');
  process.exit(1);
}
console.log(
  `claims ok — ${checked} citation(s) resolve, ${Object.keys(sorted).length} quoted text(s) unchanged`
);
