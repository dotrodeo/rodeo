// Turn hardware photographs into committable web derivatives.
//
// Photos beat renders on a build-in-public site — a render of a board nobody
// has held reads as vaporware. Renders stay only for the boards not yet shot.
//
//   node scripts/prepare-photos.mjs <src-dir>
//
// Sharp strips metadata unless you ask for it, which is the behaviour we want:
// phone photos can carry GPS. The sources were checked clean before import,
// and re-encoding guarantees it.

import sharp from 'sharp';
import { stat } from 'node:fs/promises';
import { existsSync } from 'node:fs';

const MAX_EDGE = 1600;
const BUDGET_KB = 500;
const DEST = 'src/assets/hardware';

// Explicit mapping, so a re-import is reproducible and the provenance of each
// committed file is recorded rather than depending on the order they arrived.
const MAP = [
  ['1.webp', 'rodeo-encoder-hub-back.webp'],
  ['3.webp', 'rodeo-encoder-hub.webp'],
  ['4.webp', 'rodeo-motherboard-daisy.webp'],
  ['5.webp', 'rodeo-motherboard-io.webp'],
  ['6.webp', 'rodeo-motherboard-back.webp'],
  ['7.webp', 'rodeo-motherboard.webp'],
  // 2.webp is byte-identical to 1.webp — skipped.
];

const src = process.argv[2];
if (!src) {
  console.error('usage: node scripts/prepare-photos.mjs <src-dir>');
  process.exit(2);
}

let over = 0;
for (const [from, to] of MAP) {
  const input = `${src}/${from}`;
  if (!existsSync(input)) {
    console.error(`missing source: ${input}`);
    process.exit(2);
  }
  await sharp(input)
    .resize({ width: MAX_EDGE, height: MAX_EDGE, fit: 'inside', withoutEnlargement: true })
    .webp({ quality: 82, effort: 6 })
    .toFile(`${DEST}/${to}`);

  const kb = Math.round((await stat(`${DEST}/${to}`)).size / 1024);
  if (kb > BUDGET_KB) over++;
  console.log(`${String(kb).padStart(6)}KB  ${to}${kb > BUDGET_KB ? '  OVER BUDGET' : ''}`);
}

if (over) {
  console.error(`\n${over} file(s) over ${BUDGET_KB}KB — lower MAX_EDGE or quality.`);
  process.exit(1);
}
console.log(`\n${MAP.length} photo(s) prepared, all within the ${BUDGET_KB}KB budget`);
