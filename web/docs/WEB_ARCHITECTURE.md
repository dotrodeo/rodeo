---
title: "Rodeo Web — Technical Architecture"
tags: [rodeo, web, astro, cloudflare, decision-record]
status: active
created: 2026-09-17
updated: 2026-10-15
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
| Hosting | **Cloudflare Workers** with static assets (`web/wrangler.jsonc`) |
| Deploy | **GitHub Actions** → `wrangler deploy` |
| Dynamic | One route: `POST /api/subscribe` in `web/worker/index.ts` |
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
├── wrangler.jsonc
├── worker/
│   ├── index.ts                 # the waitlist handler; everything else -> ASSETS
│   └── tsconfig.json            # Workers globals, kept out of the Astro graph
├── public/
│   ├── _headers                 # security headers, cache-control
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

## The SDK docs: Starlight at /sdk/

`rodeo-sdk` documentation runs on **Starlight**, Astro's own docs framework, rather
than being hand-built. It brings the sidebar, the table of contents, prev/next,
code highlighting and Pagefind search for free, and because it is an Astro
integration it lives in this project and ships in the same build and deploy.

Three things had to be worked out, and are worth not rediscovering:

- **Version pinning.** Current Starlight (0.42) peer-depends on Astro ^7; this site is
  on Astro 5. **0.36.3** is the newest line on Astro ^5, paired with
  `@astrojs/starlight-tailwind@4.0.1` (Tailwind ^4). Do not bump Starlight without
  bumping Astro.
- **No `routeBasePath` in 0.36.** The usual way to mount docs at a subpath does not
  exist in this version. Routes come from the directory layout instead, so the pages
  live under `src/content/docs/sdk/` and land on `/sdk/...`. `disable404Route: true`
  stops Starlight claiming the site's own 404.
- **Theming, not overriding.** Starlight is driven by CSS custom properties, so
  `src/styles/starlight.css` remaps its variables onto the tokens rather than
  fighting its CSS. Light and dark are mapped to the same values so a stray
  `data-theme` cannot produce white-on-white, and `ThemeSelect` is overridden with an
  empty component.

`SiteTitle` is also overridden. Starlight's header otherwise links only to `/sdk/`,
which leaves a visitor who lands on a docs page with no route back into the site.

**One accepted build warning.** Pagefind spawns a child process and Node emits
`DEP0190` about it. It comes from inside Starlight, not from this code, and cannot be
fixed here — so the build is no longer strictly warning-free, and that one warning is
the only permitted exception.

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

## Deploy: a Worker with static assets, from GitHub Actions

**Revised 2026-10-14.** This was planned for Cloudflare Pages. Cloudflare has
since moved on: the dashboard's create flow offers Workers and relegates Pages
to a "Need to use the legacy Pages workflow?" link, and the Pages docs carry a
migration banner. A site starting now belongs on Workers.

What that changes in practice:

- `wrangler deploy`, not `wrangler pages deploy`.
- Config lives in `web/wrangler.jsonc` rather than in command flags.
- **No project to pre-create.** The Worker is created by the first deploy.
- The API token needs **Workers Scripts · Edit**, not Pages · Edit.
- PR previews come from `wrangler versions upload` with `preview_urls: true`,
  which uploads a version and gives it a URL without promoting it.
- `_headers` and `_redirects` still work — supported natively on Workers — so
  `web/public/_headers` carries over unchanged.
- The Worker has `main` and an `ASSETS` binding. Routing stays **asset-first**:
  a request matching a built file is served by the asset layer and the script
  never runs, so `_headers` still applies to every page. Only unmatched paths
  reach the script.
- `not_found_handling: "404-page"` serves Astro's 404 rather than Cloudflare's.

Still deploying from Actions rather than Workers Builds, for the original
reason: the checkout stays sparse and submodule-free. The repo carries libDaisy
and DaisySP and the site has no use for either.

`actions/checkout@v4` with `submodules: false` (already the default) plus a
sparse checkout of `web/` and the handful of hardware paths the claim guardrail
reads evidence from — roughly 4 MB. `npm run check` gates the deploy, so copy
that has outrun the hardware fails before it ships.

`SITE_ENV` is set per branch and drives the `noindex` tag, so preview
deployments cannot be indexed under the real brand. Astro does not expose
arbitrary env vars the way it does `PUBLIC_` ones, so this was verified
empirically rather than assumed.

One gotcha worth keeping: a required status check with a `paths` filter never
runs on unrelated PRs, and GitHub treats "never ran" as "not passed". Either
don't mark these required, or add an always-passing gate job.

