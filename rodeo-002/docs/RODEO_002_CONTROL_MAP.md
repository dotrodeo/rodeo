---
title: "rodeo-002 Control Map — Interface Contract"
tags: [rodeo, rodeo-002, control-map, pinout, ads114s08, encoder-hub]
status: draft
created: 2026-09-20
updated: 2026-09-20
---

# rodeo-002 Control Map — Interface Contract

Source of truth for **which physical control does what** on rodeo-002. Firmware binds exactly
these channels and slots; a panel must physically fit this. Sibling doc:
[`RODEO_002_CONCEPT.md`](RODEO_002_CONCEPT.md).

Platform facts referenced here come from
[`rodeo-motherboard/docs/hardware-map.md`](../../rodeo-motherboard/docs/hardware-map.md) and
[`RODEO_HUB_I2C_CONTRACT.md`](../../rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md); the
channel convention `ch = chip·12 + AINindex` is the platform's, shared with rodeo-001.

> **Status: DRAFT.** Channel and slot assignments below are a proposal, not frozen — nothing
> is wired yet. The CPU spike in the concept doc may still change the parameter set.

## Surface

**16 pots + 5 encoders (each with a push switch) + 1 OLED = 22 controls. No buttons.**

Consequences of having no buttons:
* **Shift is a held encoder press**, not a latch. The hub exposes live switch level in
  `SW_STATE_LO` (§5 of the I²C contract), so "held right now" is directly readable.
* Every mode, preset and voice-mode action is a press, a long-press or a press-and-turn.

## Analog — 16 pots on ADC channels 0–15 (J6–J21)

Physical order is the 4×4 grid read left-to-right, top-to-bottom, mapped 1:1 onto consecutive
channels. **Channels 16–23 (J22–J29) are left spare**, as are all 12 digital inputs.

| `ch` | Grid | Main layer | Shift layer | Chip | AINx | Connector |
|---|---|---|---|---|---|---|
| 0  | R1C1 | Harmonics      | Env→Timbre          | U3 | AIN0  | J6  |
| 1  | R1C2 | Timbre         | Env→Morph           | U3 | AIN1  | J7  |
| 2  | R1C3 | Morph          | LFO rate            | U3 | AIN2  | J8  |
| 3  | R1C4 | Spread / detune| LFO depth           | U3 | AIN3  | J9  |
| 4  | R2C1 | Cutoff         | Filter env amount   | U3 | AIN4  | J10 |
| 5  | R2C2 | Resonance      | Keytrack            | U3 | AIN5  | J11 |
| 6  | R2C3 | Attack         | Decay               | U3 | AIN6  | J12 |
| 7  | R2C4 | Release        | Sustain             | U3 | AIN7  | J13 |
| 8  | R3C1 | Character amt  | Character secondary | U3 | AIN8  | J14 |
| 9  | R3C2 | Movement amt   | Movement secondary  | U3 | AIN9  | J15 |
| 10 | R3C3 | Texture amt    | Texture secondary   | U3 | AIN10 | J16 |
| 11 | R3C4 | Space amt      | Space secondary     | U3 | AIN11 | J17 |
| 12 | R4C1 | Space Time     | Grain pitch         | U4 | AIN0  | J18 |
| 13 | R4C2 | Space Feedback | Grain position spread | U4 | AIN1 | J19 |
| 14 | R4C3 | Space Grain    | Tilt EQ             | U4 | AIN2  | J20 |
| 15 | R4C4 | Space Mix      | Master volume       | U4 | AIN3  | J21 |

* Rows 1–2 are the voice, rows 3–4 the texture chain — rows 3 and 4 are Chroma-Console-shaped
  (four module amounts, then the granular block's own controls).
* Shift-layer labels are printed smaller on the panel. Labeled ≠ hidden.
* Pot wiring: 3-pin JST-SH, `1=+3V3, 2=wiper, 3=GND` at the motherboard. **These runs need
  REVERSED cables** — the satellite is pinned `GND / signal / supply-in`. See the harness
  section of rodeo-001's control map; a straight cable makes every pot read backwards.

### Pickup (mandatory, both layers)

A pot is **inert until its position crosses the parameter's stored value**, after:
* a preset load,
* a shift press or release (the pot now addresses a different parameter),
* engine or slot-type changes that redefine a parameter.

Without this, every preset load would jump 16 parameters to wherever the knobs happen to sit.
The OLED shows a catch indicator (which side of the value the knob is on) for any pot that is
currently out of pickup.

## I²C encoders — hub @ `0x30`, slots 1–5 (J3–J7)

Hub slot N ↔ hub connector J(N+2) ↔ panel encoder N. Slots 6–9 unpopulated.

| Slot | Encoder | Turn | Tap | Long-press (≥600 ms) | Shift + turn |
|---|---|---|---|---|---|
| 1 | **MAIN** | Engine select (wrap) | Preset browser: turn = browse, tap = load | Save-to-slot flow | **Voice mode** (Poly / Mono / Unison) |
| 2 | CHARACTER | Slot effect type | Bypass slot | — | Finer/alternate slot param |
| 3 | MOVEMENT  | Slot effect type | Bypass slot | — | Finer/alternate slot param |
| 4 | TEXTURE   | Slot effect type | Bypass slot | — | Finer/alternate slot param |
| 5 | SPACE     | Slot effect type | Bypass slot | **FREEZE toggle** (grain buffer) | Finer/alternate slot param |

* **Shift = MAIN's switch held.** Holding it and turning MAIN itself (a push-turn) selects the
  voice mode, which is why voice mode needs no button.
* **Voice mode is constrained by the engine** (concept doc, *Voice modes*). Modal is Mono
  only; String and Karplus are Mono or Unison-2. Turning MAIN onto one of those engines while
  in Poly switches the mode to Mono and posts it on the OLED, restoring the previous mode when
  a polyphonic engine is selected again. Engine order never changes — nothing is hidden or
  skipped, the instrument just says what it did.
* Encoders are incremental, so **all encoder-owned state persists in the patch/flash**
  (engine, slot types, voice mode) — same rule as rodeo-001.
* Encoder satellites take **straight** cables (`A / B / S / GND` both ends).
* Direction follows the hub's `CONFIG.ENC_DIR_INVERT`.

### Firmware polling

Per the I²C contract, a single 16-byte burst from `0x10` gets all 9 deltas plus both switch
level bytes. Shift is read from that same burst — no extra transaction. Latched press events
(`0x20/0x21`) are a separate short read, needed so a tap that starts and ends between polls is
not lost. Long-press and tap are distinguished master-side from the live level.

## OLED — SSD1309 128×64 @ `0x3C`

Feedback only, sharing I²C1/J42 with the hub. Shows: engine name, the four slot types and
their bypass states, the parameter being touched (with its value and pickup indicator), freeze
state, voice mode, preset name. Partial/low-rate refresh, interleaved with encoder polling —
a full frame at 400 kHz is ~23 ms.

## Unused platform resources

| Resource | Spare |
|---|---|
| ADC channels | 8 (ch16–23 = J22–J29) |
| Digital GPIO | all 12 (J30–J41) |
| Hub slots | 4 (slots 6–9 = J8–J11) |
| Audio IN | unused this build |

Buttons, more pots or more encoders can be added later without touching anything above.
