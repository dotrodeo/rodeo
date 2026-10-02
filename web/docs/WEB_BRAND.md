---
title: "Rodeo Web — Brand & Design Tokens"
tags: [rodeo, web, brand, design-tokens, typography]
status: active
created: 2026-09-17
updated: 2026-09-17
---

# Rodeo Web — Brand & Design Tokens

**Owner:** Derek. Source of truth for the website's visual language. The Tailwind theme
layer in `web/src/styles/` is a **transcription of this file**, not an independent decision.

> Design canvas (artboards, wordmark studies, pages with the system applied):
> <https://claude.ai/artifact/Sc1twbmgcvB1wFd1vCeQis>

## The governing principle: the board is the brand

Rodeo's visual identity was not designed for the website. It already existed, on the
silkscreen of `rodeo-motherboard`. The palette below is **sampled from a render of the
actual board**, and the typefaces are **the ones specified in the board's own KiCad text
objects**. Nothing here was invented for the web.

This matters beyond convenience. A platform sold to builders is credible in proportion to
how much of it is verifiable, and a brand lifted off the product is verifiable by opening
the board file. It also means the physical object and the website cannot drift apart: change
the soldermask colour and the site is wrong, which is the correct direction for that
dependency to run.

### How the palette was derived

`kicad-cli pcb render --quality high` on
`rodeo-motherboard/kicad/rodeo-motherboard.kicad_pcb`, then a pixel histogram over the
result. Reproduce with:

```
/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli pcb render \
  -o mobo-top.png --width 2400 --height 1800 --side top \
  --quality high --background opaque \
  rodeo-motherboard/kicad/rodeo-motherboard.kicad_pcb
```

The board declares `(layer "F.Mask" (color "Black"))` in its stackup, four copper layers,
and `(copper_finish "None")`.

## Palette

Dark is the **only** mode — see "Why there is no light mode" below.

| Token | Hex | What it is on the board | Contrast on field |
|---|---|---|---|
| `--color-field` | `#141416` | Black soldermask. The ground. | — |
| `--color-field-raised` | `#1C1C20` | One step up: cards, wells, code blocks. No border needed. | — |
| `--color-legend` | `#FEFEFE` | Silkscreen white. Headings, labels, knockout chips. | **18.2:1** |
| `--color-legend-prose` | `#E4E4E2` | Softened white for long body copy (see note). | **14.4:1** |
| `--color-hardware` | `#B6B6B6` | Connector shells and tin pads. Secondary text, captions. | **9.1:1** |
| `--color-plating` | `#F0D81E` | Gold plated through-holes. **The only accent.** | **12.7:1** |
| `--color-trace` | `#2A1E18` | Copper seen through black mask. **Hairlines and rules only.** | 1.2:1 — never text |
| `--color-copper` | `#7E604E` | Copper mid-tone. Decorative, or text at 24 px+. | 3.2:1 — large only |

Status colours reuse the palette rather than adding to it:

| Token | Value | Reads as |
|---|---|---|
| `--color-status-shipped` | `--color-plating` fill, `--color-field` text | verified on hardware |
| `--color-status-progress` | `--color-legend` 1px outline, `--color-legend` text | in bring-up |
| `--color-status-planned` | `--color-hardware` 1px outline, `--color-hardware` text | designed, not built |
| `--color-status-retired` | `--color-hardware` text, struck through | dropped |

### Rules that are not negotiable

1. **`#F0D81E` is never used on a light ground.** Gold on white is **1.4:1** — it fails
   every threshold. On the field it is 12.7:1. Inverted (black text on a gold fill) it is
   also 12.7:1, which is why the gold chip and the gold button both work.
2. **`--color-trace` never carries text.** It exists because the board's traces read as
   near-black-warm hairlines, and that is the only job it has.
3. **One accent.** The board has exactly one non-monochrome colour and so does the site.
   Resist adding a second; a cool companion was considered and rejected as un-earned.
4. **Long prose uses `--color-legend-prose`, not `--color-legend`.** Pure silkscreen white
   at 18.2:1 is right for labels and headings, and causes visible halation over a paragraph
   on a near-black ground. Verified on the log-post artboard. Headings stay `--color-legend`.

## Typography

All three faces are already specified in the boards' KiCad text objects and all three are
freely available. Self-host them (`@fontsource-variable/barlow`,
`@fontsource/ibm-plex-mono`) — a performance decision and a GDPR one.

| Role | Face | Weights | Where it comes from |
|---|---|---|---|
| Display / wordmark | **Barlow Black** (Barlow 900) | 900 | `(face "Barlow Black")` — the `RODEO` wordmark and every section label on all six boards |
| Body / UI | **Barlow** | 400, 500, 600 | `(face "Barlow")` — motherboard and hub secondary labels |
| Mono | **IBM Plex Mono** | 400, 500 | `(face "IBM Plex Mono")` — designators on the potentiometer and rotary-switch boards |

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
   panel aesthetic (`rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md:474-491`) is **that
   instrument's** flavour, deliberately not the platform brand, and is still marked
   "proposed, for Derek's nod" with the colour pairing open at `:489`.
4. **Trademark search on "Rodeo"** — still outstanding, still worth 20 minutes. An open
   licence on the designs gives away the files, not the name, and the name is the part that
   is kept.
