---
title: "Rodeo Web — Brand & Design Tokens"
tags: [rodeo, web, brand, design-tokens, typography]
status: active
created: 2026-09-17
updated: 2026-10-05
---

# Rodeo Web — Brand & Design Tokens

**Owner:** Derek. Source of truth for the website's visual language. The Tailwind theme
layer in `web/src/styles/` is a **transcription of this file**, not an independent decision.

> Design canvas (artboards, wordmark studies, pages with the system applied):
> <https://claude.ai/artifact/Sc1twbmgcvB1wFd1vCeQis>

## The governing principle: the board is the brand

Rodeo's visual identity was not designed for the website. It already existed, on the
silkscreen of `rodeo-motherboard`: black soldermask, white legend, plated yellow, heavy
caps, mono designators. Three of the four typefaces are still **the ones specified in the
board's own KiCad text objects**, and the graphic devices below are lifted off the
silkscreen unchanged.

This matters beyond convenience. A platform sold to builders is credible in proportion to
how much of it is verifiable, and a brand taken off the product is verifiable by opening the
board file.

**What changed on 2026-10-05.** The exact colour values are no longer measured — Derek chose
round hex codes, and two accents were added that are not on any board. So the accurate claim
is now: *the identity derives from the board; the values are chosen for the screen.* The
earlier, stronger claim ("every value is sampled") is retired. It was a good story, and
repeating it once it stopped being true would cost more than it bought — a reader can open
the KiCad file and check.

### How the original palette was derived (historical)

`kicad-cli pcb render --quality high` on
`rev-a/rodeo-motherboard/kicad/rodeo-motherboard.kicad_pcb`, then a pixel histogram over the
result. Reproduce with:

```
tools/kicad/bin/kicad-cli pcb render \
  -o mobo-top.png --width 2400 --height 1800 --side top \
  --quality high --background opaque \
  rev-a/rodeo-motherboard/kicad/rodeo-motherboard.kicad_pcb
```

The board declares `(layer "F.Mask" (color "Black"))` in its stackup, four copper layers,
and `(copper_finish "None")`.

## Palette

**Revised 2026-10-05.** The values were originally sampled from a render of the motherboard.
Derek chose round, legible hex codes over measured ones, and added two accents. The board is
still where the identity *comes from* — black mask, white legend, plated yellow — but the
numbers are now chosen for the screen. Do not reintroduce the "every value is measured"
claim; it is no longer true, and the honesty of these docs is worth more than the story.

Dark is the **only** mode — see "Why there is no light mode" below.

| Token | Hex | Job | Contrast on field |
|---|---|---|---|
| `--color-field` | `#111111` | The page | — |
| `--color-field-raised` | `#1C1C1C` | Cards, wells, code blocks | — |
| `--color-rule` | `#2E2E2E` | Hairlines | — |
| `--color-legend` | `#FFFFFF` | Headings, labels, knockout chips | **18.9:1** |
| `--color-legend-prose` | `#E0E0E0` | Long-form body | **14.3:1** |
| `--color-hardware` | `#B0B0B0` | Captions, secondary text | **8.7:1** |
| `--color-yellow` | `#FFCC00` | Primary action, `shipped` status | **12.5:1** |
| `--color-indigo` | `#6655FF` | Section bands — **surface only** | 3.9:1 |
| `--color-indigo-text` | `#8888FF` | Eyebrows and labels on dark | **6.3:1** |
| `--color-cyan` | `#00CCFF` | Code, SDK, technical reference | **10:1** |

### Why three accents

The earlier rule was one accent, on the reasoning that the board has exactly one
non-monochrome colour. That was a good argument for a short page and a bad one for a long
one: a dark page with a single accent has no way to signal a new section except another
yellow band, and two in a row is too much. Indigo earns its place as a sectioning device.

Cyan has a narrower job — code, the SDK, inline technical reference — and should not be used
decoratively. If it ends up unused, remove it rather than finding work for it.

### Rules that are not negotiable

1. **`--color-indigo` never carries text.** At 3.9:1 it fails as a foreground on the field.
   This is not theoretical: the eyebrow label in the original mockup did exactly this.
   `--color-indigo-text` exists for that job. White on indigo is 5.5:1 and fine.
2. **`#FFCC00` is never used on a light ground.** On the field it is 12.5:1; inverted, with
   black text on a yellow fill, also 12.5:1. On white it is around 1.4:1 and unusable. On
   indigo it is 3.2:1 — acceptable for display type at 24px+, never for body.
3. **Long prose uses `--color-legend-prose`, not `--color-legend`.** Pure white at 18.9:1 is
   right for labels and headings and causes visible halation over a paragraph on near-black.

## Typography

All three faces are already specified in the boards' KiCad text objects and all three are
freely available. Self-host them (`@fontsource-variable/barlow`,
`@fontsource/ibm-plex-mono`) — a performance decision and a GDPR one.

| Role | Face | Where it comes from |
|---|---|---|
| Display / wordmark | **Barlow Black** (900) | `(face "Barlow Black")` — the `RODEO` wordmark and every section label on all six boards |
| UI / short copy | **Barlow** 400–600 | `(face "Barlow")` — motherboard and hub secondary labels |
| Technical | **IBM Plex Mono** 400/500 | `(face "IBM Plex Mono")` — designators on the satellite boards |
| Long-form reading | **Source Serif 4** | Not from the board. Chosen 2026-10-05 from three candidates, for build-log posts |

**Source Serif 4, chosen 2026-10-05** from three candidates rendered side by side with real
copy. It is the sturdiest of the three in reverse — the stems hold where lighter serifs thin
out on black. Newsreader and Literata have been uninstalled.

