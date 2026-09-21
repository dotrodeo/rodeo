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
character. Candidate engine list (final set gated on the CPU spike below):

| Engine | Source |
|---|---|
| Virtual analog (dual variable-shape) | DaisySP `VariableShapeOscillator`, `VariableSawOscillator` |
| Waveshaper / wavefolder | DaisySP `Oscillator` + `Wavefolder` |
| 2-op FM | DaisySP `Fm2` |
| Formant / vowel | DaisySP `FormantOscillator`, `VosimOscillator` |
| Harmonic / additive | DaisySP `HarmonicOscillator` |
| Granular-ish grit | DaisySP `GrainletOscillator`, `Particle` |
| String (Karplus/physical) | DaisySP `StringVoice`, `KarplusString` |
| Modal | DaisySP `ModalVoice` |
| Wavetable | **to port** — Mutable Instruments Plaits (MIT), or a small in-house table set |
| Chord | **to port** — Plaits (MIT), the most expensive engine by far |

Most of the list is already vendored in `rodeo-motherboard/firmware/DaisySP` — these are the
Plaits-derived modules Émilie Gillet contributed upstream, so the "Plaits-like" feel is mostly
a matter of *driving* them from common macros rather than porting new DSP. Anything still
missing comes from the MIT-licensed Mutable source.

**Per voice, after the engine:** a DaisySP `Svf` (Derek chose a real multimode filter over a
Plaits-style low-pass gate) plus an amp envelope, a filter envelope and one LFO.

**Voice modes:** Poly · Mono (legato + glide) · Unison (stacked, detuned by Spread).

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

## Resource budget — UNVERIFIED, gating the design

**8 voices is the working target; 12 is unlikely once the texture chain and granular delay
are live.** The Daisy's H7 has real headroom, but these engines were written for
single-voice Eurorack modules, and the heavier ones (chord, modal, full FM) may need a
per-patch voice cap applied automatically.

> **Next build step:** a bench program that instantiates N copies of each candidate engine on
> the actual board and reports audio-callback load, then adds the texture chain. Its numbers
> set the voice count and the final engine list **before** any of this is committed to.

Working assumptions for that spike: 48 kHz, block size 48, voices and FX state in internal
SRAM, granular buffer in SDRAM.

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
2. Final engine list, pending the CPU spike.
3. Panel/enclosure layout — not started. The 4×4 pot grid plus a row of 5 encoders under the
   OLED is the assumed geometry.
4. MIDI CC in/out map for the pots.
