---
title: "rodeo-002 — Instrument Concept"
tags: [rodeo, rodeo-002, instrument, concept, granular, plaits, daisy-seed]
status: draft
created: 2026-09-20
updated: 2026-09-20
---

# rodeo-002 — Instrument Concept

**What it is:** the second instrument instance built on the Rodeo platform (see the repo
README — the platform is the product, an instance is a demonstration of it). rodeo-002 is a
**multi-engine digital polysynth feeding a Chroma-Console-style texture chain that ends in a
granular delay.** It is played from an external MIDI keyboard; it has no keybed and no
sequencer.

It is deliberately *not* rodeo-001. rodeo-001's governing rule was **no hidden controls**;
rodeo-002 accepts a **single shift layer** (Derek, 2026-09-20) in exchange for far more
parameters than its 21 physical controls could otherwise reach.

## Fixed constraints (Derek, 2026-09-20)

| | |
|---|---|
| Parts on hand | 1 motherboard · 1 encoder hub · **5 encoders** · **16 pots** · 1 OLED · **no buttons** |
| Played by | MIDI keyboard only (TRS MIDI IN). No arp/sequencer requirement |
| Daisy | Seed, **64 MB SDRAM** variant |
| Audio input | **Ignored for this build.** Possible future upgrade (the jacks exist) |
| Presets | **Required** |
| Voices | Voice-mode switch. **6 minimum, 8 target, 12 only if resources spare** |
| Touchstone | **Hologram Electronics Chroma Console** — its textures and granular delays |

## Signal flow

```
MIDI IN ─► voice alloc ─► N × [ macro-oscillator engine ─► SVF ─► amp env ] ─► stereo sum
                                                                                   │
   ┌───────────────────────────────────────────────────────────────────────────────┘
   ▼
 [CHARACTER] ──► [MOVEMENT] ──► [TEXTURE] ──► [SPACE] ──► tilt EQ ─► volume ─► LINE OUT
  drive /        chorus /       bitcrush /    granular delay / cloud,
  fold /         vibrato /      tape wobble / freeze, reverse, pitch-shifted
  saturate       phaser         lo-fi filter  grains (shimmer)
```

### The voice

One **engine per patch**, selected on the main encoder. Every engine is driven by the same
three macros — **Harmonics / Timbre / Morph** — so the panel never changes meaning, only
character. Candidate engine list, **measured on hardware 2026-09-20** (budget section
below). "Voices" is the largest count that stayed under 70% audio load **with the texture
chain running** — i.e. what the instrument can actually offer.

| Engine | Source | Cost/voice | Voices |
|---|---|---|---|
| Virtual analog (synced variable-shape) | DaisySP `VariableShapeOscillator` | 3.3% | **16** |
| Variable saw | DaisySP `VariableSawOscillator` | 3.0% | **16** |
| Particle (resonant noise grains) | DaisySP `Particle` | 3.6% | **12** |
| Formant / vowel | DaisySP `FormantOscillator` | 4.6% | **12** |
| Harmonic / additive (16 partials) | DaisySP `HarmonicOscillator<16>` | 6.7% | **8** |
| 2-op FM | DaisySP `Fm2` | 6.8% | **8** |
| Grainlet | DaisySP `GrainletOscillator` | 6.9% | **8** |
| Vosim | DaisySP `VosimOscillator` | 8.9% | 6 |
| Z-osc | DaisySP `ZOscillator` | 9.5% | 6 |
| Karplus string | DaisySP `String` | 14.5% | **Mono · Unison-2** |
| String voice | DaisySP `StringVoice` | 17.1% | **Mono · Unison-2** |
| Modal | DaisySP `ModalVoice` | 46.2% | **Mono only** |
| Wavetable | **to port** — Plaits (MIT), or a small in-house table set | unmeasured | — |
| Chord | **to port** — Plaits (MIT) | unmeasured, ~4–6 osc/voice | — |

**The three physical models cannot meet the 6-voice minimum, so they are monophonic
engines** (Derek, 2026-09-20 — see Voice modes). They are struck and plucked models that
suit monophonic playing anyway. Everything from Grainlet up meets the 8-voice target; the
two cheapest VA engines have room for twice that.

Most of the list is already vendored in `rodeo-motherboard/firmware/DaisySP` — these are the
Plaits-derived modules Émilie Gillet contributed upstream, so the "Plaits-like" feel is mostly
a matter of *driving* them from common macros rather than porting new DSP. Anything still
missing comes from the MIT-licensed Mutable source.

**Per voice, after the engine:** a DaisySP `Svf` (Derek chose a real multimode filter over a
Plaits-style low-pass gate) plus an amp envelope, a filter envelope and one LFO.

