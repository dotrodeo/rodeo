---
title: "rodeo-002 Control Map — Interface Contract"
tags: [rodeo, rodeo-002, control-map, pinout, ads114s08, encoder-hub, panel]
status: active
created: 2026-09-20
updated: 2026-09-21
---

# rodeo-002 Control Map — Interface Contract

Source of truth for **which physical control does what** on rodeo-002. Firmware binds exactly
these channels and slots; the panel must physically fit this. Sibling doc:
[`RODEO_002_CONCEPT.md`](RODEO_002_CONCEPT.md).

Platform facts referenced here come from
[`rodeo-motherboard/docs/hardware-map.md`](../../rodeo-motherboard/docs/hardware-map.md) and
[`RODEO_HUB_I2C_CONTRACT.md`](../../rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md); the
channel convention `ch = chip·12 + AINindex` is the platform's, shared with rodeo-001.

> **Status: 🔒 PANEL LAYOUT LOCKED (Derek, 2026-09-20).** The grid, the 30 × 35 mm pitch, and
> the pot-channel and encoder-slot assignments below are frozen — firmware and harness build to
> them. Nothing is wired yet. Changes from here need an explicit decision, not a drift.

## Surface

**16 pots + 5 encoders (each with a push switch) + 1 OLED = 22 controls. No buttons.**

Consequences of having no buttons:
* **Shift is a held encoder press**, not a latch. The hub exposes live switch level in
  `SW_STATE_LO` (§5 of the I²C contract), so "held right now" is directly readable.
* Every mode, preset and voice-mode action is a press, a long-press or a press-and-turn.

## Panel geometry

```
    col1    col2    col3    col4  │ col5    col6
  ┌───────────────────────────────────────────────────────┐
  │   (o)     (o)     (o)     (o) │  (E1)     ┌─────────┐  │  row 1
  │   (o)     (o)     (o)     (o) │  (E2)     │  OLED   │  │  row 2
  │   (o)     (o)     (o)     (o) │  (E3)     └─────────┘  │  row 3
  │   (o)     (o)     (o)     (o) │  (E4)   (E5)           │  row 4
  └───────────────────────────────────────────────────────┘
     └──── the synth ────┘ └── the chain ──┘ └─ the brain ─┘
```

Three blocks, and the grouping is the point:
* **Columns 1–3 are the synth** — voice, filter, envelope, and the granular block's own
  parameters.
* **Column 4 plus the encoder column are the texture chain** — one slot per row, its amount
  pot beside the encoder that picks its effect type, reading top-to-bottom in signal order.
* **The OLED and the isolated MAIN encoder (E5) are the brain** — engine, presets, voice mode,
  the gesture recorder, and every readout.

Every encoder is physically adjacent to the one pot it pairs with, and nothing is modal.

### Mechanical (verified against the satellite KiCad, 2026-09-20)

| Board | Outline | Shaft | Cable |
|---|---|---|---|
| `rodeo-potentiometer` | 25 × 30 mm | 10.0 mm from left edge, 16.6 mm from top | JST-SH, **bottom** edge, exits in-plane |
| `rodeo-encoder` | 25 × 25 mm | 10.0 mm from left, 16.2 mm from top | JST-SH, **bottom** edge |

* The shaft is off-centre (10 mm left / 15 mm right) **identically on both board types**, so
  boards in a common orientation tile exactly and the pot and encoder columns line up.
* **Pitch: 30 × 35 mm** (Derek, 2026-09-20). Boards collide below 25 × 30 mm, so this leaves
  ~5 mm of gap both ways. The in-plane cable exit needs only a few millimetres before it can
  bend back behind the board plane, so 5 mm below each board is enough. **Rows 1–3's cables
  must pass the rows below them**, so 37–38 mm vertical is easier to harness if the panel
  proportions still look right — not required.
* **The motherboard (100 × 80 mm) and `rodeo-encoder-hub` (45 × 45 mm) mount directly to the
  3D-printed case**, not to the panel (Derek, 2026-09-20). Only the 21 control satellites and the
  OLED hang on the panel, so the harness is the only thing crossing between the two.
* The Alps RK09K pot and EC11E encoder bushings are the same size — one hole diameter serves
  all 21 (verify against the parts in hand).