## Email capture

**Built 2026-10-14.** The plan below described a Pages Function; it is a Worker
route now, and two details changed in the building. The rest held.

**Buttondown + a Worker route.** Alternatives considered: Mailchimp (heaviest embed,
aggressive branding), Kit/ConvertKit (good, but RSS-driven broadcasts are manual or
higher-tier), Cloudflare D1 (rejected — you would own sending, unsubscribes, suppression,
DKIM and deliverability), and a raw provider embed (works with no code but puts a
third-party origin in the form action and allows no honeypot, Turnstile or redirect
control).

Buttondown wins on one decisive feature: **RSS-to-email**. Point it at
`https://rodeooh.rodeo/rss.xml` and every new log post becomes a newsletter. Writing a post
*is* sending the newsletter, which collapses the two things this site exists for into one
action. Free under 100 subscribers, $9/mo after, clean REST API, built-in double opt-in.

Wiring, as built:

1. **`src/components/SubscribeForm.astro`** — `<form method="post"
   action="/api/subscribe">`. The path is under `/api/` because that prefix is
   routed Worker-first — see below. An `email` field, an off-screen `b_name`
   honeypot, a hidden `source` (`home` / `ecosystem` / `log-post` / …), the
   Turnstile widget, and a consent line. One component, used by `CtaBand` and
   by `/subscribe/`, so the markup the Worker checks cannot drift between them.
2. **`worker/index.ts`**, in order: reject non-POST with 405; honeypot
   non-empty → 303 to `/subscribe/thanks/` and silently drop; validate the
   address; verify Turnstile; if `DRY_RUN === '1'` stop and redirect as
   success; otherwise `POST https://api.buttondown.com/v1/subscribers` with
   `type: "unactivated"`, which is what triggers the confirmation email.
   **The double opt-in and the dated record of consent live at the provider**
   — that is the design, not an omission. Then 303 to `/subscribe/thanks/`.
3. **Errors** 303 back to `/subscribe/?error=<code>`, and an inline script on
   that page turns the code into a sentence. The address is never put in the
   query string: it would land in a URL, a referer header and every
   intermediate log for no gain, and the subscriber can retype it. Without JS
   the form simply reappears without the message.
4. **Env**, set three different ways on purpose:
   - `BUTTONDOWN_API_KEY`, `TURNSTILE_SECRET` — Worker **secrets**
     (`wrangler secret put`). `wrangler deploy` never deletes secrets, so this
     is one-time setup and they never touch CI.
   - `DRY_RUN` — passed as `--var DRY_RUN:1` on `versions upload` only, so it
     exists on previews and nowhere else. It is deliberately *not* in
     `wrangler.jsonc`: `wrangler deploy` clears vars absent from the config,
     which is what guarantees production has none.
   - `PUBLIC_TURNSTILE_SITE_KEY` — a build-time GitHub **variable**, not a
     secret; Turnstile reads it off the page.
5. **The form needs JavaScript, and that is new.** The POST and the 303 are
   real, so the mechanics are unchanged — but Turnstile cannot produce a token
   without JS, so a JS-less submission is rejected with `error=captcha`. This
   is inherent to any captcha, not a flaw in the wiring; the choice was
   Turnstile or an open form, and Turnstile won. Before the keys existed the
   no-JS path did work, so the earlier claim here was true when written and
   stopped being true on 2026-10-15. Turnstile is the only third-party script
   on the site.
6. **Submission is held until the token exists.** A fast click would otherwise
   post before Turnstile finishes and bounce the subscriber to an error page
   for doing nothing wrong. The inline script in `SubscribeForm.astro` catches
   `submit`, waits up to 8s polling the hidden field, and re-submits by itself
   the moment the token lands — so the usual case costs one line of status
   text and no lost typing. After 8s it says the check has not finished and
   points at the widget, which is the right advice for both an untouched
   interactive challenge and a blocked Turnstile.

   **Where that script lives matters.** It is at the top level of the
   component, *not* inside a `{siteKey && …}` expression. Astro parses an
   element's children inside an expression as JSX, so a template literal there
   is emitted as literal `` {`…`} `` text: the whole function ships as an
   inert string, greps clean, and never runs. It happened once. The script
   guards itself instead — no widget on the form, nothing happens.

### Routing: the part that cost the most, and nearly shipped broken

