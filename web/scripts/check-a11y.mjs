// Static accessibility checks over the built HTML.
//
// Not a replacement for axe — these are the regressions that are cheap to
// detect without a browser and expensive to notice by eye. No Chrome, no
// server, no flake, so it can gate every build.
//
//   node scripts/check-a11y.mjs      (after npm run build)

import { readdir, readFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';

const DIST = resolve(dirname(new URL(import.meta.url).pathname), '../dist');

if (!existsSync(DIST)) {
  console.error('check-a11y: no dist/ — run npm run build first');
  process.exit(1);
}

async function* walk(dir) {
  for (const e of await readdir(dir, { withFileTypes: true })) {
    const p = join(dir, e.name);
    if (e.isDirectory()) yield* walk(p);
    else yield p;
  }
}

const strip = (s) =>
  s
    .replace(/<[^>]*>/g, ' ')
    .replace(/&[a-z]+;|&#\d+;/gi, ' ')
    .replace(/\s+/g, ' ')
    .trim();

const problems = [];
let pages = 0;

for await (const file of walk(DIST)) {
  if (!file.endsWith('.html')) continue;
  const html = await readFile(file, 'utf8');
  const rel = file.slice(DIST.length);
  pages++;
  const say = (m) => problems.push(`${rel}: ${m}`);

  // 1. Language, so screen readers pick a voice.
  if (!/<html[^>]*\slang="[a-z]{2}/i.test(html)) say('<html> has no lang attribute');

  // 2. Exactly one h1 — the document's name.
  const h1 = (html.match(/<h1[\s>]/g) ?? []).length;
  if (h1 === 0) say('no <h1>');
  else if (h1 > 1) say(`${h1} <h1> elements; there should be exactly one`);

  // 3. Every image needs alt. An empty alt is allowed and means decorative.
  for (const [tag] of html.matchAll(/<img\b[^>]*>/g)) {
    if (!/\salt=/.test(tag)) say(`<img> without alt: ${tag.slice(0, 90)}`);
  }

  // 4. A link or button with no accessible name is a dead end for anyone not
  //    looking at it. Text, aria-label, or a titled child all count.
  for (const [, attrs, inner] of html.matchAll(/<(?:a|button)\b([^>]*)>([\s\S]*?)<\/(?:a|button)>/g)) {
    if (/\saria-hidden="true"/.test(attrs)) continue;
    if (/\saria-label(?:ledby)?="[^"]+"/.test(attrs)) continue;
    if (/\stitle="[^"]+"/.test(attrs)) continue;
    if (strip(inner)) continue;
    if (/<(?:img|svg)\b/.test(inner) && /\salt="[^"]+"/.test(inner)) continue;
    say(`link or button with no accessible name: <a${attrs.slice(0, 70)}>`);
  }

  // 5. Every form control needs a label, explicit or otherwise.
  for (const [tag] of html.matchAll(/<(?:input|textarea|select)\b[^>]*>/g)) {
    if (/\stype="(?:hidden|submit|button|image)"/.test(tag)) continue;
    if (/\saria-label(?:ledby)?="[^"]+"/.test(tag)) continue;
    const id = tag.match(/\sid="([^"]+)"/)?.[1];
    if (id && html.includes(`for="${id}"`)) continue;
    say(`form control with no label: ${tag.slice(0, 90)}`);
  }
}

if (problems.length) {
  console.error(`\na11y: ${problems.length} problem(s) across ${pages} page(s)\n`);
  for (const p of problems) console.error(`  ${p}`);
  console.error('');
  process.exit(1);
}
console.log(`a11y ok — ${pages} page(s): lang, one h1, alt text, named controls`);
