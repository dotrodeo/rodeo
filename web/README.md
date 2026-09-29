# web/ — the Rodeo website

**The site is not built yet.** This directory currently holds the discovery and planning
output only. No `package.json`, no dependencies, no deploy.

## What's here

| Document | What it settles |
|---|---|
| [`docs/WEB_DISCOVERY.md`](docs/WEB_DISCOVERY.md) | What the site is for, who it talks to, the message, the information architecture, content inventory, risks |
| [`docs/WEB_BRAND.md`](docs/WEB_BRAND.md) | The visual language — palette, typefaces, graphic devices, wordmark. All derived from the motherboard PCB |
| [`docs/WEB_ARCHITECTURE.md`](docs/WEB_ARCHITECTURE.md) | Stack, directory layout, content model, deploy pipeline, email capture, verification |
| [`docs/WEB_CLAIMS_AUDIT.md`](docs/WEB_CLAIMS_AUDIT.md) | What we're allowed to claim today, with evidence; what must never appear; the accuracy guardrail to build |
| [`docs/WEB_ROADMAP.md`](docs/WEB_ROADMAP.md) | Phases, estimates, and the open-decisions checklist |

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

1. Read [`docs/WEB_ROADMAP.md`](docs/WEB_ROADMAP.md) and clear phase 0.
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
  ../rodeo-motherboard/kicad/rodeo-motherboard.kicad_pcb
```

Flat layer art for graphic use:

```bash
/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli pcb export svg \
  -o mobo-silk.svg --layers 'F.Silkscreen,Edge.Cuts' \
  --page-size-mode 2 --exclude-drawing-sheet --black-and-white \
  ../rodeo-motherboard/kicad/rodeo-motherboard.kicad_pcb
```

Commit only web-ready derivatives: ≤2500 px long edge, ≤500 KB, sRGB. Originals stay
outside the repo.
