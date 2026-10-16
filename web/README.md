# web/ — the Rodeo website

**The site is not built yet.** This directory currently holds the discovery and planning
output only. No `package.json`, no dependencies, no deploy.

## What's here

The brief and the roadmap hold campaign planning, so they live in the private
`dotrodeo/rodeo-internal` repository; links to them only work for the team.

| Document | What it settles |
|---|---|
| [`WEB_DISCOVERY.md`](https://github.com/dotrodeo/rodeo-internal/blob/main/web/docs/WEB_DISCOVERY.md) (in `rodeo-internal`) | What the site is for, who it talks to, the message, the information architecture, content inventory, risks |
| [`docs/WEB_BRAND.md`](docs/WEB_BRAND.md) | The visual language — palette, typefaces, graphic devices, wordmark. All derived from the motherboard PCB |
| [`docs/WEB_ARCHITECTURE.md`](docs/WEB_ARCHITECTURE.md) | Stack, directory layout, content model, deploy pipeline, email capture, verification |
| [`docs/WEB_CLAIMS_AUDIT.md`](docs/WEB_CLAIMS_AUDIT.md) | What we're allowed to claim today, with evidence; what must never appear; the accuracy guardrail to build |
| [`WEB_ROADMAP.md`](https://github.com/dotrodeo/rodeo-internal/blob/main/web/docs/WEB_ROADMAP.md) (in `rodeo-internal`) | Phases, estimates, and the open-decisions checklist |

**Design canvas** (artboards for the foundations, wordmark, home, boards, board detail and a
log post): <https://claude.ai/artifact/Sc1twbmgcvB1wFd1vCeQis>

## The one-paragraph version

Rodeo's product is the **platform** — `rodeo-motherboard` plus the `rodeo-*` satellite
boards. `rodeo-001` is a reference build, not the thing being sold. The site is
**devlog-first**, aimed at builders, ahead of a Kickstarter. It's an Astro 5 + Tailwind v4
static site on Cloudflare Pages, deployed from GitHub Actions. The whole ecosystem is open
source, hardware included. The visual identity is sampled from the board itself: black
soldermask `#141416`, silkscreen white `#FEFEFE`, plated-hole gold `#F0D81E`, set in Barlow
Black and IBM Plex Mono — the faces already specified in the boards' KiCad text objects.
Dark mode only.

## Before writing any code

1. Read [`WEB_ROADMAP.md`](https://github.com/dotrodeo/rodeo-internal/blob/main/web/docs/WEB_ROADMAP.md) in `rodeo-internal` and clear phase 0.
2. Read [`docs/WEB_CLAIMS_AUDIT.md`](docs/WEB_CLAIMS_AUDIT.md). Product copy is not
   freehand: every spec statement renders from `src/data/specs.ts` and carries a status.
3. Transcribe [`docs/WEB_BRAND.md`](docs/WEB_BRAND.md) into the token layer rather than
   re-deciding it.

## Regenerating board imagery

No raster images are committed yet. Renders come from KiCad and are reproducible, so
regenerate them when a board changes instead of maintaining an image library:

```bash
/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli pcb render \
  -o mobo-top.png --width 2400 --height 1800 --side top \
  --quality high --background opaque \
  ../rev-a/rodeo-motherboard/kicad/rodeo-motherboard.kicad_pcb
```

Flat layer art for graphic use:

```bash
/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli pcb export svg \
  -o mobo-silk.svg --layers 'F.Silkscreen,Edge.Cuts' \
  --page-size-mode 2 --exclude-drawing-sheet --black-and-white \
  ../rev-a/rodeo-motherboard/kicad/rodeo-motherboard.kicad_pcb
```

Commit only web-ready derivatives: ≤2500 px long edge, ≤500 KB, sRGB. Originals stay
outside the repo.

---

## Running it

```bash
cd web
npm install
npm run dev      # http://localhost:4321
npm run check    # astro check + claim citations + asset budgets
npm run build
```

`npm run check` must be clean before a commit. It runs three things:

- `astro check` — types and content-collection schemas
- `scripts/check-claims.mjs` — every `evidence` citation in `src/data/boards.ts`
  resolves to a real, non-blank line, and no `shipped` claim sits within three
  lines of a `TODO`. See [docs/WEB_CLAIMS_AUDIT.md](docs/WEB_CLAIMS_AUDIT.md).
- `scripts/check-assets.mjs` — nothing committed under `src/assets/` or
  `public/` exceeds 500 KB.

## What exists so far

Phase 1 and the board catalogue: design tokens, layout shell, `/`, `/ecosystem/`,
a page per board, `/style-guide` and `/404`. Not yet built: the build log, `/docs/`,
`/prototype/`, the subscribe function and the deploy workflow — see
[WEB_ROADMAP.md](https://github.com/dotrodeo/rodeo-internal/blob/main/web/docs/WEB_ROADMAP.md) in `rodeo-internal`.

### Imagery

**Photographs where they exist, renders only for boards not yet shot.** The distinction is
rendered into the caption by `BoardFigure`, which refuses to render without both `media`
(`photo` | `render`) and `rev` — a render passed off as a photo is the same class of
dishonesty as an unlabelled board revision.

Photographed so far: `rodeo-motherboard` (front, back, two detail shots) and
`rodeo-encoder-hub` (front, back). Still renders: potentiometer, encoder, button.

Importing new photos — re-encoding also strips any EXIF, which phone photos can carry:

```bash
npm run photos -- <src-dir>   # mapping lives in scripts/prepare-photos.mjs
```

Renders are regenerated from KiCad rather than stored as precious artwork:

```bash
kicad-cli pcb render -o out/<board>.png --width 1800 --height 1400 \
  --side top --quality high --background opaque <board>.kicad_pcb
npm run renders out          # re-encode into src/assets/hardware within budget
```