**Asset-first routing will intercept a form POST.** The asset layer checks
`Sec-Fetch-Mode: navigate` — which every browser form submission sends and
`curl` does not — and for a navigation to a path it has no asset for it tries
to serve `not_found_handling`'s 404 page. A POST cannot be answered with an
asset, so it returns **405 and the Worker is never invoked**.

This is a nasty failure mode because it is invisible from a terminal. The
endpoint returned a correct 303 to every `curl` thrown at it and 405 to the
first real browser submission. It was caught by driving the actual form, not
by testing the endpoint.

The fix is `"run_worker_first": ["/api/*"]` in the `assets` block, which takes
the asset layer out of the decision for that prefix. Scoped to `/api/*` rather
than `true` on purpose: Worker-generated responses do not get `_headers` rules
applied, so every page that is a real page stays asset-first and keeps its
security headers. Verified after the change that `/` still carries
`X-Frame-Options` and that `/nope/` still renders Astro's own 404.

**`_routes.json` is gone.** That was the Pages mechanism for keeping a Function
off every request. On Workers it does not exist, and asset-first routing plus a
scoped `run_worker_first` does the same job.

### The key-ordering trap

`TURNSTILE_SECRET` applies to the running Worker the moment it is set.
`PUBLIC_TURNSTILE_SITE_KEY` is compiled into the HTML and needs a deploy. Set
the secret first and the live form goes down: the Worker demands a token the
deployed page cannot produce, and every signup fails `error=captcha`. Set the
variable and deploy first, then the secret.

### Degraded modes, both deliberate

- **No Turnstile keys** → no widget is rendered and the Worker logs
  `TURNSTILE_SECRET unset — captcha not verified` on every signup, leaving the
  honeypot and Buttondown's own IP check. Acceptable for a pre-launch waitlist,
  and it is why the key is worth adding before the campaign.
- **Buttondown 400** is treated as success. It is the collision and suppression
  family, the address was already validated, and the one thing worse than a
  confusing error is confirming to a stranger that an address is on the list.
  401/403/429/5xx are surfaced as `error=server`. Nothing logs the address.

### Compliance

- **GDPR:** double opt-in gives demonstrable consent with a timestamp stored at Buttondown.
  **`/privacy/` exists as of 2026-10-15** and names Buttondown (processor, US — DPA and
  SCCs) and Cloudflare (hosting, operational logs, Turnstile), with purpose, retention and
  the right to erasure. It was written against the code rather than from a template: the
  third-party origins in the built output, the absence of analytics, the 28 self-hosted
  font files, the single `starlight-theme` localStorage key, and exactly what
  `worker/index.ts` sends to Buttondown versus logs locally. **If the signup path changes,
  that page changes with it** — it is the one page where going stale is a legal problem
  rather than an embarrassment. Note Web Analytics is *not* deployed, so the page says so;
  adding it later means editing `/privacy/` in the same commit.
- **Self-host fonts.** Serving Google Fonts from Google's CDN has been found to violate GDPR
  in German case law, and it is trivially avoidable.
- **CAN-SPAM:** a working unsubscribe (Buttondown handles it) **and a valid physical postal
  address** in every email. See the risk note in [WEB_DISCOVERY.md](https://github.com/dotrodeo/rodeo-internal/blob/main/web/docs/WEB_DISCOVERY.md) (`rodeo-internal`).
- Buttondown's sending domain needs SPF/DKIM records on `rodeooh.rodeo`.
- **The handler sends the subscriber's IP to Buttondown** as `ip_address`, which is their
  documented spam check and matters more while Turnstile is unconfigured. An IP is personal
  data, so `/privacy/` has to say so — it is the one disclosure the signup path adds beyond
  the address itself. Nothing else leaves the account: `source` is logged, never sent.

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

- `npm run dev` for the walkthrough; `npm run worker` (`astro build && wrangler dev`) with
  `DRY_RUN=1` in `.dev.vars` to exercise the handler against the real asset routing locally.
  Turnstile's published test secrets drive the pass and fail branches:
  `1x0000000000000000000000000000000AA` always passes,
  `2x0000000000000000000000000000000AA` always fails.
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
- **Turnstile on previews:** add `*.workers.dev` to the widget's allowed hostnames, or the
  captcha silently fails on every preview and looks like a broken form.
- **One manual test that cannot be automated — run and passed 2026-10-15.** A real address
  subscribed through the live form, the double opt-in mail arrived, the link was clicked, and
  the subscriber showed as activated. Re-run it after any change to the handler or the keys;
  nothing local exercises the provider leg.
