---
title: "Rodeo Web — Technical Architecture"
tags: [rodeo, web, astro, cloudflare, decision-record]
status: active
created: 2026-09-17
updated: 2026-09-17
---

# Rodeo Web — Technical Architecture

**Owner:** Derek. The decision record for how the site is built and shipped. Reasons are
recorded alongside decisions, because several of these look arbitrary without them.

## Stack

| Layer | Choice |
|---|---|
| Framework | **Astro 5**, `output: 'static'`, no adapter |
| Styling | **Tailwind v4** via `@tailwindcss/vite` |
| Content | Astro content collections (`glob()` loader), **MDX** for log posts |
| Hosting | **Cloudflare Pages**, project type **Direct Upload** |
| Deploy | **GitHub Actions** → `wrangler pages deploy` |
| Dynamic | Exactly one Cloudflare Pages Function, at `/subscribe` |
| Email | **Buttondown**, with RSS-to-email |
| Node | 22 (pinned in `web/.nvmrc`) |

No UI framework. Nothing on this site needs hydration; adding React to a static marketing
page is the default mistake here, not a neutral choice.

### Three Astro 5 specifics that current tutorials get wrong

Worth stating because each silently wastes an afternoon:

1. The content config lives at **`src/content.config.ts`**, not `src/content/config.ts`.
2. Collections require an **explicit `loader`** — `glob()` from `astro/loaders`.
3. **`@astrojs/tailwind` is deprecated.** Use `@tailwindcss/vite` with `@theme` in CSS.

## Directory layout

```
web/
├── .nvmrc                       # "22"
├── astro.config.mjs
├── package.json
├── tsconfig.json                # extends astro/tsconfigs/strict
├── lighthouse-budget.json
├── functions/
│   └── subscribe.ts             # the one Pages Function
├── public/
│   ├── _headers                 # CSP, cache-control
│   ├── _routes.json             # restrict the Function to /subscribe
│   ├── robots.txt
│   ├── favicon.svg
│   ├── fonts/
│   ├── audio/
│   └── og/
├── scripts/
│   ├── check-claims.mjs         # the guardrail — see WEB_CLAIMS_AUDIT.md
│   └── check-assets.mjs         # fails if any asset > 500 KB
├── docs/                        # these documents
└── src/
    ├── content.config.ts
    ├── content/log/             # YYYY-MM-DD-slug.mdx
    ├── data/
    │   ├── site.ts
    │   ├── specs.ts             # source of truth for every product claim
    │   └── boards.ts            # source of truth for the board catalogue
    ├── assets/                  # web-ready derivatives only, committed
    ├── components/
    ├── layouts/
    ├── lib/
    ├── pages/
    └── styles/
        ├── global.css           # @import "tailwindcss" + @theme tokens
        └── tokens.css           # transcription of WEB_BRAND.md
```

`astro.config.mjs` essentials: `site: 'https://rodeooh.rodeo'`, `trailingSlash: 'always'`
with `build.format: 'directory'` (so canonical URLs, internal links, RSS and Pages'
redirect behaviour all agree — a mismatch here produces duplicate-URL SEO noise),
integrations `[mdx(), sitemap()]`, `vite: { plugins: [tailwindcss()] }`.

## Content model

### Log posts — `src/content/log/*.mdx`

MDX rather than plain Markdown, because posts need `<Figure>` (optimized responsive images
with enforced `alt`), `<AudioDemo>`, and `<Claim id="…" />` so a post can reference a live
spec status instead of restating it. In plain MD you write raw `<img>` and lose
`astro:assets` entirely. Components are mapped globally in the post layout so posts read
like Markdown with three extra tags.

Schema:

| Field | Rule |
|---|---|
| `title` | string, 10–70 chars (70 is the practical OG/SERP limit) |
| `date` | `z.coerce.date()` |
| `updated` | optional date |
| `summary` | string, 40–200 chars, **required** — serves as meta description, OG description *and* RSS description |
| `tags` | `z.array(z.enum([...]))`, min 1 |
| `hero` | optional `image()`, with `heroAlt` required-if-present via `.refine()` |
| `draft` | boolean, default `false` |
| `audio` | optional `[{ src, title, caption? }]` |
| `relatedSpecs` | optional spec ids, cross-validated against `specs.ts` |

