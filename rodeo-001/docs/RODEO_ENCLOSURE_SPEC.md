---
title: "Rodeo Enclosure & Panel Spec"
tags: [rodeo, enclosure, panel-layout, industrial-design, mechanical]
status: draft
created: 2026-08-08
---

# Rodeo Enclosure & Panel Spec

**Owner:** Honey (industrial/mechanical design). This is the mechanical counterpart to
Bumble's [RODEO_CONTROL_MAP.md](./RODEO_CONTROL_MAP.md). The control
map is the **contract**: it defines *what* must fit (control types, footprints, edge-vs-face,
grouping). This doc defines *how* it's housed and laid out. Keep the two in sync — any
change to control count/type here or there must be reflected in both.

> Status: DRAFT — awaiting control wishlist + motherboard reference before real layout.

## Governing principle inherited from the control map: NO HIDDEN CONTROLS

Every player-adjustable parameter gets a dedicated physical control on the face. Mechanical
consequence: **panel area is the binding constraint on the whole instrument's size.** The
enclosure is sized bottom-up from control count × footprint × ergonomic spacing, not chosen
first and filled after. The OLED is feedback-only and does not reduce control area.

## Known fixed constraints (as of 2026-08-08)

| Item | Value | Source / confidence |
|------|-------|---------------------|
| Compute board | Daisy Seed on Rodeo Synthesizer Motherboard | Derek |
| Daisy Seed footprint | ~51 × 18 mm, 0.1" dual-row castellated headers, USB on one short edge | Commonly published Seed dims — **VERIFY against actual board** |
| Rodeo motherboard mounting pattern | **95.0 × 75.0 mm**, 4× M2 (2.2 mm holes) at corners (100.3/195.3, 55.8/130.8) | `rodeo-motherboard/kicad/rodeo-motherboard.kicad_pcb` — verified |
| Rodeo motherboard outline | ~95×75 mm active area; **overall outline not yet finalized on Edge.Cuts** (board is mid-edit) | Same file — provisional |
| Analog channels | 24 | Derek |
| Digital I/O | 12 | Derek |
| Display | **HiLetgo 2.42″ SSD1309, 128×64, I²C @0x3C** (locked Derek 2026-08-21) | Derek / Bumble |

## Motherboard reference ingested — 2026-08-08

Derek pointed the project at `/Users/derek/proj/rodeo`; the motherboard KiCad project
(`rodeo-motherboard/kicad/rodeo-motherboard.kicad_pcb`, KiCad 9, **mid-edit — read-only, numbers
provisional**) resolves the standing placement blocker. Extracted (as of this state):

- **Mounting:** 4× M2 holes on a **95.0 × 75.0 mm** rectangle (corners above).
- **I/O edge (X ≈ +190, one side):** 4× 3.5 mm audio jacks (PJ320D horizontal) at
  Y = 63.3 / 74.35 / 85.4 / 96.45 → **~11.05 mm pitch**; USB-C (J1) at (194.15, 121.7),
  with USBLC6 ESD nearby. → This is the rear/side I/O edge for my enclosure.
- **Bus headers (center, Y≈94):** I²C (J42, 4-pin), SPI (J43, 6-pin), UART (J44, 4-pin)
  JST SH — the OLED lands on J42 (I²C).
- **Analog fan-out:** ≈40 3-pin JST SH (BM03B) connectors in a grid + a set of 2-pin
  (BM02B) — consistent with per-control analog wiring (pot wiper + rails / switch + gnd)
  rather than on-board pots. This tells us **how the 24 analog inputs are exposed: wired.**

### Architecture reframe — Rodeo is a MODULAR ecosystem, not a monolith

The sibling folders are per-control-type satellite PCBs, each its own KiCad project:
`rodeo-potentiometer`, `rodeo-rotary-switch`, `rodeo-switch`, `rodeo-encoder-bank`,
`rodeo-pixel`. `rodeo-encoder-bank/DESIGN.md` confirms the model: a self-contained
**RP2040 I²C module**, 95 × 62 mm, 4 detented EC11 encoders on **20 mm pitch** (knobs up to
17 mm fit; 20/25 mm do not without a pitch respin), M2.5 corners, Qwiic/STEMMA QT
daisy-chained, USB-C on the bottom-center edge, side-entry Qwiic on left/right.

**This synthesizes the board-mount-vs-wired fork:** controls are **board-mounted on
distributed per-type satellite PCBs**, and those satellites are **wired to the motherboard
hub** — JST SH (3-pin) for analog controls, Qwiic/I²C for smart modules (encoders, pixels).
So my panel layout is driven by the *fixed geometries of the satellite boards* (outlines,
control pitches, connector edges), tiled/mounted over the motherboard, not by a single
co-designed control PCB. Two connection classes to plan harness + strain relief for:
1. **Analog satellites** (pot / rotary-switch / switch) → 3-pin JST SH → mobo ADCs.
2. **Smart satellites** (encoder-bank, pixel) → Qwiic/I²C daisy-chain.

**Next mechanical action:** build a satellite-board geometry table (outline, mounting-hole
pattern, control pitch, control-height, connector edge) for each of the 5 modules by reading
their `.kicad_pcb` outlines — that becomes the tiling kit for the panel/enclosure layout.
Known so far: encoder-bank 95×62 mm, 20 mm pitch, M2.5. Others pending extraction.

## The pivotal mechanical fork: board-mounted vs. panel-mounted controls

This decision, more than any other, sets tolerances, assembly labor, internal volume, and
how tightly my panel couples to Bumble's/Derek's PCB. It should be settled with Derek early.

- **Board-mounted controls** — pots/encoders/jacks soldered to a control PCB that sits
  directly behind the panel, shafts/bushings protruding through panel cutouts.
  - + Cheap, fast, repeatable assembly; no hand-wiring; mechanically rigid.
  - − Panel cutout positions are locked to PCB hole positions to tight tolerance
    (±0.2 mm class); the panel layout effectively *becomes* a PCB layout constraint, so
    it must be co-designed with whoever lays out that control PCB.
  - − Component body heights set the panel-to-PCB standoff; all controls share one plane.