**Why a third family at all.** The mockup set body copy in mono, which is distinctive and
right for short copy, but mono is measurably slower to read at paragraph length and eats the
measure on a phone — the hero paragraph already runs ten lines at 375px. The rule:
**mono up to about three lines, the reading face beyond.** `global.css` provides `.copy-tech`
and `.copy-read` for exactly this split.

Setting conventions, taken from how the board sets type:

- Display is **all caps, tight**: `letter-spacing: -0.02em`, `line-height: 0.9`. The board's
  `RODEO` is Barlow Black at 6 mm with no tracking added.
- Knockout chips are **caps, loose**: `letter-spacing: 0.1em`, weight 900, 11–13 px.
- Mono carries every **designator, register address, pin name, dimension and part number**.
  `J42`, `0x30`, `ADS114S08`, `25 × 30 mm`. This is load-bearing: the mono face is what
  makes a spec table read as documentation rather than marketing.
- Body copy is Barlow 400 at 16–18 px, `line-height: 1.5`–`1.65`.

Font budget: ≤180 KB total, latin subset, `font-display: swap`, preload the display weight
and one body weight only.

## Graphic devices

Three devices, all lifted directly off the silkscreen. Together they are the entire
component vocabulary — there is no fourth idea to invent.

### 1. The knockout chip

A white box with black caps inside it. On the board it labels `ANALOG BANK 1`, `I2C`, `SPI`,
`UART`, `LINE OUT`, `DAISY SEED3`. On the site it is the section header, the nav's active
state, and — filled gold instead of white — the status badge.

```
background: var(--color-legend); color: var(--color-field);
font-weight: 900; font-size: 12px; letter-spacing: 0.1em; padding: 4px 9px;
```

### 2. The outline group

A 1.5 px `--color-legend` rule around a functional region, with a knockout chip
**straddling the bottom edge**. The board groups its 24 analog jacks and 12 digital jacks
this way. On the site it wraps the board catalogue, the register map, and any spec block
that deserves to read as a unit.

### 3. Designator micro-labels

Small mono text in `--color-hardware` beside things: `J42`, `0x10`, `Rev-A`. The board is
covered in them. They give a page technical texture at almost no visual cost and they are
never decorative — a designator on the site always names something real.

Also available, not yet used: `kicad-cli pcb export svg --layers 'F.Cu,Edge.Cuts'` produces
the board's copper geometry as flat line art. The 40-connector fan-out grid is a strong
candidate for a background or section divider. Exports are in the scratchpad; none is
committed yet.

## Wordmark

The board lockup **is** the wordmark: `RODEO` in Barlow Black with a knockout bar beneath
it. The bar is a **descriptor slot** — `SYNTHESIZER MOTHERBOARD` on the board,
`OPEN SYNTH PLATFORM` in site nav — and swaps by context without touching the mark.

- Horizontal variant for nav: mark at 34 px, chip alongside.
- Inverted variant (black on white) for print and panel engraving.
- Small mark: `R` in Barlow Black, black on a `--color-plating` square. Works down to 16 px;
  the plated-hole gold is the most recognisable thing about the board at favicon size.

### Naming — settled 2026-09-17

**The product is Rodeo.** The domain `rodeooh.rodeo` is a play on "Romeo, oh Romeo" — a wink
in the address bar, not a second brand. No surface says "Rodeooh", so the domain does no
brand work and the wordmark carries all of it. Practical consequences: canonical URLs, OG
cards and Buttondown's SPF/DKIM records all sit on `rodeooh.rodeo`, while every piece of
copy, the `<title>` suffix and the wordmark say Rodeo.

## Why there is no light mode

Decided 2026-09-17, and it is a decision rather than a deferral.

1. **The brand is a black object.** The identity is soldermask, silkscreen and plating. A
   light inversion has no referent — there is no white Rodeo board to be faithful to.
2. **The accent does not survive it.** `#F0D81E` on white is 1.4:1. A light mode needs a
   different accent, i.e. a second brand.
3. **The photography is dark.** Every board image is a black board. On a light page they
   float; on the field they sit flush.
4. **One person maintains this.** A second theme doubles design QA on every component,
   forever, for no audience that has asked for it.

Mitigations so "dark only" reads as intent rather than oversight: `<meta name="color-scheme"
content="dark">`, and `--color-field` is a warm-cool near-black (`#141416`, the measured
mask value) rather than `#000`.

The two-layer token structure (raw palette → semantic tokens that components reference
exclusively) is kept anyway. It is what makes this reversible in one file if the decision
ever changes, and it costs nothing now.

## Open items

1. **Board family mask inconsistency.** `rodeo-button` and `rodeo-encoder` declare **no**
   `F.Mask` colour in their stackup; the other four declare `Black`. Left alone, the family
   arrives looking like two different products. One-line fix per board in KiCad — a hardware
   task with brand consequences, tracked here because this doc is where it was found.
2. ~~**`JLCJLCJLCJLC` is on the motherboard's front silkscreen.**~~ **Corrected 2026-10-01 —
   this was wrong.** The field is on `B.SilkS` (`rodeo-motherboard.kicad_pcb`), and the
   photograph of the board's back confirms it: a small `JLCJLCJLCJLC` above the `RODEO`
   wordmark. That is already discreet. No action needed, and nothing was ever wrong on the
   front.
3. **No `rodeo-001` imagery exists.** The reference-build showcase page needs either a
   render of the instance KiCad or photographs of an assembled unit. Its Cassette Futurism
   panel aesthetic (`instruments/rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md:474-491`) is **that
   instrument's** flavour, deliberately not the platform brand, and is still marked
   "proposed, for Derek's nod" with the colour pairing open at `:489`.
4. **Trademark search on "Rodeo"** — still outstanding, still worth 20 minutes. An open
   licence on the designs gives away the files, not the name, and the name is the part that
   is kept.
