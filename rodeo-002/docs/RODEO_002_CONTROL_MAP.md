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
>
> **Channel map revised 2026-09-21 (Derek):** pots split 8 + 8 across both ADC chips for a
> faster scan. The panel is unchanged; only which motherboard header each pot plugs into moved.

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

## Analog — 16 pots, 8 per ADC chip (J6–J13, J18–J25)

> Wiring it? [`RODEO_002_HARNESS.md`](RODEO_002_HARNESS.md) is the build sheet:
> the same map as a plug-in checklist, with cable types and what to look for on
> the screen as each run goes in.

**Rows 1–2 are on U3, rows 3–4 are on U4, eight pots each** (revised 2026-09-21). The two chips
convert in parallel, so the scan rate is set by whichever carries more channels: 8 per chip
reads each pot every ~9.3 ms, **~108 Hz per pot** at the platform's default 1000 SPS
(`RODEO_ADC_LIB_DESIGN.md` §4: 1.16 ms per hop). The earlier 12 + 4 split ran U3 at 12 hops,
~72 Hz, while U4 sat mostly idle.

Within each chip the grid is read left-to-right, top-to-bottom onto consecutive inputs.
**Spare: ch8–11 (J14–J17) and ch20–23 (J26–J29)**, plus all 12 digital inputs.

| `ch` | Grid | Main layer | Shift layer | Chip | AINx | Conn |
|---|---|---|---|---|---|---|
| 0  | R1C1 | Harmonics         | Env→Timbre               | U3 | AIN0 | J6  |
| 1  | R1C2 | Timbre            | Env→Morph                | U3 | AIN1 | J7  |
| 2  | R1C3 | Morph             | LFO rate                 | U3 | AIN2 | J8  |
| 3  | R1C4 | **Character amt** | Character **Bias**       | U3 | AIN3 | J9  |
| 4  | R2C1 | Cutoff            | Filter type *(stepped)*  | U3 | AIN4 | J10 |
| 5  | R2C2 | Resonance         | LFO depth                | U3 | AIN5 | J11 |
| 6  | R2C3 | **Filter env**    | **LFO dest** *(stepped)* | U3 | AIN6 | J12 |
| 7  | R2C4 | **Movement amt**  | Movement **Rate**        | U3 | AIN7 | J13 |
| 12 | R3C1 | Attack            | Spread                   | U4 | AIN0 | J18 |
| 13 | R3C2 | Decay *(= release)* | Glide                  | U4 | AIN1 | J19 |
| 14 | R3C3 | Sustain           | Master volume *(global)* | U4 | AIN2 | J20 |
| 15 | R3C4 | **Texture amt**   | Texture **Tone**         | U4 | AIN3 | J21 |
| 16 | R4C1 | Space Time        | Grain pitch              | U4 | AIN4 | J22 |
| 17 | R4C2 | Space Feedback    | Grain position spread    | U4 | AIN5 | J23 |
| 18 | R4C3 | Space Grain       | Tilt EQ                  | U4 | AIN6 | J24 |
| 19 | R4C4 | **Space Mix**     | Space **Damp**           | U4 | AIN7 | J25 |

* **Alias footgun, both chips:** AIN6/AIN7 are also REFP1/REFN1, and ch6/7 and ch18/19 use
  them. They are correct as inputs only while `REFSEL=00` — never select reference 1. (AIN8–11,
  the GPIO aliases, are now unused.)
* Shift-layer labels are printed smaller on the panel. Labeled ≠ hidden. Column 4's shift
  labels are **Bias / Rate / Tone / Damp** — each true for every effect type in that slot
  (concept doc, *Effect types*), which is what makes them printable.