## Analog — 16 pots on ADC channels 0–15 (J6–J21)

Physical order is the grid read left-to-right, top-to-bottom, mapped 1:1 onto consecutive
channels. **Channels 16–23 (J22–J29) stay spare**, as do all 12 digital inputs.

| `ch` | Grid | Main layer | Shift layer | Chip | AINx | Conn |
|---|---|---|---|---|---|---|
| 0  | R1C1 | Harmonics         | Env→Timbre               | U3 | AIN0  | J6  |
| 1  | R1C2 | Timbre            | Env→Morph                | U3 | AIN1  | J7  |
| 2  | R1C3 | Morph             | LFO rate                 | U3 | AIN2  | J8  |
| 3  | R1C4 | **Character amt** | Character secondary      | U3 | AIN3  | J9  |
| 4  | R2C1 | Cutoff            | Filter type *(stepped)*  | U3 | AIN4  | J10 |
| 5  | R2C2 | Resonance         | Filter env amount        | U3 | AIN5  | J11 |
| 6  | R2C3 | Spread / detune   | **LFO dest** *(stepped)* | U3 | AIN6  | J12 |
| 7  | R2C4 | **Movement amt**  | Movement secondary       | U3 | AIN7  | J13 |
| 8  | R3C1 | Attack            | Decay                    | U3 | AIN8  | J14 |
| 9  | R3C2 | Release           | Sustain                  | U3 | AIN9  | J15 |
| 10 | R3C3 | Master volume     | LFO depth                | U3 | AIN10 | J16 |
| 11 | R3C4 | **Texture amt**   | Texture secondary        | U3 | AIN11 | J17 |
| 12 | R4C1 | Space Time        | Grain pitch              | U4 | AIN0  | J18 |
| 13 | R4C2 | Space Feedback    | Grain position spread    | U4 | AIN1  | J19 |
| 14 | R4C3 | Space Grain       | Tilt EQ                  | U4 | AIN2  | J20 |
| 15 | R4C4 | **Space Mix**     | Space secondary          | U4 | AIN3  | J21 |

* Shift-layer labels are printed smaller on the panel. Labeled ≠ hidden.
* **Two shift positions are enums, read as stepped pots** (the platform already has stepped-pot
  logic in `pot_ui.h`): **Filter type** (LP / BP / HP) and **LFO dest**. Keep the destination
  list short — Pitch / Timbre / Morph / Cutoff / Amp / Space mix — so the steps stay readable.
* **Filter keytrack is not on the panel.** It moved to MAIN's patch-settings page because it
  is set once per patch, unlike LFO destination, which is played with. That trade is what made
  room for LFO dest, and keytrack is the only parameter demoted to a menu.
* Pot wiring: 3-pin JST-SH, `1=+3V3, 2=wiper, 3=GND` at the motherboard. **These runs need
  REVERSED cables** — the satellite is pinned `GND / signal / supply-in`. See the harness
  section of rodeo-001's control map; a straight cable makes every pot read backwards.

### Pickup (mandatory, both layers)

A pot is **inert until its position crosses the parameter's stored value**, after:
* a preset load,
* a shift press or release (the pot now addresses a different parameter),
* engine or slot-type changes that redefine a parameter,
* a gesture loop stopping or being cleared (parameters stay where the loop left them).

Without this, every preset load would jump 16 parameters to wherever the knobs happen to sit.
The OLED shows a catch indicator (which side of the value the knob is on) for any pot that is
currently out of pickup.

## I²C encoders — hub @ `0x30`, slots 1–5 (J3–J7)

Hub slot N ↔ hub connector J(N+2) ↔ panel encoder N. The encoder column is slots 1–4, top to
bottom; the isolated encoder under the OLED is slot 5. Slots 6–9 unpopulated.

