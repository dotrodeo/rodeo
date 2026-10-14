// Claim-accuracy guardrail — see web/docs/WEB_CLAIMS_AUDIT.md.
//
// Implemented here: check 1 (evidence resolves) and check 2 (TODO proximity).
// Checks 3-6 (line-hash drift lockfile, banned phrases in built HTML,
// relatedSpecs cross-refs, revision scope) come with the log and specs work.
//
// Run from web/: node scripts/check-claims.mjs

import { readFile } from 'node:fs/promises';
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
    const claims = [...src.matchAll(/evidence:\s*(?:`([^`]+)`|'([^']+)')/g)].map((m) => {
      const raw = m[1] ?? m[2];
      return raw.replace(/\$\{(\w+)\}/g, (_, k) => consts[k] ?? `\${${k}}`);
    });
    return { boards: [{ slug: '(parsed)', specs: claims.map((evidence) => ({ evidence })) }] };
  }
);

const problems = [];
let checked = 0;

for (const board of boards) {
  const cites = [
    ...(board.specs ?? []).map((s) => s.evidence),
    ...(board.caveats ?? []).map((c) => c.evidence),
  ].filter(Boolean);

  for (const cite of cites) {
    checked++;
    const [path, range] = String(cite).split(':');
    const abs = resolve(REPO, path);

    if (!existsSync(abs)) {
      problems.push(`${board.slug}: evidence path does not exist — ${path}`);
      continue;
    }
    if (!range) continue; // a bare path is allowed for whole-file evidence

    const lines = (await readFile(abs, 'utf8')).split('\n');
    for (const part of range.split(',')) {
      const [a, b] = part.split('-').map(Number);
      const start = a;
      const end = Number.isFinite(b) ? b : a;

      if (!Number.isFinite(start) || start < 1 || end > lines.length) {
        problems.push(`${board.slug}: ${path}:${part} is out of range (file has ${lines.length})`);
        continue;
      }
      const body = lines.slice(start - 1, end).join('\n');
      if (!body.trim()) {
        problems.push(`${board.slug}: ${path}:${part} is blank — cite a line that says something`);
        continue;
      }
      if (/^\|[\s|:-]+\|$/.test(body.trim())) {
        problems.push(`${board.slug}: ${path}:${part} is a table separator, not evidence`);
        continue;
      }

      const near = lines.slice(Math.max(0, start - 1 - PROXIMITY), end + PROXIMITY).join('\n');
      const hit = near.match(TODO_MARKERS);
      if (hit && !ADJACENCY_EXCEPTIONS.has(cite)) {
        problems.push(
          `${board.slug}: ${path}:${part} sits within ${PROXIMITY} lines of "${hit[0]}" — ` +
            `re-verify, cite better evidence, or record an adjacency exception`
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
console.log(`claims ok — ${checked} citation(s) resolve, none beside unshipped work`);
