// Turn board renders into committable web derivatives.
//
//   node scripts/shrink-renders.mjs                      # every Rev-B board
//   node scripts/shrink-renders.mjs <src-dir> [dest-dir]
//
// With no arguments it reads the studio renders tools/render/render.py writes
// into hardware/<board>/renders/ (front, back and iso, JPEG) and writes WebP
// into src/assets/hardware/rev-b/. Regenerate those first when a board changes:
//
//   blender -b -P tools/render/render.py -- --all        # from the repo root
//
// Studio renders sit on a graded backdrop, so they go to WebP: a palette PNG
// bands the gradient. kicad-cli's flat renders (.png) are mostly black mask and
// white silkscreen, and a palette PNG still crushes those far better than a
// full-colour re-encode. (macOS `sips` makes them *bigger* — don't use it.)

import sharp from 'sharp';
import { readdir, stat, mkdir } from 'node:fs/promises';
import { join } from 'node:path';

const MAX_WIDTH = 1400;
const HERO_WIDTH = 2400; // <board>-wide: a page's hero, shown up to ~1180 CSS px
const BUDGET_KB = 500; // matches scripts/check-assets.mjs

let jobs; // [src-dir, dest-dir]
if (process.argv[2]) {
  jobs = [[process.argv[2], process.argv[3] ?? 'src/assets/hardware']];
} else {
  const hw = '../hardware';
  const boards = (await readdir(hw)).filter((d) => d.startsWith('rodeo-')).sort();
  jobs = boards.map((b) => [join(hw, b, 'renders'), 'src/assets/hardware/rev-b']);
}

let over = 0;
let count = 0;
for (const [src, dest] of jobs) {
  await mkdir(dest, { recursive: true });
  let files;
  try {
    files = (await readdir(src)).filter((f) => /\.(png|jpe?g)$/.test(f)).sort();
  } catch {
    console.error(`no renders in ${src} — run tools/render/render.py first`);
    process.exit(2);
  }
  for (const file of files) {
    const flat = file.endsWith('.png');
    const out = flat ? file : file.replace(/\.jpe?g$/, '.webp');
    const width = /-wide\./.test(file) ? HERO_WIDTH : MAX_WIDTH;
    const img = sharp(join(src, file)).resize({ width, withoutEnlargement: true });
    await (flat
      ? img.png({ compressionLevel: 9, palette: true, quality: 92, effort: 10 })
      : img.webp({ quality: 82, effort: 6 })
    ).toFile(join(dest, out));

    const kb = Math.round((await stat(join(dest, out))).size / 1024);
    if (kb > BUDGET_KB) over++;
    count++;
    console.log(`${String(kb).padStart(6)}KB  ${out}${kb > BUDGET_KB ? '  OVER BUDGET' : ''}`);
  }
}

if (count === 0) {
  console.error('no renders found');
  process.exit(2);
}
if (over > 0) {
  console.error(`\n${over} file(s) over the ${BUDGET_KB}KB budget — lower MAX_WIDTH or crop.`);
  process.exit(1);
}
console.log(`\nall ${count} within the ${BUDGET_KB}KB budget`);