* **Two shift positions are enums, read as stepped pots** (the platform already has stepped-pot
  logic in `pot_ui.h`): **Filter type** (LP / BP / HP) and **LFO dest**. The destinations, in the
  order the pot steps through them: **Pitch · Timbre · Morph · Cutoff · Amp · Space mix ·
  Harmonics · Resonance · Character** (2026-09-24). **New ones go on the end** — the value is
  stored in presets by index, so inserting in the middle would silently re-point every saved
  patch. Space mix and Character modulate a chain slot rather than a voice and are applied once
  per audio block (`common/lfo_dest.h`); the rest are applied inside each voice.
* **Master volume is global**: never stored in a preset, so a preset load cannot move it
  (Derek, 2026-09-21). It moved behind SHIFT on 2026-09-24, freeing a main-layer pot — and **it
  now waits for pickup like every other pot**. It used to be exempt, which was right while it
  sat on the main layer where that knob's position *was* the volume; behind SHIFT the same knob
  belongs to Sustain, so going live the instant SHIFT went down jumped the output level to
  wherever Sustain happened to be sitting.

### Revision, 2026-09-24 — three changes that paid for each other

**The envelope is ADS, Moog style: Decay sets the fall time whether or not the key is still
held.** That is one knob instead of two for the same expressive range, and it puts the whole
envelope — Attack, Decay, Sustain — on the **main** layer, where it had been split across both.
`Patch` keeps a `release` field, moved in step by the panel, so the stored preset format is
unchanged and a later firmware could separate them again without a version bump.

**Spread moved to the shift layer.** Not a taste judgement: `VoiceManager` passes a detune of
zero in Poly and Mono and only spreads voices in **Unison**, so the knob is inert in normal
playing. That is the profile of a shift-layer parameter, not a main one. (If Spread should do
something polyphonic — per-voice detune or stereo placement — that is a separate change, and it
would earn its way back.)

**Master volume moved behind SHIFT**, and the freed slots went to **Sustain** and **Filter env**
— the latter promoted out of the shift layer onto R2C3, beside Cutoff and Resonance, so the
whole filter is one row. **Glide** takes the last free shift slot; it was previously reachable
only by loading a preset.
* **Some parameters live on MAIN's settings pages, not the panel** — the set-once-per-patch
  ones: filter keytrack, glide time, LFO shape, velocity→filter amount and pitch-bend range.
  Keytrack gave its panel position to LFO destination, which is played with rather than set.
  See *Settings pages* under the OLED section.
* Pot wiring: 3-pin JST-SH, `1=+3V3, 2=wiper, 3=GND` at the motherboard. **These runs need
  REVERSED cables** — the satellite is pinned `GND / signal / supply-in`. See the harness
  section of rodeo-001's control map; a straight cable makes every pot read backwards.

### Pickup (mandatory, both layers)

A pot is **inert until its position crosses the parameter's stored value**, after:
* a preset load,
* a shift press or release (the pot now addresses a different parameter),
* engine or slot-type changes that redefine a parameter,
* a gesture loop stopping or being cleared (parameters stay where the loop left them).

Master volume is exempt — see above. Stepped pots (filter type, LFO dest) catch when the pot's
step matches the stored step.

Without this, every preset load would jump 16 parameters to wherever the knobs happen to sit.
The OLED shows a catch indicator (which side of the value the knob is on) for any pot that is
currently out of pickup.

## I²C encoders — hub @ `0x30`, slots 1–5 (J3–J7)

Hub slot N ↔ hub connector J(N+2) ↔ panel encoder N. The encoder column is slots 1–4, top to
bottom; the isolated encoder under the OLED is slot 5. Slots 6–9 unpopulated.