**Voice modes:** Poly (8, or 6 on Vosim/Z-osc) · Mono (legato + glide) · Unison (stacked,
detuned by Spread).

**Mono-only engines (decided 2026-09-20).** Modal, String and Karplus cost too much per voice
to be polyphonic, so each engine declares which voice modes it supports:

| Engine class | Poly | Mono | Unison |
|---|---|---|---|
| Everything 8-voice or better | 8 | yes | up to 4 |
| Vosim, Z-osc | 6 | yes | up to 4 |
| String, Karplus | — | yes | 2 only |
| Modal | — | yes | — |

Selecting a mono-only engine while in Poly **switches the voice mode to Mono and says so on
the OLED**, remembering the previous mode so that moving back to a polyphonic engine restores
it. The engine list is never filtered — every engine stays reachable in the same order, and
the instrument explains the change rather than hiding the option. This is the one place where
engine choice reaches outside the voice.

### The texture chain

Four slots in series, mirroring the Chroma Console's module layout: each slot has a
**selectable effect type** (encoder), **one amount knob** (pot), a **secondary parameter**
(shift + encoder) and a **bypass** (encoder tap). The chain is stereo and runs once on the
voice sum, not per voice.

**SPACE is the headline block.** A granular delay whose buffer lives in SDRAM — 64 MB is
minutes of stereo float, so buffer length is a non-issue. Controls: Time, Feedback, Grain
(size/density macro), Mix on the main layer; grain pitch, position spread on shift.
**Freeze** (long-press its encoder) locks the buffer so new notes play over a held cloud;
that is the instrument's signature gesture, and it makes a "capture" feature nearly free.

## Resource budget — MEASURED on hardware, 2026-09-20

Measured by [`bench_voices`](../firmware/bench_voices/) on the real board, at 48 kHz / block
48, voices in SRAM and the grain buffer in SDRAM. Raw log:
[`bench_voices-2026-09-20.log`](bench_voices-2026-09-20.log).

**The texture chain costs a flat 12.9%** — 12.8–13.0% measured across five engines — and it
does not scale with voice count, because it runs once on the voice sum. That figure includes
the granular delay at its worst case: 12 overlapping grains, long and dense, shimmer pitch,
feedback up, reading a 4-second SDRAM buffer. **The headline feature of the instrument costs
about an eighth of the processor**, and SDRAM latency — the thing most likely to have made
this unworkable — is a non-issue.

Everything else is linear in voice count, so the whole budget is:

```
load = 12.9% (chain) + voices x cost-per-voice
```

**8 voices is confirmed for 7 of the 12 measured engines**, and the two cheapest run 16.
Per-engine cost is in the table above.

### What this means for the design

* **8 voices is the architecture** — not 6, not 12. Only the two VA engines and Particle
  could do more, and designing around a count that a third of the engine list cannot reach
  would make a worse instrument.
* **There is real headroom at 8 voices** on the cheaper engines: 8 VA voices plus the chain
  is ~39%. That is better spent on a *richer voice* — a second oscillator, a better filter, a
  second LFO — than on voices 9 through 16.
* **70% is the ceiling, deliberately.** The benchmark ran with no pot scanning, no encoder
  polling, no OLED and no MIDI. Audio runs in interrupt context and the UI runs in what is
  left, so that 30% remainder is the UI's entire budget — a full OLED frame at 400 kHz is
  ~23 ms of I²C by itself.
* **Wavetable and Chord remain unmeasured** — they are not ported yet. Chord stacks 4–6
  oscillators per voice, so expect roughly 4 voices; it may not earn its place.

## Preset system

Patches in the Daisy's QSPI flash: engine, all pot values (both layers), all four slot types
and their parameters, voice mode, MIDI settings. Browsing and saving live on the main encoder
(see the control map).

**Physical-position problem:** pots hold their last physical position, so after a preset load
(and after every shift-layer release) the panel disagrees with the patch. Every pot therefore
uses **pickup** — a pot is inert until it crosses the stored value. This applies to the main
layer too, not just the shift layer.

## Open questions

1. **Gesture recorder** (record and loop a knob movement, as the Chroma Console does) —
   Derek: nice-to-have, droppable if an encoder action is better spent elsewhere. Parked.
2. ~~The three physical models miss the 6-voice minimum~~ **RESOLVED (Derek, 2026-09-20):**
   keep them, restricted to Mono/Unison — see Voice modes above.
3. Panel/enclosure layout — not started. The 4×4 pot grid plus a row of 5 encoders under the
   OLED is the assumed geometry.
4. MIDI CC in/out map for the pots.
