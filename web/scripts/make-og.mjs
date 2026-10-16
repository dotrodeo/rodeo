// Generate the Open Graph cards committed to public/og/.
//
// Run locally, not in CI: it renders text with the real brand faces through
// fontconfig, so it needs Barlow and IBM Plex Mono installed on the machine.
// The output is three small PNGs that get committed, the same arrangement as
// scripts/shrink-renders.mjs.
//
//   node scripts/make-og.mjs
//
// PNG, not AVIF or WebP: Slack, iMessage and X will not render the modern
// formats in a link preview, and a card nobody can see is worse than no card.

import sharp from 'sharp';
import { mkdir } from 'node:fs/promises';

const W = 1200;
const H = 630;

const FIELD = '#111111';
const WHITE = '#ffffff';
const YELLOW = '#ffcc00';
const GRAY = '#b0b0b0';

/** Escape for XML text nodes — an unescaped ampersand breaks the whole SVG. */
const esc = (s) =>
  s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

const cards = [
  {
    name: 'default',
    lines: ['Build the panel.', 'Skip the PCB.'],
    kicker: 'Open-source synth platform',
  },
  {
    name: 'ecosystem',
    lines: ['One host board', 'and its satellites'],
    kicker: 'Boards, pinouts and published sources',
  },
  {
    name: 'log',
    lines: ['Building it', 'in the open'],
    kicker: 'The Rodeo build log',
  },
  {
    // Covers /docs/ and /sdk/ both — Starlight takes one card for the whole
    // instance, and "documentation" is honest for either.
    name: 'docs',
    lines: ['Pinouts, contracts', 'and drivers'],
    kicker: 'Rodeo documentation',
  },
];

function svg({ lines, kicker }) {
  // Two lines at 96px is the largest that keeps the longest title on one line
  // at this width; the layout is fixed rather than measured because there are
  // three cards and they are checked by eye.
  const titleY = lines.length > 1 ? [330, 440] : [385];
  const title = lines
    .map((l, i) => `<text x="96" y="${titleY[i]}" class="t">${esc(l)}</text>`)
    .join('');

  return `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}">
  <style>
    .t { font-family: 'Barlow'; font-weight: 900; font-size: 96px; fill: ${WHITE}; letter-spacing: -2px; }
    .mark { font-family: 'Barlow'; font-weight: 900; font-size: 40px; fill: ${WHITE}; letter-spacing: 1px; }
    .k { font-family: 'IBM Plex Mono'; font-size: 24px; fill: ${GRAY}; }
    .u { font-family: 'IBM Plex Mono'; font-size: 24px; fill: ${YELLOW}; }
  </style>
  <rect width="${W}" height="${H}" fill="${FIELD}"/>
  <rect x="0" y="0" width="16" height="${H}" fill="${YELLOW}"/>
  <text x="96" y="130" class="mark">RODEO</text>
  <rect x="96" y="168" width="104" height="4" fill="${YELLOW}"/>
  ${title}
  <text x="96" y="536" class="k">${esc(kicker)}</text>
  <text x="96" y="576" class="u">rodeooh.rodeo</text>
</svg>`;
}

await mkdir('public/og', { recursive: true });

for (const card of cards) {
  const out = `public/og/${card.name}.png`;
  const buf = await sharp(Buffer.from(svg(card)))
    .png({ palette: true, quality: 90 })
    .toBuffer();
  await sharp(buf).toFile(out);
  console.log(`${out.padEnd(28)} ${W}x${H}  ${Math.round(buf.length / 1024)}KB`);
}