| Slot | Panel | Turn | Tap | Long-press (≥600 ms) | Shift + turn |
|---|---|---|---|---|---|
| 1 | R1C5 CHARACTER | Type: Warm / Drive / Fuzz / Fold / Rectify | Bypass slot | — | Low cut |
| 2 | R2C5 MOVEMENT  | Type: Chorus / Vibrato / Phaser / Flanger / Tremolo | Bypass slot | — | LFO shape |
| 3 | R3C5 TEXTURE   | Type: Crush / Downsample / Tape / Dust / Squash | Bypass slot | — | Per-type variant |
| 4 | R4C5 SPACE     | Type: Echo / Cloud / Reverse / Wash / Reverb | Bypass slot | **FREEZE** | Grain-pitch quantize |
| 5 | R4C6 **MAIN**  | Engine select (wrap), or the **value** of whatever NAV has highlighted | **Gesture recorder**: arm · close loop · clear | — *(nothing: a hold is shift)* | **Voice mode** |
| 6 | R3C6 **NAV**   | **Move the selection** (opens the menu from the normal screen) | **Enter / confirm** | **Back**, one level | — |

* **Shift = MAIN's switch held.** MAIN sits at the far right, so shift is held with one hand
  while the other sweeps pots. Holding it and turning MAIN itself (a push-turn) selects the
  voice mode, which is why voice mode needs no button.
* **MAIN's press resolves on release, and only if nothing else was touched.** Shift is a *hold*,
  and so is a long-press — without this rule, holding shift for more than 600 ms while reaching
  for a pot would fire the long-press action. On release: if any pot or encoder moved while MAIN
  was down, the press was a shift and does nothing; otherwise it is a tap (< 600 ms) or a
  long-press (≥ 600 ms). **"Moved" means DELIBERATELY moved** — 3% of travel from where the pot
  sat when the press began, not pass-to-pass movement. On hardware 0.3% of dither was enough to
  disqualify a press and silently swallow a save (2026-09-24). Measuring from the start of the
  press means a slow turn still counts while dither never does. Once a clean hold passes 600 ms the OLED shows what releasing will do,
  so the pending action is never invisible. Slot encoders 1–4 are never shift, so their own
  long-press (Freeze) can fire at the threshold without waiting for release.
* **Voice mode is constrained by the engine** (concept doc, *Voice modes*), **built
  2026-09-25**. String and Karplus are Mono or Unison only (Modal is gone). Turning MAIN onto
  one of them while in Poly switches the mode to Mono and posts it on the OLED, restoring the
  previous mode when a polyphonic engine is selected again. Push-turning the mode on such an
  engine **skips past Poly** rather than offering something that would be ignored. Engine order
  never changes — nothing is hidden or skipped, the instrument just says what it did.
* Encoders are incremental, so **all encoder-owned state persists in the patch/flash**
  (engine, slot types, voice mode) — same rule as rodeo-001.
* Encoder satellites take **straight** cables (`A / B / S / GND` both ends).
* Direction follows the hub's `CONFIG.ENC_DIR_INVERT`.

### Two brain encoders (2026-09-29)

A sixth encoder turned up, and it is what let every gesture mean one thing. **NAV moves, MAIN
changes.** That is the whole rule, and it holds in every mode:

| | NAV (E6, above MAIN) | MAIN (E5) |
|---|---|---|
| Turn | Move the selection | Change the highlighted **value** |
| Tap | Enter / confirm | Gesture recorder: arm · close · clear |
| Hold | **Back**, one level | SHIFT (+ turn = voice mode) |

NAV's hold fires *at* the threshold rather than on release, because unlike MAIN it has no shift
to be told apart from and waiting would only make going back feel slow.

Three things fall out of it. **Hold-to-edit is gone** — a settings page is turned with MAIN, not
held. **The Close menu entry is gone** — NAV's hold is the way out. And **the gesture recorder is
always one press away**, whatever is on screen, which matters because closing a loop is the only
action on this instrument that has to land on a beat.

**NAV is hub slot 6 (connector J8)**, the first of the four the control map used to leave empty.

### The menu (revised 2026-09-24, rebuilt on NAV 2026-09-29)

