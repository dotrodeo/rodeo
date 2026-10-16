// Fail the build on a broken internal link.
//
// Offline and deterministic on purpose: it resolves links against the built
// output in dist/ and never touches the network. A link check that makes HTTP
// requests is the job everyone learns to ignore, and the failures it reports
// are usually somebody else's outage.
//
// It catches the class of bug that has actually happened here: a document
// moved out of the repository and the links to it kept pointing at nothing.
//
//   node scripts/check-links.mjs      (after npm run build)

import { readdir, readFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';

const DIST = resolve(dirname(new URL(import.meta.url).pathname), '../dist');

if (!existsSync(DIST)) {
  console.error('check-links: no dist/ — run npm run build first');
  process.exit(1);
}

async function* walk(dir) {
  for (const e of await readdir(dir, { withFileTypes: true })) {
    const p = join(dir, e.name);
    if (e.isDirectory()) yield* walk(p);
    else yield p;
  }
}

/** Where a site-absolute path lands in dist/. */
function target(href) {
  const clean = href.split('#')[0].split('?')[0];
  if (clean === '' || clean === '/') return join(DIST, 'index.html');
  const base = join(DIST, clean);
  if (clean.endsWith('/')) return join(base, 'index.html');
  // Extensionless paths are directory routes (trailingSlash: 'always').
  return /\.[a-z0-9]+$/i.test(clean) ? base : join(base, 'index.html');
}

const pages = [];
for await (const f of walk(DIST)) if (f.endsWith('.html')) pages.push(f);

const problems = [];
let checked = 0;

for (const page of pages) {
  const html = await readFile(page, 'utf8');
  const rel = page.slice(DIST.length) || '/';
  const ids = new Set([...html.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1]));

  const refs = [
    ...[...html.matchAll(/\shref="([^"]+)"/g)].map((m) => m[1]),
    ...[...html.matchAll(/\ssrc="([^"]+)"/g)].map((m) => m[1]),
  ];

  for (const href of refs) {
    // Off-site, protocol-relative, and non-navigational schemes are not ours.
    if (/^(https?:|mailto:|tel:|data:|\/\/)/i.test(href)) continue;

    if (href.startsWith('#')) {
      checked++;
      const id = decodeURIComponent(href.slice(1));
      if (id && !ids.has(id)) problems.push(`${rel}  →  ${href}  (no element with that id)`);
      continue;
    }
    // Relative links would resolve against the page's directory; the site
    // emits site-absolute paths everywhere, so anything else is a mistake.
    if (!href.startsWith('/')) {
      problems.push(`${rel}  →  ${href}  (relative link; use a site-absolute path)`);
      continue;
    }

    checked++;
    const t = target(href);
    if (!existsSync(t)) problems.push(`${rel}  →  ${href}  (nothing at ${t.slice(DIST.length)})`);
  }
}

if (problems.length) {
  console.error(`\nlinks: ${problems.length} broken of ${checked} checked\n`);
  for (const p of problems) console.error(`  ${p}`);
  console.error('');
  process.exit(1);
}
console.log(`links ok — ${checked} internal link(s) across ${pages.length} page(s)`);