- **Panel-mounted + wired** — each control fastened to the panel by its own nut/bushing,
  wired back to the motherboard via the JST SH harness.
  - + Tolerant of position error; controls independent of any PCB; flexible layout.
  - + Panel can be any material/process without PCB constraints.
  - − Hand-wiring labor; more internal volume for the harness; needs strain relief.
  - − Heavier, more expensive at volume.

Hybrid is common: jacks + a few controls board-mounted, the rest panel-mounted + wired.
**Recommendation pending control count** — but flag for Derek now, it gates everything.

### Converged recommendation (Honey + Bumble, 2026-08-08) — pending Derek's call

Bumble co-owns this fork on analog-noise grounds (pot wipers are high-impedance nodes;
board-mounting = shortest wiper→ADC runs, RC filter at the pin, no harness antennas). We
jointly recommend the **hybrid**:

- **Pots + encoders → board-mounted on a control PCB.** Clean analog + least assembly labor.
- **Jacks + OLED → panel-mounted-and-wired.** Jacks are low-Z, OLED is digital; both
  tolerate the JST SH harness.

**Mechanical consequence I own:** if pots/encoders sit on a control PCB behind the panel,
then **the panel layout *is* the control PCB's mechanical outline** — same 2D coordinate
system, co-designed. Proposed deliverable split so there's no ambiguity:

- **Honey owns:** board mechanical outline, control hole pattern + positions, panel cutouts,
  mounting-hole pattern, board-to-panel standoff stack, and knob/cap selection.
- **Bumble owns:** channel routing / copper on that same outline, RC filters, grounding,
  **and the control-PCB↔motherboard interconnect** (connector type, pin order, ground
  interleave). The two 12-ch ADCs live on the *motherboard*, not the control PCB — so even
  with board-mounted pots, the control PCB needs one board-to-board link carrying up to 24
  analog lines + grounds back to the mobo. Likely split-filter: series R at each pot on the
  control PCB, filter cap at the ADC pin on the mobo, harness carrying already-tamed signals.
- Shared: the outline+hole DXF/STEP is the handoff artifact between us.

**Open mechanical item I own — control-PCB stack arrangement (blocked on mobo reference):**
The interconnect Bumble specs couples to how the control PCB physically relates to the mobo:
- *Mezzanine stack* — control PCB parallel above the mobo on a board-to-board connector;
  fixed stack height, compact, but sets a hard standoff dimension and connector keep-out in
  the internal-depth budget, and the two boards' mounting patterns must register.
- *Offset + wired harness* — control PCB behind the panel, mobo elsewhere in the case, JST
  SH harness between; more layout freedom, more internal volume/strain-relief.
Resolves with the motherboard reference (mounting-hole pattern + ADC input exposure), which
Bumble is already waiting on to finalize the interconnect — same blocker, decide together then.

**Planarity constraint (board-mounted):** all board-mounted controls share one PCB plane, so
their bushing/shoulder heights above the panel must be compatible — either matched component
heights, or I absorb the difference in knob/cap height selection, or the panel gets local
recesses. This is why the "bushing/actuator height" column matters per-row.

## Voice architecture & control inventory v0.1 (Derek, 2026-08-15)

Derek's brief: **"between a Prophet-5 and an Arturia MiniFreak"** — the clarity of a
straightforward subtractive synth, with a few modern tricks (formants, supersaws, unison),
but *two notches less fiddly* than the MiniFreak. Locked answers:

- **Voice count:** 12-voice polyphony (minimum acceptable 8). Supports **poly + mono** modes.
- **Oscillators:** **two full oscillators** (not osc + sub).
- **No sequencer, no arpeggiator.** (Biggest single simplification vs. MiniFreak.)

**Design translation (the anti-MiniFreak stance):** the MiniFreak's complexity is not its
features, it's (1) *mode-multiplexed knobs* (macros re-mean per osc type) and (2) a mod
matrix + sequencer + cycling envelopes. "No hidden controls" already forbids #1; dropping
seq/arp removes most of #2. So Rodeo = one flexible voice, **one knob per function**, laid
out by signal flow. The three modern features are **always-on dial-in controls, never modes:**

- **Supersaw / unison → an OSC "Spread" knob** — one saw fans into a 7-voice detuned stack.
  Dedicated knob, no mode. (Per-osc spread = within one oscillator; *Unison* voice mode
  stacks whole voices — the global **Unison detune** knob serves that.)
- **Formant → a filter "Vowel" morph** (A-E-I-O-U), not a formant *oscillator*. A filter
  character you dial, always available regardless of oscillator settings. Prophet-like.

### Voice v1 UPDATE — locked by Derek 2026-08-23 ("less-complicated MiniFreak")

Firmware gate cleared; Fizz is standing up `rodeo-001/firmware`. Locked voice:
- **12-voice polyphony** (unchanged).
- **Oscillators: subtractive core + FM + formant *models*** — note this **supersedes** the
  v0.1 line above that treated formant as a *filter Vowel morph*. Formant (and FM) are now
  **oscillator models**, presumably selected by the per-osc **Wave encoder**.
- **Filter: polyphonic SVF** (state-variable, per-voice) — a topology detail, no panel impact
  (Cutoff/Reso/EnvAmt/Type/Keytrack unchanged).
- **Mod: Filter ADSR + Amp ADSR + 1 LFO, single selectable destination, NO mod matrix.**

**Panel fit (my confirmation to Fizz, 2026-08-23):** the mod set is **already fully allocated**
— 2 ADSRs = 8 pots (ch13–16 / ch17–20), LFO = Rate+Amount pots (ch21/22) + Shape button +
**LFO Dest button** (the single-destination selector; press-to-cycle, OLED readout). Nothing to
carve; headroom remains (6/12 digital, ch0/ch3 analog, 1 encoder slot).