**MAIN's hold means SHIFT and nothing else.** It used to *also* resolve on release as a
long-press that armed the gesture recorder, told apart from a shift only by whether anything
else had been touched — an invisible rule at the moment it mattered, and wrong in both
directions: hesitate while holding shift and you armed the recorder; graze a pot while arming
and nothing happened. Derek, 2026-09-24: *"that feels confusing and easy to mess up."*

Everything now hangs off MAIN's **tap**, so the only press rule left is **tap acts, hold
shifts**.

| In the menu | Does |
|---|---|
| Turn NAV | Move the selection (wraps; it remembers where you left it) |
| Tap NAV | Enter the highlighted entry |
| Hold NAV | Back — to the menu from a list, to playing from the menu |
| Touch any pot or slot encoder | Close, doing nothing |

**Menus are vertical lists filling the screen**: an inverted title bar with a counter, five rows
of 6×8, the selection inverted, and a scroll bar when there are more than five. Before this each
list showed *one* entry at a time, so turning was blind — you could not see what was above or
below and had to memorise the order. That was most of what "difficult and inconsistent to
navigate" meant (Derek, 2026-09-29).

**The menu does not time out.** It used to close itself after five seconds, which is precisely
the moment it should not — it went away while the player was still deciding (2026-09-25).
Nothing is lost by leaving it open: any pot or slot encoder still closes it, so it can only
persist while the panel is untouched.

Entries: **Load preset · Save preset · Patch settings · Global settings · Factory reset** —
five, which is exactly what fits without scrolling. Gesture left the menu for MAIN's tap; Close
left it for NAV's hold.

**Factory reset is the one entry that asks before it acts.** Its page starts on `NO`; turning
moves it to `YES`, and only then does a tap do anything. A hold could not have been the
confirmation, because a hold is shift. It clears presets, gestures and instrument state, and
deliberately keeps the panel wiring map. Load and Save being separate is what let the
browser's long-press-to-save go away — inside either one, a tap acts and that is all. The
settings pages (*Patch* and *Global*, below) join this menu when they are built.

### Preset browser (decided 2026-09-21, built 2026-09-24, re-homed under the menu 2026-09-24)

Tapping MAIN opens it, and **while it is open MAIN's controls mean something else** — the OLED
says so for as long as it is open:

| In the browser | Does |
|---|---|
| Turn MAIN | Browse slots |
| Tap MAIN | **Act** on the highlighted slot — load or overwrite, whichever entry you came in by — and close |
| Touch any other control | **Close without loading** |

**The idle timer stops while MAIN is held.** A press is activity: without this, pausing to
decide which slot to save to let the browser close under a finger that was already holding the
encoder down, and the long-press then meant *gesture* instead of *save* (2026-09-24).

**As built:** **24 slots — sixteen factory sounds and eight empty** for your own (v2.0.0). An empty slot shows
`-- empty --` and will not load, but can be saved into — so saving means choosing a factory
sound to replace, and there is no restore gesture yet.

**Saving names the slot first** (built 2026-09-25). After choosing the slot, a tap goes to the
name: **turn spells the character under the cursor, hold MAIN and turn moves along the name,
and a tap saves** — the same two axes a settings page uses, so naming needed no gesture of its
own. It starts from what the slot is already called, so renaming is an edit rather than
spelling it out again; an empty slot starts from `USER n`, and a name spelled down to nothing
comes back as that. Upper case only: at six pixels a character the difference between `l` and
`I` is not worth the extra turning. A saved slot is named `User N` — naming needs a text entry this
panel cannot offer. Loading drops pickup on every pot, so nothing jumps.

There is no long-press here or anywhere else: you arrive from **Load preset** or **Save
preset**, the screen says which, and a tap does that one thing.

### Gesture recorder (decided 2026-09-21)

Records pot movements and loops them — the Chroma Console's gesture idea. **Built 2026-09-24**
(`common/gesture.h`), reached from the menu.