| Slot | Panel | Turn | Tap | Long-press (≥600 ms) | Shift + turn |
|---|---|---|---|---|---|
| 1 | R1C5 CHARACTER | Slot effect type | Bypass slot | — | Slot secondary |
| 2 | R2C5 MOVEMENT  | Slot effect type | Bypass slot | — | Slot secondary |
| 3 | R3C5 TEXTURE   | Slot effect type | Bypass slot | — | Slot secondary |
| 4 | R4C5 SPACE     | Slot effect type | Bypass slot | **FREEZE** (grain buffer) | Slot secondary |
| 5 | R4C6 **MAIN**  | Engine select (wrap) | Preset browser: turn = browse, tap = load, long-press = save to this slot | **GESTURE** — arm / close loop / clear | **Voice mode** |

* **Shift = MAIN's switch held.** MAIN sits at the far right, so shift is held with one hand
  while the other sweeps pots. Holding it and turning MAIN itself (a push-turn) selects the
  voice mode, which is why voice mode needs no button.
* **MAIN's press resolves on release, and only if nothing else was touched.** Shift is a *hold*,
  and so is a long-press — without this rule, holding shift for more than 600 ms while reaching
  for a pot would fire the long-press action. On release: if any pot or encoder moved while MAIN
  was down, the press was a shift and does nothing; otherwise it is a tap (< 600 ms) or a
  long-press (≥ 600 ms). Once a clean hold passes 600 ms the OLED shows what releasing will do,
  so the pending action is never invisible. Slot encoders 1–4 are never shift, so their own
  long-press (Freeze) can fire at the threshold without waiting for release.
* **Voice mode is constrained by the engine** (concept doc, *Voice modes*). Modal is Mono
  only; String and Karplus are Mono or Unison-2. Turning MAIN onto one of those engines while
  in Poly switches the mode to Mono and posts it on the OLED, restoring the previous mode when
  a polyphonic engine is selected again. Engine order never changes — nothing is hidden or
  skipped, the instrument just says what it did.
* Encoders are incremental, so **all encoder-owned state persists in the patch/flash**
  (engine, slot types, voice mode) — same rule as rodeo-001.
* Encoder satellites take **straight** cables (`A / B / S / GND` both ends).
* Direction follows the hub's `CONFIG.ENC_DIR_INVERT`.

### Gesture recorder (decided 2026-09-21)

Records pot movements and loops them — the Chroma Console's gesture idea. It lives on **MAIN's
long-press** because it is a global performance feature and belongs on the brain encoder, not
on a texture slot. **Save moved into the preset browser** to make room (long-press on the
highlighted slot while browsing), which is where you are already choosing a slot anyway.

| MAIN long-press, when… | Does |
|---|---|
| idle | **Arm.** Recording starts on the first pot movement, so there is no rush. OLED: `REC`. |
| recording | **Close the loop** and start playing it back. |
| looping | **Stop and clear.** |

* **Loop end is trimmed to the press-DOWN moment.** A long-press resolves ≥ 600 ms after the
  press starts (and MAIN resolves on release — see above), so closing the loop at resolution
  would make every loop end late. The firmware timestamps the press-down and cuts there.
* **Pots only, both layers.** Encoders change effect types and engines; looping discrete jumps
  like those reads as a malfunction, not a gesture.
* **Grabbing a pot that is in the loop takes that parameter back** from the loop for the rest
  of playback, so you can play over a gesture without stopping it.
* **Up to 16 s, sampled at the pot scan rate.** That is ~72 Hz per channel with the platform's
  default 1000 SPS ADC rate — set by U3, which carries 12 of the 16 pots (ch0–11) while U4
  carries 4; the two chips convert in parallel, so the busier one sets the pace
  (`RODEO_ADC_LIB_DESIGN.md` §4). Recording faster than the pots are read would only store
  repeated samples.
* Gestures **are saved with presets** (Derek, 2026-09-21): a preset
  with a gesture starts looping when it loads. Clearing a loop affects only the live state — the
  stored preset keeps its gesture until it is saved again.

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

Its one menu page is **patch settings**: filter keytrack, MIDI channel, bend range and the
other set-once parameters. Reached from MAIN.

## Unused platform resources

| Resource | Spare |
|---|---|
| ADC channels | 8 (ch16–23 = J22–J29) |
| Digital GPIO | all 12 (J30–J41) |
| Hub slots | 4 (slots 6–9 = J8–J11) |
| Audio IN | unused this build |

Buttons, more pots or more encoders can be added later without touching anything above.