**✅ RESOLVED — Option A locked (Derek via Fizz, 2026-08-23):** each oscillator's Shape/PW pot
becomes a **model-dependent "Timbre" knob** — one normalized 0–1 param whose meaning follows the
Wave-encoder engine (PW for pulse, **FM-index for FM**, **formant-position for formant**). The
filter **"Vowel" pot stays as-is** (a formant *filter* ≠ a formant *oscillator* — complementary,
both kept). **Zero channel remap, zero cutout change**; ch0/ch3 + spare digital stay in reserve.
Panel relabel done: the 2 OSC Shape pots now read **"Timbre"** here (control-map relabel is
Bumble's). **Legibility note:** silk reads the generic "Timbre" — the *live* per-engine meaning
shows on the **OLED** on model-select (same OLED-lean already accepted for Wave-select); it's the
one spot a knob's current function isn't silk-screened, and it's deliberate.

### Full control inventory (v0.1)

Signal-flow order, left → right. Class column: **pot** (continuous), **sel** (rotary selector,
silk-screened positions), **tog** (toggle/button), **wheel** (performance).

| Section | Control | Class | Notes |
|---|---|---|---|
| OSC 1 | Octave | sel | 32/16/8/4/2 |
| OSC 1 | Wave | sel | tri/saw/square/pulse |
| OSC 1 | Timbre (model-dep) | pot | Shape/PW/FM-index/formant-pos per selected engine (Option A, 2026-08-23) |
| OSC 1 | Spread (supersaw) | pot | |
| OSC 2 | Octave | sel | |
| OSC 2 | Wave | sel | |
| OSC 2 | Timbre (model-dep) | pot | Shape/PW/FM-index/formant-pos per selected engine (Option A, 2026-08-23) |
| OSC 2 | Spread | pot | |
| OSC 2 | Detune | pot | fine, vs OSC 1 |
| OSC 2 | Hard sync → OSC 1 | tog | digital |
| MIX | OSC 1 level | pot | |
| MIX | OSC 2 level | pot | |
| MIX | Noise level | pot | |
| FILTER | Cutoff | pot | |
| FILTER | Resonance | pot | |
| FILTER | Env amount | pot | bipolar |
| FILTER | Keytrack | sel | off / half / full |
| FILTER | Vowel (formant) | pot | off → A-E-I-O-U |
| FILTER | Type | sel | LP / BP / HP |
| FILT ENV | Attack / Decay / Sustain / Release | pot ×4 | |
| AMP ENV | Attack / Decay / Sustain / Release | pot ×4 | |
| LFO | Rate | pot | |
| LFO | Shape | sel | tri/saw/square/S&H |
| LFO | Amount | pot | |
| LFO | Destination | sel | pitch/filter/PW/amp |
| VOICE | Mode | sel | Poly / Mono / Unison |
| VOICE | Unison detune | pot | |
| GLOBAL | Glide | pot | |
| GLOBAL | Master tune | pot | |
| GLOBAL | Master volume | pot | |

**Pitch bend + mod wheel: NOT on the panel** (Derek, 2026-08-15) — supported over **MIDI**
instead; Rodeo is a desktop synth. Removes 2 analog controls and frees 2 ADC channels.

**Tally by class:** 26 pots + 9 rotary selectors = **35 analog** · 1 toggle (of 12 digital).
Sub-oscillator intentionally dropped (two full oscillators cover it); noise kept.

### ⚠ Finding: the all-visible surface exceeds the 24-channel analog budget

This is *not* over-scoping — a fully-visible, one-knob-per-function 2-oscillator synth is
inherently a ~35-control instrument (a Prophet-5 has ~40). Derek asked for Prophet *clarity*,
not Prophet *minimalism*. The mismatch is purely the board: the control map budgets **24
analog channels** (2× ADS114S08), and every pot / rotary-selector / wheel consumes one. We
need **~37 → we are ~13 over.** Three ways to close the gap:

- **A — Expand analog channels (recommended, honors the vibe).** Add a **3rd ADS114S08**
  (12-ch) on SPI1 → 36 channels, plus 1 wheel on a spare Seed native ADC pin, and the whole
  surface is pots + labeled rotary switches, *all visible, zero display dependence* — the
  Prophet-5 experience Derek is after. Cost: one more ADC chip + one CS line. **Bumble's call.**
- **B — Offload selectors + trims to I2C encoder-banks (no board change).** Encoders are
  *free* of the 24/12 budget. Put the ~10 discrete selects + set-and-forget trims (octaves,
  waves, modes, detune, glide, tune) on 2–3 encoder-banks; keep the 24 ADC channels for the
  live-sweep pots. "No hidden controls" is preserved via the encoder RGB rings + OLED — but
  it leans a notch *toward* the MiniFreak look (encoders-with-display) Derek is moving away from.
- **C — Cut ~13 controls.** Real feature reduction (drop filter Type, drop per-osc Spread to
  one global, fixed keytrack, share one ADSR, etc.). Cheapest, least capable.

**Honey's recommendation: Path A.** The design is right-sized; the board budget is the only
thing in the way, and one more ADC chip buys the all-knobs-and-switches Prophet surface
outright. Path B is the clean no-hardware-change fallback. Pending Bumble (channel budget /
3rd-ADC feasibility) + Derek (vibe call: pure pots-and-switches vs. some encoders).

### DECISION (Derek, 2026-08-15): Path B — build on the produced board

Derek: *"Adding another ADC isn't feasible, the main board is already produced. I'm ok with
single encoders or encoder banks. The synth will have an OLED so we can lean on that to
provide clarity where we need."* Bumble confirmed the 3rd ADS114S08 (U5 on PG10/pin 8) is
electrically clean — **but it needs a motherboard respin**, and the board is already made.
So **Path A becomes the documented Rev-B upgrade path, not this build.** We fit the produced
board: 24 ADC channels + the plentiful digital GPIO + I2C encoder-banks + the OLED for
enum readout.

**Recommended Path B allocation (fits the produced board, zero changes):**

| Layer | Budget | Gets | Count |
|---|---|---|---|
| **ADC pots + wheels** | 24 ADC ch | The live-sweep controls — filter (cutoff/res/env-amt/vowel), both ADSRs (8), OSC shape+spread ×2 (4), mixer levels (3), LFO rate+amount (2), master volume, + pitch & mod wheels | **22 pots + 2 wheels = 24** |
| **Digital cycle-buttons** | of 12 GPIO | Labeled enum selectors, current value on OLED: OSC1/2 Wave, Filter Type, Keytrack, LFO Shape, LFO Dest, Voice Mode + OSC-sync toggle | ~8 (≤12) |
| **I2C encoder-bank(s)** | free of 24/12 | Stepped + fine-trim, detented, ring/OLED readout: OSC1/2 Octave, OSC2 Detune, Unison detune, Glide, Master tune | ~6 → 1–2 banks |

**Why this keeps the Prophet feel despite Path B:** the 24 things you actually *sweep* stay
real pots with pointers; enum selectors become one-button-one-parameter (labeled, OLED shows
state — not menu-diving); only the fine trims + octave steps ride encoders. So it reads as
"pots + labeled buttons + a small encoder cluster," not a wall of encoders. The OLED does the
clarity work Derek asked it to. Exact channel-by-channel split firms up in the panel layout +
Bumble's control-map table.

**One feel choice for Derek (non-blocking — I'll default as noted):** enum selectors
(Wave/Octave/etc.) default to **cycle-button + OLED**. If you'd rather a couple feel like true
detented **rotary switches**, we'd free an ADC channel per switch by promoting that many
live-sweep pots to encoders — a straight swap, say the word.

### FROZEN allocation (Wave-select DECIDED — Derek 2026-08-21: encoders)

**Ecosystem (Derek, 2026-08-21):** locked control set = **potentiometers (analog), rotary
encoders (I²C), momentary buttons (digital), HiLetgo 2.42″ SSD1309 OLED (I²C)**. Rotary
switches and LED indicators REMOVED. **OSC Wave-select = rotary ENCODERS (Derek's pick,
option b, 2026-08-21)** — detented click, sits in each OSC box; ch0/ch3 now free as spare
analog. Final frozen tally:

| Class | Count | Panel cutout | Spare |
|---|---|---|---|
| Potentiometers (analog) | **22** | pot bushing Ø7 | **2 ADS channels free** (ch0, ch3 — first-ever spare; future CV/mod in) |
| Rotary encoders (I²C) | **8** | encoder bushing Ø7 (same family as pot) | **8 of 9** on the one hub (1 spare) |
| Momentary buttons (digital) | **6** | button bushing | 6 of 12 free |
| OLED SSD1309 | **1** | display window | — |

→ **37 physical controls: 22 pots + 8 encoders + 6 buttons + 1 OLED.**

- **8 encoders** as individual `rodeo-encoder` singles on the one `rodeo-encoder-hub` (I²C
  @0x30, 9-cap, 8/9 used): **OSC1 Wave, OSC2 Wave** (the Derek pick) + OSC1/OSC2 Octave + OSC2
  detune, unison detune, glide, master tune. Off the 24/12 budget. OLED `0x3C` shares the bus.
  *Firmware note (Fizz):* encoders are incremental → the current waveform (and other encoder
  states) must be **persisted across power cycles** and shown on the OLED; no panel-face
  indication of position. (Per Bumble's absolute-vs-incremental nuance.)
- **6 buttons** = 5 enum cycle-buttons (Filter Type, Keytrack, LFO Shape, LFO Dest, Voice
  Mode) + 1 hard-sync. All ride `rodeo-button` (momentary, 2-pin JST-SH sig/GND, idle-high via
  STM32 internal pull-up). No SPDT, no per-button LED.
- **22 pots** on 24 ADS channels → **ch0/ch3 spare** (were the Wave rotary channels; now free).

**`rodeo-button` = the digital workhorse (my build deliverable).** Carries all 6 digital
actions including hard-sync. 2-pin JST-SH; mechanical/cap design I own; on the family + BOM.

**Rev-B upgrade path (documented, not this build):** if a motherboard respin ever happens
(Issue 6 CS pull-ups are already queued for it), fold in **U5 (3rd ADS114S08, CS3 = PG10/pin
8, +J50–J61)** → 36 ADC channels, and promote the encoder/button enums back to silk-labeled
rotary switches for the fully-visible all-analog surface. Full spec lives in Bumble's control
map (36-channel expansion section).

## Satellite geometry (the tiling kit) — extracted from KiCad, 2026-08-15

Each analog control is its own small carrier PCB (RC filter + JST-SH pigtail); the control
mounts to the panel by its own bushing, board floats behind. Edge.Cuts + footprints read
from each `.kicad_pcb`:

| Satellite | Board (Edge.Cuts) | Control element | Panel mount | Connector | Notes |
|---|---|---|---|---|---|
| `rodeo-potentiometer` | **25 × 30 mm** | Alps RK09K vertical pot | pot bushing Ø7 | 3-pin JST-SH horiz | 4× M2 · R+C RC on board |
| `rodeo-encoder` (individual) | **25 × 25 mm** (measured Edge.Cuts) | EC11 detented encoder + push | encoder bushing Ø7 | I²C to hub | **free-placed single** → `rodeo-encoder-hub` (9-cap, @0x30); groups per-function like a pot |
| `rodeo-button` | **25 × 25 mm** (measured Edge.Cuts) | momentary pushbutton | button bushing | 2-pin JST-SH | **digital workhorse** — all 8 enum/toggle actions incl. hard-sync; free-placed single |
| `rodeo-encoder-hub` | STM32C0 hub PCB (behind-panel, not a control) | — | board-mount, internal | I²C @0x30 | **9-encoder cap**; carries the voice's 6 (+2 if Wave→encoder = 8/9) |
| ~~`rodeo-encoder-bank` (4-gang)~~ | — | — | — | — | **REMOVED (Derek 2026-08-21)** — replaced by hub + individual `rodeo-encoder` singles |
| ~~`rodeo-rotary-switch`~~ | — | — | — | — | **REMOVED** — no rotary switches in ecosystem |
| ~~`rodeo-switch` (SPDT)~~ | — | — | — | — | **REMOVED** — hard-sync now a momentary `rodeo-button` |
| ~~`rodeo-pixel`~~ | — | — | — | — | **REMOVED** — LED indicators dropped; all feedback on OLED |

**Pitch rule set (v2 — 2026-08-21, all controls now free-placed singles):** the whole kit is a
**uniform ~25 mm grid** — pot 25×30, encoder 25×25, button 25×25 — each mounting by its own
bushing and placeable anywhere. Base grid: **25 mm horizontal pitch, 30 mm row pitch** (pots
set the tallest tile; comfortable for 16–18 mm knobs). Encoders/buttons drop into the same grid
with margin. **No fixed multi-gang cluster anymore** — the old 4-gang encoder-bank is gone, so
encoders co-locate with their function (e.g. a Wave encoder inside its own OSC box) exactly
like a pot. Encoders are I²C to the behind-panel `rodeo-encoder-hub`; only the shaft/bushing
lands on the panel.

## Panel Layout v2 (2026-08-21) — Wave = encoders (Derek's pick)

Signal-flow, two bands, left → right, on the uniform 25 mm grid. Every control is a free-placed
single co-located with its function; the OLED is the only element on the right. **The 2 OSC
Wave selectors are encoders sitting in their own OSC boxes** (next to Timbre/Spread), and the
former ENC-BANK cluster is gone — its encoders (octaves, detune, glide, master tune) now live in
their functional sections. Sync is a momentary button; no LEDs anywhere.

```
 ┌──────────┬──────────┬────────┬─────────────┐   ┌───────────────┐
 │  OSC 1   │  OSC 2   │  MIX   │   FILTER     │   │               │
 │ Wave  e  │ Wave  e  │ Osc1 ▪ │ Cutoff ▪     │   │     OLED      │
 │ Timbre▪  │ Timbre▪  │ Osc2 ▪ │ Reso   ▪     │   │   HiLetgo     │
 │ Spread▪  │ Spread▪  │ Noise▪ │ EnvAmt ▪     │   │  2.42″ 128×64 │
 │ Oct   e  │ Oct   e  │ Sync ○ │ Vowel  ▪     │   │   (I²C 0x3C)  │
 │          │ Detune e │        │ Type○  Ktrk○ │   │               │
 ├──────────┼──────────┼────────┼─────────────┤   └───────────────┘
 │ FILT ENV │ AMP ENV  │  LFO   │   GLOBAL / VOICE      │
 │ A ▪ D ▪  │ A ▪ D ▪  │ Rate ▪ │ Volume ▪   Mode ○     │
 │ S ▪ R ▪  │ S ▪ R ▪  │ Amt  ▪ │ Glide e  Uni-det e    │
 │          │          │ Shp○Dst○│ Mtune e              │
 └──────────┴──────────┴────────┴───────────────────────┘
  ▪ pot (ADC, ×22)   e encoder (I²C hub, ×8)   ○ momentary button (×6)   OLED ×1
```

**Encoder placement (8, all on `rodeo-encoder-hub` @0x30):** OSC1 Wave, OSC1 Octave · OSC2
Wave, OSC2 Octave, OSC2 Detune · Glide, Unison-detune, Master-tune (global/voice). Each is a
25×25 single dropped into the grid beside its function.

**Hub-slot binding (LOCKED by Bumble in control map, 2026-08-21):** hub is 8/9 (1 spare);
**slot 7 = OSC1 Wave, slot 8 = OSC2 Wave** (the two new encoders). Wiring convention:
**header order = slot order**, so when I draw the v2 harness the 2 Wave encoders wire to the
next 2 free hub headers → slots 7/8. Bumble confirms the final index↔param map off my v2
harness once drawn. Full slot 1–8 table lives in `RODEO_CONTROL_MAP.md`.
**Button placement (6):** Sync (OSC/MIX seam) · Filter Type, Keytrack (FILTER) · LFO Shape,
LFO Dest (LFO) · Voice Mode (GLOBAL).

**Digital button → connector binding — FROZEN (Bumble took my physical order 1:1, 2026-08-23).**
My signal-flow order bound to the digital connectors + `board.h`:

| # | Button | Panel section | `DIG_n` | Connector | Seed pin |
|---|---|---|---|---|---|
| 1 | OSC2 Hard-sync | OSC/MIX seam | DIG_1 | J30 | 2 |
| 2 | Filter Type (SVF mode) | FILTER | DIG_2 | J31 | 3 |
| 3 | Filter Keytrack | FILTER | DIG_3 | J32 | 4 |
| 4 | LFO Shape | LFO | DIG_4 | J33 | 5 |
| 5 | LFO Dest | LFO | DIG_5 | J34 | 6 |
| 6 | Voice Mode | GLOBAL | DIG_6 | J35 | 7 |

**Why J30–J35 (Bumble's rationale):** these are the **SDMMC pins — digital-only, no ADC/timer
capability** — so buttons waste nothing on them (no SD card in Rodeo → free GPIO, no boot-strap
conflict). **J36–J41 (DIG_7–12, Seed pins 22–27) kept spare on purpose** — they're the
*ADC-capable* pins, so the digital reserve can be reclaimed later as native analog / CV / timer.
**Electrical:** `rodeo-button` is momentary-to-GND with a 100 nF debounce on the satellite, no
hub-side resistor → firmware `GPIO::Pull::PULLUP`, active-low (pressed = 0); hard-sync is the
same momentary button, firmware treats the press as a toggle. Control map is now frozen on all
three classes (pots ch0–23, buttons DIG_1–6, encoder hub slots 1–8).

**ADC channel sequence (ch = reading order, top band L→R then bottom band L→R).** Wave→encoder
freed **ch0 (J6) and ch3 (J9)** — now spare. **LOCKED (Bumble, 2026-08-21): gaps stay, no
re-pack** — all 22 pots keep their exact frozen ch assignments + existing harness wiring; the
only delta is ch0/ch3 → SPARE. J6/J9 are reserved as our first free ADS headers for a future
CV/mod input (which brings its own buffer + protection).

| ch | Conn | Control | | ch | Conn | Control |
|---|---|---|---|---|---|---|
| 0 | J6 | **SPARE** (was OSC1 Wave; now encoder) | | 12 | J18 | FILTER Vowel |
| 1 | J7 | OSC1 Timbre (model-dep) | | 13 | J19 | FILT ENV Attack |
| 2 | J8 | OSC1 Spread | | 14 | J20 | FILT ENV Decay |
| 3 | J9 | **SPARE** (was OSC2 Wave; now encoder) | | 15 | J21 | FILT ENV Sustain |
| 4 | J10 | OSC2 Timbre (model-dep) | | 16 | J22 | FILT ENV Release |
| 5 | J11 | OSC2 Spread | | 17 | J23 | AMP ENV Attack |
| 6 | J12 | MIX Osc1 level | | 18 | J24 | AMP ENV Decay |
| 7 | J13 | MIX Osc2 level | | 19 | J25 | AMP ENV Sustain |
| 8 | J14 | MIX Noise level | | 20 | J26 | AMP ENV Release |
| 9 | J15 | FILTER Cutoff | | 21 | J27 | LFO Rate |
| 10 | J16 | FILTER Resonance | | 22 | J28 | LFO Amount |
| 11 | J17 | FILTER Env amount | | 23 | J29 | GLOBAL Master volume |

**Chip-boundary note for Bumble:** U3 (ch 0–11) / U4 (ch 12–23) split falls mid-FILTER
(Env amount on U3, Vowel on U4). Electrically fine (both chips scanned) — but if you'd rather
a functional section stay whole on one chip, rotate the assignment; the *physical* order above
is the fixed input from me, map as you see fit.

**Overall envelope (first-pass):** ~**380–420 mm W × 190–220 mm D** panel — an 8-section desktop
surface, CO2-laser-cut flat sheet at a ~10° tilt on the two 3D-printed cheeks (fab/form/aesthetic
LOCKED 2026-08-15). The motherboard (95 × 75) + Seed sit inside with room to spare; controls,
not the boards, set the size (bottom-up per our governing principle). With Wave→encoder the
right side now carries **only the OLED** (ENC-BANK block gone) — freeing that real estate to
center/enlarge the 2.42″ display. Final W×D firms up once the 25 mm-grid placement is
dimensioned (v2 detail pass) and the OLED window lands from Derek's caliper reading.

## Enclosure construction & aesthetic (Derek, 2026-08-15)

Derek's calls: **(1) desktop with a slight tilt**, size driven by fabrication; **(2) hybrid
fab — 3D printing (256³ mm build volume) + CO2 laser (12×18″ = 305×457 mm bed); (3)
Cassette-Futurism** aesthetic (retrofuturist ~1970s–90s: earth tones, Supergraphic Ultramodern,
laser-grid, chunky late-Cold-War hardware).

### The governing fabrication constraint — read this first

**The 3D printer's 256 mm axis cap is smaller than the panel (~400 mm wide); the CO2 laser bed
(457 mm long axis) is not.** So the build splits along tool lines:

- **Panel → CO2-laser-cut + engraved flat sheet.** ~400 × 200 mm fits the 305×457 bed
  (long axis along the 457 mm). This is also the *better* tool for the panel: clean, repeatable
  Ø7 bushing holes and crisp engraved labels. **Note: CO2 cannot cut metal** — so the earlier
  "CNC aluminum" panel option is off; panel material is **acrylic or plywood** (see below).
- **End-cheeks (×2, the tilt wedges) → 3D-printed.** Each is small (~200 D × 60 H × 18 W) —
  well inside 256³ — and printing is ideal for the wedge geometry + internal standoff bosses.
- **Base tray / rear I/O spine → laser-cut flat parts** (captured between the cheeks), or
  printed if a part stays under 256 mm. Any full-width part (spanning ~400 mm) **must be
  laser-cut, never a single print.**

Construction: two 3D-printed end-cheeks carry the laser-cut panel at a slight tilt (proposing
**~10°**, front lip ~30 mm / rear ~55–60 mm to clear internal depth + the rear I/O edge), with
a laser-cut base tray between them. Motherboard (95×75, 4× M2) on standoff bosses off the base
or cheeks; satellite carriers hang on each control's own panel bushing.

### Aesthetic direction — Cassette Futurism (proposed, for Derek's nod)

- **Panel:** **two-layer laser acrylic** — a colored cap over a contrasting core, engraved
  *through* the cap so labels read in the core color. Earth-tone pairing (e.g. **cream-over-
  burnt-orange** or **mustard-over-brown**). Period-correct, durable, and the signature
  Supergraphic look comes free from the engrave-through. (Alt: baltic-birch ply for real-wood
  warmth — but acrylic holds the 24+ round bushing holes tighter and color-matches the printed
  cheeks; **my recommendation = two-layer acrylic**.)
- **Supergraphics:** bold engraved section-blocks/stripes dividing the 8 zones; chunky 70s sans
  labels (Moog/ARP/Braun lineage). Optional laser-grid motif framing the OLED window.
- **Knobs & cheeks:** skirted fluted 70s-style knobs, earth-tone or black w/ a bright pointer;
  end-cheeks printed in a matching or contrasting earth tone (wood-fill filament an option).
- **Freed constraint:** with the panel non-metal, engraving *is* the labeling (no separate
  silkscreen step) — plays straight into the cassette-futurist look.

**Open for Derek (non-blocking, I'll proceed on the recommendation):** two-layer acrylic vs.
plywood panel, and the earth-tone color pairing. Everything else I can dimension now.

## Panel cutout schedule v1 (prep while enclosure calls pend)

Per-control panel geometry — this is **control-driven, not enclosure-driven**, so it's valid
regardless of form factor/material. The *tolerance/clearance* column is the only fab-dependent
part and waits on the fabrication method. Nominal bushing Ø below; ⚠VERIFY items need a
datasheet pull before templates are cut.

| Control | Panel feature | Nominal | Anti-rotation | Confidence |
|---|---|---|---|---|
| Pot (Alps RK09K) ×22 | round hole | **Ø7.0 mm** (M7 bushing) | 3.2 mm lug hole @ offset | high (RK09K std) |
| Encoder (Alps EC11) ×8 | round hole | **Ø7.0 mm** (M7 bushing) | 3.2 mm lug hole @ offset | high (EC11 std) · incl. 2 OSC Wave |
| Button (`rodeo-button`) ×6 | round hole | TBD w/ chosen switch (target ~Ø6–7 mm bushing) | — | derive from `rodeo-button` footprint |
| OLED (**HiLetgo 2.42″ SSD1309, I²C**) | rect window + 4× mount | module PCB ~**72 × 43 mm**; active window **55.01 × 27.49 mm** | 4× mount holes — ⚠VERIFY pattern | window high (confirmed w/ Bumble); **hole pattern not published → Derek to caliper the physical module (4 centers + Ø); do NOT cut standoffs from a guess** |
| Audio / USB-C / power | rear I/O edge | on motherboard I/O edge (jacks 11 mm pitch, per mobo) | — | high (from mobo) |

**~~Wave rotary (SRBV14)~~ and ~~Sync toggle (NKK GW12)~~ rows removed** — rotary switches and
the SPDT toggle are gone from the ecosystem (Derek 2026-08-21); both functions now ride
`rodeo-button`. Two ⚠VERIFY datasheet blockers (SRBV14 rotary bushing, GW12 toggle) thereby
**evaporate**; the OLED variant blocker is **resolved to a concrete part** (HiLetgo 2.42″
SSD1309) — only its 4-hole mounting pattern still needs the mechanical drawing before cutting.

**Knob/actuator collision check (v1 pitch):** at 25 mm horizontal pitch, 16–18 mm knobs leave
7–9 mm finger gap — comfortable. Rotary/toggle/encoder shafts co-planar on the panel; the
carrier boards float behind on their own bushings, so per-control behind-panel depth = board
(≤6 mm) + JST-SH pigtail bend radius — sets minimum internal depth (~15–18 mm) once confirmed.

### `rodeo-button` momentary satellite — design brief (my deliverable)

**PCB state (checked 2026-08-21):** `rodeo-button.kicad_pcb` exists, board outline **25 × 25 mm**
(measured, matches the encoder tile), with a 2-pin JST-SH (SM02B-SRSS-TB horizontal) + a 1206 R.
But the switch is a **generic `Switch:SW_Push` symbol with NO footprint assigned** — so there is
**no real bushing Ø to read off yet.** Selecting the actual panel-mount momentary switch is my
open design task; it sets both Bumble's PCB footprint and my cutout Ø.

- **Electrical (settled w/ Bumble):** 2-pin JST-SH (sig/GND), momentary NO to GND, idle-high
  via STM32 internal pull-up — no on-board pull-up, no LED (value shows on OLED). (The 1206 R on
  the board is likely a placeholder/optional series — confirm with Bumble whether it stays.)
- **Mechanical (mine — OPEN):** pick a panel-mount momentary pushbutton with a threaded bushing
  so it mounts like the pots/encoders. Target ~Ø6–7 mm bushing to sit in the 25 mm grid; want a
  cap that suits the Cassette-Futurism look. Candidate class: 6 mm threaded-bushing momentary
  (e.g. PVA1 / E-Switch TL59 class), JLCPCB-stockable (there's a `jlcpcb/` folder). Deliver: a
  specific MPN → hand Bumble the footprint, log the bushing Ø in the cutout schedule.
- **Qty in first build (frozen 2026-08-21):** **6** — Filter Type, Keytrack, LFO Shape, LFO
  Dest, Voice Mode, hard-sync.



Bumble's rows will carry type / footprint class / edge-vs-face / grouping. For placement
I also need, per control:

- **Mount style** — board-mounted vs panel-mounted-and-wired (per the fork above).
- **Bushing/actuator height above panel** — sets knob/cap selection and how far shafts
  protrude; drives knob collision spacing.
- **Behind-panel depth** — component body + solder + wiring/connector height; sets the
  minimum enclosure internal depth and standoff stack.
- **Actuation axis** — for toggles/slide switches, which way they throw (affects label
  placement and finger clearance).

## Open items — need input

- [ ] **Motherboard reference** — footprint, mounting-hole pattern, connector positions +
      heights (audio/MIDI/USB/power), Seed header location. **Shared blocker with Bumble.**
- [ ] **Control wishlist / count** — from Derek; sizes the panel and therefore the case.
- [ ] **Board-mount vs wired** decision (see fork above) — Derek.
- [ ] **CV/gate jack count** — sets rear-edge real estate; joint with Bumble (each jack ≈
      one analog channel + one rear-edge jack cutout).
- [x] **Form factor** — LOCKED (Derek 2026-08-15): desktop, slight tilt (~10°), size driven by fab.
- [x] **Fabrication method** — LOCKED (Derek 2026-08-15): hybrid = 3D-printed end-cheeks + CO2-laser-cut panel/base (CNC-alu retired; CO2 can't cut metal).
- [x] **Aesthetic direction** — LOCKED (Derek 2026-08-15): Cassette-Futurism.
- [ ] **Panel material + color** (non-blocking; I default two-layer acrylic) — two-layer acrylic vs plywood + color pairing — Derek.
- [ ] **OSC Wave feel** (a button / c pot; both trivial per Honey+Bumble) — Derek's feel call, freezes placements.
- [ ] **OLED 4-hole mounting pattern** — not published; Derek to caliper the physical module.

## Rear-edge jack geometry (planning rule of thumb, pre-layout)

Panel-mount 3.5 mm jacks want ~11–12 mm center-to-center minimum for finger + plug barrel
clearance; nut flats want a bit more. A rear edge N mm wide fits roughly `floor(N/12)` jacks
in a single row before audio/MIDI/USB/power claim their share. So CV/gate jack count trades
directly against rear-edge width, which trades against overall footprint. This is why jack
count is a joint Bumble/Honey decision, not just an electrical one.

## Change log

- 2026-08-21 (latest) — **OSC Wave-select DECIDED = encoders** (Derek, option b). Final frozen
  tally: **22 pots + 8 encoders + 6 buttons + 1 OLED = 37 controls.** Both OSC Wave selectors
  become `rodeo-encoder` singles in their OSC boxes (8/9 on the one hub @0x30); ch0/ch3 (J6/J9)
  freed as the first-ever spare ADS channels (future CV/mod in). Buttons drop 8→6 (5 enum +
  hard-sync). **Delivered Panel Layout v2** (redrawn floorplan on the 25 mm grid, ENC-BANK block
  dissolved, encoders distributed to function, OLED alone on the right). Updated cutout schedule
  (enc ×8 / btn ×6), ADC table (ch0/ch3 SPARE), envelope note. Firmware flag for Fizz: encoders
  are incremental → persist waveform + encoder state across power cycles, value on OLED.
  Remaining on me: button bushing Ø off the `rodeo-button` footprint, then dimensioned v2 cut
  templates once Derek calipers the OLED 4-hole pattern.
- 2026-08-21 (later) — **Encoder architecture changed** (Derek): the fixed 4-gang
  `rodeo-encoder-bank` is **gone**, replaced by a hub-and-satellite scheme — STM32C0
  `rodeo-encoder-hub` (**9-encoder cap**, one hub @0x30 carrying the voice's 6) + individual
  `rodeo-encoder` satellites. **Panel impact: encoders are now free-placed singles like pots
  and buttons** — the fixed-cluster constraint that ranked Wave-option (b) last is void, so
  **(b) is reopened** and all three Wave options co-locate in the OSC box (pure feel call).
  Measured the new outlines from KiCad Edge.Cuts: **`rodeo-encoder` = 25 × 25 mm, `rodeo-button`
  = 25 × 25 mm** (both individual, M2-class corners). Tiling kit → **uniform ~25 mm grid**
  (pot 25×30, encoder 25×25, button 25×25). Updated tiling table, pitch rules (v2), Wave-feel
  note, and flagged Panel Layout v1 floorplan SUPERSEDED (shows old ENC BANK + rotaries).
  Satellite projects confirmed on disk under `/Users/derek/proj/rodeo/`: `rodeo-encoder`,
  `rodeo-encoder-hub`, `rodeo-button` exist; `rodeo-encoder-bank`, `rodeo-switch`, `rodeo-pixel`
  gone. Revised panel lean: **(b) best action / (c) panel-face legibility / (a) minimalist.**
- 2026-08-21 — **Ecosystem locked to 4 control types** (Derek): potentiometers (analog),
  rotary encoders (I²C), momentary buttons (digital), HiLetgo 2.42″ SSD1309 OLED (I²C).
  **Rotary switches + LED indicators REMOVED.** Panel impact: cutout families collapse to
  {pot bushing, encoder bushing (same family), button, one OLED window}; **zero LED
  cutouts/lightpipes/bezels**. `rodeo-rotary-switch`, `rodeo-switch` (SPDT), `rodeo-pixel`
  struck from the tiling kit; **`rodeo-button` promoted to the digital workhorse** (all 8
  digital actions incl. hard-sync). Bumble's re-reconciled tally: **22 pots + 8 buttons + 6
  encoders + 1 OLED = 37 controls**; 2 analog channels (ch0/ch3) freed by the Wave rehome.
  Panel real estate absorbs +2 buttons easily (nets simpler: −2 rotary 30×30 tiles, −1 SPDT
  tile). **OSC Wave feel open for Derek** — (a) cycle-button ≈ (c) pot-as-4-zone > (b) encoder
  (Honey+Bumble agree; encoder-bank's fixed 4-gang geometry can't co-locate Wave with its OSC
  pots). OLED cutout: outline confirmed (PCB ~72×43, window 55.01×27.49 mm); 4-hole pattern
  not published → Derek to caliper the module. **Also corrected stale blocker:** enclosure
  form/fab/aesthetic were already LOCKED 2026-08-15 — not pending Derek.

- 2026-08-15 — **Enclosure construction & aesthetic** locked from Derek's calls (desktop
  slight tilt; hybrid fab = 3D print 256³ + CO2 laser 305×457; Cassette-Futurism). Key
  finding: the **256 mm print axis < ~400 mm panel < 457 mm laser axis**, so the build splits
  along tool lines — **panel = CO2-laser-cut/engraved flat sheet** (acrylic or ply; CO2 can't
  cut metal, so the CNC-alu option is retired), **end-cheeks (×2 tilt wedges) = 3D-printed**,
  **full-width base/rear-spine = laser-cut**. Construction: two printed cheeks carry the
  laser panel at ~10° (front lip ~30 / rear ~55–60 mm), laser base tray between. Aesthetic:
  two-layer engrave-through acrylic (cream-over-burnt-orange / mustard-over-brown),
  supergraphic section-blocks, fluted 70s knobs, earth-tone printed cheeks. Non-blocking
  choices left for Derek: panel material (acrylic vs ply) + color pairing.
- 2026-08-15 — **Panel Layout v1** + satellite geometry table (tiling kit) added. Extracted
  Edge.Cuts from all 5 satellite `.kicad_pcb`s (pot 25×30, rotary 30×30, switch 20×30, pixel
  40×30, encoder-bank 100×67 / 20 mm pitch). Set pitch rules (25 mm H / 30 mm row). Two-band
  signal-flow floorplan; froze the physical **ADC channel sequence ch0→23 = J6→J29** for
  Bumble to map 1:1. Overall envelope first-pass ~380–420 × 190–220 mm. Gated on form-factor /
  fab method / aesthetic from Derek.
- 2026-08-15 — **Wheels removed** (Derek: pitch-bend + mod over MIDI; desktop synth). Surface
  → 35 analog (26 pot + 9 sel). Spent the 2 freed ADC channels promoting **OSC1/OSC2 Wave to
  rotary switches** (`rodeo-rotary-switch`); ADS back to full 24. Bumble froze Path B class
  allocation in the control map; octaves confirmed as encoders (2nd bank `0x31`); digital enum
  buttons drop to 5 (+sync). Flagged **new `rodeo-button` momentary satellite** needed
  (`rodeo-switch` is verified SW_SPDT latching — good for sync toggle, not the cycle-buttons).
- 2026-08-15 — **Path B decided** (Derek: board already produced, no respin; OK with
  encoders/banks + lean on OLED). Recorded Path B allocation: 24 ADC = 22 live-sweep pots +
  2 wheels; enum selectors → digital cycle-buttons w/ OLED; fine trims + octaves → 1–2 I2C
  encoder-banks. Path A (3rd ADS114S08 / U5 on PG10) reframed as documented Rev-B upgrade path.
- 2026-08-15 — Added **Voice architecture & control inventory v0.1** from Derek's brief
  (Prophet-5 × MiniFreak, 12-voice poly + mono, 2 full osc, no seq/arp). Full 37-analog
  control list; surfaced the finding that an all-visible 2-osc surface exceeds the 24-channel
  ADC budget by ~13, with three resolutions (A: 3rd ADS114S08 — recommended; B: encoder-bank
  offload; C: feature cut). Fixed stale control-map cross-ref path (now `./RODEO_CONTROL_MAP.md`).
- 2026-08-08 — Doc created as mechanical counterpart to the control map. Recorded known
  constraints, the board-mount-vs-wired fork, extra per-control fields needed, and jack
  geometry rule. Real layout blocked on motherboard reference + control wishlist.