| Gesture entry, when… | Does |
|---|---|
| idle | **Arm.** Recording starts on the first pot movement, so there is no rush. |
| armed | **Cancel**, rather than record nothing. |
| looping | **Stop and clear.** |

**While a take is recording, MAIN's tap closes the loop** rather than opening the menu — that is
the one timing-critical action the instrument has, and it should not need a menu. The menu is
therefore unreachable while recording; close the loop first. The header shows `ARM`, `REC` or
`LOOP` in place of the voice mode for as long as a take is running, so the state is never
hidden.

* **Loop end is trimmed to the press-DOWN moment.** A long-press resolves ≥ 600 ms after the
  press starts (and MAIN resolves on release — see above), so closing the loop at resolution
  would make every loop end late. The firmware timestamps the press-down and cuts there.
* **Pots only, both layers.** Encoders change effect types and engines; looping discrete jumps
  like those reads as a malfunction, not a gesture.
* **Grabbing a pot that is in the loop takes that parameter back** from the loop for the rest
  of playback, so you can play over a gesture without stopping it.
* **Up to 16 s, sampled at the pot scan rate** — ~108 Hz with the 8 + 8 channel split.
  Recording faster than the pots are read would only store repeated samples.
* Gestures **are saved with presets** (Derek, 2026-09-21; built 2026-09-24): a preset with a
  gesture starts looping the moment it loads, including the one loaded at power-up. Clearing a
  loop affects only the live state — the stored preset keeps its gesture until it is saved
  again, and saving a slot with no gesture running clears the stored one.

### Firmware polling

Per the I²C contract, a single 16-byte burst from `0x10` gets all 9 deltas plus both switch
level bytes. Shift is read from that same burst — no extra transaction. Latched press events
(`0x20/0x21`) are a separate short read, needed so a tap that starts and ends between polls is
not lost. Long-press and tap are distinguished master-side from the live level.

## OLED — SSD1309 128×64 @ `0x3C`

Feedback only, sharing I²C1/J42 with the hub. Partial/low-rate refresh, interleaved with
encoder polling — a full frame at 400 kHz is ~23 ms.

**Layout as built (2026-09-24):**

| Rows | Holds |
|---|---|
| 0–8 | **Preset name**, `*` if the patch has changed since it was loaded · voice mode in three letters · voices in use. **Inverted while SHIFT is held**, and the name is replaced by `TURN: MODE` — holding MAIN and turning it chooses the voice mode, and that is the only moment the hint applies. |
| 11–18 | The four slot types, bypassed ones struck through. **The Space cell inverts when frozen.** |
| 20–62 | The body: scope, readout and meter — **see below** |

**Nothing is held in reserve at the bottom.** There used to be a text line for shift, freeze and
messages, empty in almost every frame while the waveform was squeezed into eighteen rows. Those
moved onto the things they describe — shift inverts the header, freeze inverts the Space cell —
and messages float over the scope for the second or so they last. The waveform got the space:
27 rows where it had 18, the same height whether or not a readout is under it. The row positions
are named constants with `static_assert`s between them, because every one of 64 rows is spoken
for and an overlap is a bug that only shows up on hardware.

**The engine is not on the header.** The preset name is the more useful thing to have in front
of you, and the engine announces itself as a message when you turn MAIN onto it.

**The scope is almost always on**, so you can watch the waveform change shape as you turn a
knob. It takes the whole readout area when nothing else is on screen, and a strip above the
value when something is. It is normalised to whatever region it is given — a quiet patch reads
as well as a loud one — triggered on a rising zero crossing so a held note stands still, and
clamped to its box, because the gain trails the signal and the display's coordinates are
unsigned: an unclamped overshoot wraps and paints a band across the screen. All of that lives
in `common/scope.h`, where it is tested.

**The readout is not always a meter.** What a parameter *is* decides how it is shown:

| When | Shows |
|---|---|
| A continuous pot | Scope strip (rows 20–46), then its name and **the value as a percentage** (48), and the meter (57–62). Pickup puts `UP`/`DN` in front of the number — the value shown is the one being chased, not the one the pot is at. |
| A **stepped** pot (filter type, LFO destination) | Scope strip, then `Name: Choice` spelled out. A bar says nothing about *which* destination. |
| An **envelope** pot (attack, decay, sustain, release) | **The whole ADSR shape** — the scope stands aside, because the envelope is the picture worth seeing |
| **Nothing, for over a second** | The scope alone. **The rows below stay reserved**, so the waveform is always the same height — a scope that resizes as a popup comes and goes is harder to read than a smaller one that holds still. |

**Two redraw paces, because the two things change for different reasons.** Anything the panel
did redraws as soon as it happens, up to ~25 fps — that is a knob feeling responsive. The scope
moves constantly on its own, so *by itself* it redraws at ~10 fps. Turning a knob therefore
costs no more bus than it already did: the frame was going out anyway, and a frame is the same
1024 bytes whatever is drawn in it. Only a scope moving under a still panel is rate-limited.

**There is no CPU meter.** It was a bring-up instrument and the serial log still prints the
measured load beside the predicted one, which is where that comparison belongs.

**A pot must move ~0.6% of its travel AND hold it for ~20 ms to count as moved at all**,
measured against the value it last wrote rather than the previous sample. The time matters as
much as the distance: the ADCs deliver a channel ~108 times a second, so one bad conversion
persists across many passes of the main loop and no amount of counting passes would catch it —
it showed up as parameters changing on screen that nobody had touched. A pot already in motion
keeps responding immediately, so a turn never feels stepped. Without that, a pot resting within pickup range of
its stored value catches, then writes its own dither into the patch for ever — enough to mark a
preset modified with nobody touching the panel. Measuring against the last write is what lets a
slow turn still land.

### Settings pages (built 2026-09-25)

Two pages, reached from the menu. The split between them is what a preset load may and may not
touch:

| Page | Holds | Saved |
|---|---|---|
| **Patch** | Filter keytrack · velocity→filter amount · pitch-bend range · LFO shape · **mod wheel destination** | with each preset |
| **Global** | **MIDI in channel · MIDI out channel** | to its own flash region, never to a preset |

**Turn to choose the line, hold MAIN and turn to change it** — the same push-turn that picks the
voice mode, so a page needs no edit mode of its own and the one press rule still holds. A tap
leaves; there is nothing to confirm, because the change has already happened. Anything else
touched closes the page, as does five seconds of nothing.

Glide is not on the Patch page: it has a pot of its own on the shift layer (R3C2), and two
owners for one value is worse than either. Pot calibration is not there either — there is no
calibration any more (see the harness doc); the wiring map is cleared by holding MAIN through
power-up.

**The MIDI channels persist** in the instrument-state region at `0x7A0000`, beside the boot
preset — global state belongs with global state. **In** can be Omni; **out** cannot, because
something has to be on the wire.

**The mod wheel is a destination now** (2026-09-25): LFO depth · Cutoff · Timbre · Morph · Space
mix · Character. LFO depth is the default and is what the wheel always did. It is stored in a
byte of the preset format that used to be padding, so presets written before it existed read as
LFO depth rather than as nonsense — no version bump, nothing lost. New destinations go on the
END of the enum, same as the LFO's.

Master volume is also global but lives on its pot. **MIDI channel must never be in a preset** —
loading one could otherwise leave the synth listening on a channel the keyboard isn't sending
(Derek, 2026-09-21).

## Unused platform resources

| Resource | Spare |
|---|---|
| ADC channels | 8 (ch8–11 = J14–J17, ch20–23 = J26–J29) |
| Digital GPIO | all 12 (J30–J41) |
| Hub slots | 4 (slots 6–9 = J8–J11) |
| Audio IN | unused this build |

Buttons, more pots or more encoders can be added later without touching anything above.