**The tag enum is closed**: `firmware`, `dsp`, `pcb`, `panel`, `enclosure`, `bring-up`,
`audio`, `process`. This is the highest-leverage line in the schema — it makes tag routes
generatable without scanning, prevents `Firmware`/`fw`/`firmware` sprawl, and fails the
build on a typo.

Filenames carry a date prefix (`2026-09-13-i2c-speculative-read.mdx`) so the directory
sorts chronologically in an editor; strip it in `getStaticPaths` so URLs are
`/log/i2c-speculative-read/`. Support an optional `slug` override so a title can be reworded
without breaking a shared link.

Drafts are filtered on `import.meta.env.PROD` inside one helper in `src/lib/posts.ts`, used
everywhere — pages never query the collection directly. Drafts are excluded from pages, RSS
**and** sitemap.

### Boards — `src/data/boards.ts`

`/ecosystem/` and every board page render from one typed source, shared with `specs.ts`.
A dropped board is removed in one place and disappears from the catalogue, the nav and the
sitemap together. Given `rodeo-rotary-switch` and `rodeo-encoder-bank` are already retired,
this is not hypothetical.

Each board carries a `rev` (`'A'` today for all of them) and each image carries a
`rev` alongside its `alt`, so the `Figure` component can render the revision caption
**automatically** rather than relying on an author remembering. Per
[WEB_CLAIMS_AUDIT.md](./WEB_CLAIMS_AUDIT.md) every board image must show its revision, and a
rule that depends on discipline will be broken once; a rule that lives in the component
cannot be. `Figure` requires `rev` for anything under `src/assets/hardware/` and fails the
build without it, the same way it already requires `alt`.

Spec claims carry `rev: 'A' | 'B' | 'A+B'` plus an optional `supersededBy`, so a Rev-A fact
with a known Rev-B replacement renders both. See the revision checks in the claims audit.

### RSS

`@astrojs/rss` at `src/pages/rss.xml.ts`, `description: post.data.summary` plus a link — not
full content, which needs the Container API and adds a failure mode. **`site` must be
correct**: Buttondown consumes this feed, so a wrong value means broken emails rather than
just a broken feed.

### OG images

Phase one: three static 1200×630 PNGs in `public/og/`, plus a build-time `getImage()`
variant of any post's `hero` at 1200×630. Zero new dependencies, most of the value. Later:
`satori` + `resvg` for generated title cards. Note OG images must be **absolute URLs** and
**PNG/JPEG** — Slack, iMessage and Twitter will not render AVIF, so these opt out of the
AVIF pipeline.

## Deploy: GitHub Actions, not the Pages git integration

The original case for Actions had two legs. Open-sourcing the repo removed one of them — with
a public repo, granting Cloudflare's GitHub App read access costs nothing. **The remaining
leg still decides it:** Pages supports a root directory and build watch paths, but it
**cannot be told to skip submodules**, so every site build clones libDaisy for no reason.

`.github/workflows/web.yml`:

- Triggers: push to `main` and PRs, both filtered to `paths: ['web/**', '.github/workflows/web.yml']`, plus `workflow_dispatch`.
- `actions/checkout@v4` with `submodules: false` (**already the default** — submodules must
  be opted into, so this is free), `fetch-depth: 1`, and a sparse checkout of `web/`,
  `rodeo-001/docs`, `rodeo-motherboard/docs`, `rodeo-001/firmware`,
  `rodeo-motherboard/firmware`. The docs and firmware paths are needed by the claim
  guardrail and are a few hundred KB. Checkout stays well under 5 MB.
- `actions/setup-node@v4`, `node-version-file: web/.nvmrc`, `cache: npm`,
  `cache-dependency-path: web/package-lock.json`.
- `npm ci` → `npm run check` → `npm run build`, all `working-directory: web`.
  `SITE_ENV=production` on `main`, `preview` otherwise.
