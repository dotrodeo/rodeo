// Turn kicad-cli board renders into committable web derivatives.
//
// Board renders are mostly flat black soldermask with white silkscreen and a
// little gold, so a palette PNG crushes them far better than a re-encode at
// full colour depth. (macOS `sips` makes them *bigger* — don't use it.)
//
//   node scripts/shrink-renders.mjs <src-dir> [dest-dir]
//
// Regenerate the sources first — see web/README.md for the kicad-cli commands.

import sharp from 'sharp';
import { readdir, stat, mkdir } from 'node:fs/promises';

const MAX_WIDTH = 1400;
const BUDGET_KB = 500; // matches scripts/check-assets.mjs

const src = process.argv[2];
const dest = process.argv[3] ?? 'src/assets/hardware';

if (!src) {
  console.error('usage: node scripts/shrink-renders.mjs <src-dir> [dest-dir]');
  process.exit(2);
}

await mkdir(dest, { recursive: true });

const files = (await readdir(src)).filter((f) => f.endsWith('.png')).sort();
if (files.length === 0) {
  console.error(`no .png files in ${src}`);
  process.exit(2);
}

let over = 0;
for (const file of files) {
  await sharp(`${src}/${file}`)
    .resize({ width: MAX_WIDTH, withoutEnlargement: true })
    .png({ compressionLevel: 9, palette: true, quality: 92, effort: 10 })
    .toFile(`${dest}/${file}`);

  const kb = Math.round((await stat(`${dest}/${file}`)).size / 1024);
  if (kb > BUDGET_KB) over++;
  console.log(`${String(kb).padStart(6)}KB  ${file}${kb > BUDGET_KB ? '  OVER BUDGET' : ''}`);
}

if (over > 0) {
  console.error(`\n${over} file(s) over the ${BUDGET_KB}KB budget — lower MAX_WIDTH or crop.`);
  process.exit(1);
}
console.log(`\nall ${files.length} within the ${BUDGET_KB}KB budget`);
