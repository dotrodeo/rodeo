// Fail the build if a committed asset busts the budget in planning/WEB_ARCHITECTURE.md.
// Policy without enforcement does not survive a late-night commit.
import { readdir, stat } from 'node:fs/promises';
import { join } from 'node:path';

const BUDGET = 500 * 1024;
const ROOTS = ['src/assets', 'public'];

async function* walk(dir) {
  let entries;
  try {
    entries = await readdir(dir, { withFileTypes: true });
  } catch {
    return;
  }
  for (const e of entries) {
    const p = join(dir, e.name);
    if (e.isDirectory()) yield* walk(p);
    else yield p;
  }
}

const over = [];
let count = 0;
for (const root of ROOTS) {
  for await (const file of walk(root)) {
    count++;
    const { size } = await stat(file);
    if (size > BUDGET) over.push({ file, kb: Math.round(size / 1024) });
  }
}

if (over.length) {
  console.error(`\n${over.length} asset(s) over the 500KB budget:`);
  for (const o of over) console.error(`  ${String(o.kb).padStart(5)}KB  ${o.file}`);
  console.error('\nRe-encode with: node scripts/shrink-renders.mjs <src-dir>');
  process.exit(1);
}
console.log(`assets ok — ${count} file(s), all under 500KB`);