- `cloudflare/wrangler-action@v3`, `command: pages deploy dist --project-name=rodeo-web --branch=${{ github.head_ref || github.ref_name }}`,
  `workingDirectory: web`. Secrets: `CLOUDFLARE_API_TOKEN` (scoped to *Cloudflare Pages:
  Edit* on one account) and `CLOUDFLARE_ACCOUNT_ID`. Run from `web/`, `wrangler` picks up
  `web/functions/` automatically — no `@astrojs/cloudflare` adapter, the site stays static.
- `concurrency: { group: web-${{ github.ref }}, cancel-in-progress: true }`.
- A second job `claims`, filtered to `paths: ['rodeo-001/**', 'rodeo-motherboard/**', 'web/src/data/specs.ts']`,
  runs only `npm run claims`. **This is the loop-closer**: a firmware commit that invalidates
  a marketing claim fails CI on that commit, not three weeks later.

Two gotchas worth planning for:

- Create the Pages project as **Direct Upload**. A git-connected project rejects
  `wrangler pages deploy`, and converting afterwards is tedious.
- A required status check with a `paths` filter never runs on unrelated PRs and blocks merge
  forever. Either don't mark it required, or add a no-op `web-gate` job that always succeeds.

Repo size, for the record, is **not** an argument here: the repo is ~2.8 MiB packed, and
`.step` is ASCII that compresses roughly 10:1. The costs were the submodule and the missing
path filter.

## Email capture

**Buttondown + a Pages Function proxy.** Alternatives considered: Mailchimp (heaviest embed,
aggressive branding), Kit/ConvertKit (good, but RSS-driven broadcasts are manual or
higher-tier), Cloudflare D1 (rejected — you would own sending, unsubscribes, suppression,
DKIM and deliverability), and a raw provider embed (works with no code but puts a
third-party origin in the form action and allows no honeypot, Turnstile or redirect
control).

Buttondown wins on one decisive feature: **RSS-to-email**. Point it at
`https://rodeooh.rodeo/rss.xml` and every new log post becomes a newsletter. Writing a post
*is* sending the newsletter, which collapses the two things this site exists for into one
action. Free under 100 subscribers, $9/mo after, clean REST API, built-in double opt-in.

Wiring:

1. `SubscribeForm.astro` — a plain `<form method="POST" action="/subscribe">`. An `email`
   field, an off-screen `b_name` honeypot, a Turnstile widget, a hidden `source`
   (`home` / `footer` / `subscribe-page` / `post:<slug>`), and un-ticked explicit consent
   copy. No pre-checked boxes.
2. `functions/subscribe.ts`:
   - reject non-POST;
   - honeypot non-empty → 303 to `/subscribe/thanks/` and silently drop (don't tell the bot);
   - verify the Turnstile token against `challenges.cloudflare.com/turnstile/v0/siteverify`;
   - if `env.DRY_RUN === '1'`, skip the provider and redirect as success — **this is how
     preview deploys stop polluting the real list**;
   - `POST https://api.buttondown.com/v1/subscribers` with `type: "unactivated"`, which
     triggers the confirmation email. **The double opt-in and the legal record of consent
     live at the provider, not in our code** — that is the point of the design;
   - treat 400 "already subscribed" as success (don't leak list membership);
   - 303 to `/subscribe/thanks/`.
3. `public/_routes.json` = `{"version":1,"include":["/subscribe"],"exclude":["/*"]}`. Without
   it the Function is consulted on **every** request, which hurts the cache story and the
   invocation count.
4. Env vars set **separately for Production and Preview**: `BUTTONDOWN_API_KEY`,
   `TURNSTILE_SECRET`, and `DRY_RUN=1` on Preview only. `PUBLIC_TURNSTILE_SITE_KEY` at build
   time.
5. The form works with JS disabled (real POST + 303). Turnstile is the only third-party
   script on the site.

### Compliance

- **GDPR:** double opt-in gives demonstrable consent with a timestamp stored at Buttondown.
  `/privacy/` must name Buttondown (processor, US — their DPA and SCCs) and Cloudflare
  (hosting + Web Analytics), and state purpose, retention and the right to erasure.
- **Self-host fonts.** Serving Google Fonts from Google's CDN has been found to violate GDPR
  in German case law, and it is trivially avoidable.
- **CAN-SPAM:** a working unsubscribe (Buttondown handles it) **and a valid physical postal
  address** in every email. See the risk note in [WEB_DISCOVERY.md](./WEB_DISCOVERY.md).
- Buttondown's sending domain needs SPF/DKIM records on `rodeooh.rodeo`.

## Design tokens

Two layers, and components reference **only** the second:

1. `src/styles/tokens.css` — the raw measured palette from
   [WEB_BRAND.md](./WEB_BRAND.md).
2. `@theme` in `global.css` — semantic tokens (`--color-field`, `--color-legend`,
   `--color-plating`, `--color-engrave`, the four status colours).

Dark is the only mode (reasoning in [WEB_BRAND.md](./WEB_BRAND.md)). The two-layer structure
is kept anyway: it costs nothing and makes that reversible in one file.

## Images and audio

- `astro:assets` `<Image>`/`<Picture>`, always wrapped in `Figure.astro` so `alt` is
  structurally required. `formats={['avif','webp']}` with a JPEG fallback, explicit `widths`,
  explicit `width`/`height` on every image (CLS insurance), `loading="lazy"` everywhere
  except the hero.
- Commit **web-ready derivatives only**: ≤2500 px long edge, ≤500 KB, sRGB, in
  `src/assets/`. Originals live outside the repo with the location recorded in
  `src/assets/README.md`. No git-lfs — it adds a CI fetch step and complicates the sparse
  checkout for no benefit, since derivatives are small enough not to need it.
- `scripts/check-assets.mjs` fails `npm run check` if anything under `src/assets/` or
  `public/` exceeds 500 KB. **That script is what actually prevents bloat** — policy without
  enforcement doesn't survive a late-night commit.
- Budgets, encoded in `lighthouse-budget.json`: hero ≤250 KB served, inline post images
  ≤120 KB, fonts ≤180 KB, scripts ≤50 KB, total home-page transfer ≤900 KB.
- **Audio:** never commit WAV. MP3 primary (Safari's Opus support is container-limited),
  ≤30 s, ≤600 KB, in `public/audio/` — `astro:assets` doesn't process audio.
- **Video:** not self-hosted. YouTube or Vimeo behind a static poster and a click-to-load
  iframe, so no eager third-party frame and no Lighthouse damage.

## Verification

- `npm run dev` for the walkthrough; `wrangler pages dev` with `DRY_RUN=1` to exercise the
  Function against real routing locally.
- `npm run check` = `astro check` (TS + collection schemas) + `check-claims.mjs` +
  `check-assets.mjs`.
- `npm run build && npm run preview` — the build must be **warning-clean**, not just exit 0.
- **Links:** `lychee` against `dist` with `--offline` on every CI run (deterministic, no
  network flake), plus a weekly scheduled online run for rotted external links. That split
  keeps the link check from becoming the flaky job everyone ignores.
- **HTML:** `npx html-validate 'dist/**/*.html'` — catches the malformed nesting MDX makes
  easy.
- **Lighthouse:** `@lhci/cli` against the preview server. Targets: Performance ≥98 mobile,
  Accessibility 100, Best Practices 100, SEO 100; LCP <1.5 s, CLS <0.01, TBT <50 ms.
- **RSS:** run `/rss.xml` through the W3C Feed Validator once. Buttondown depends on it.
- **Preview indexing — do not skip.** `_headers` cannot condition on hostname, so gate at
  build time: the workflow passes `SITE_ENV`, and `BaseHead.astro` emits
  `<meta name="robots" content="noindex,nofollow">` unless it is `production`. Preview
  deployments indexed under the real brand would be genuinely damaging.
- **Turnstile on previews:** add `*.pages.dev` to the widget's allowed hostnames, or the
  captcha silently fails on every preview and looks like a broken form.
- **One manual test that cannot be automated:** on production, subscribe with a real address,
  confirm the double opt-in email arrives, click through, verify the subscriber shows as
  activated.
