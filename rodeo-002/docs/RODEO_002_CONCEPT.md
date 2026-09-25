---
title: "rodeo-002 — Instrument Concept"
tags: [rodeo, rodeo-002, instrument, concept, granular, plaits, daisy-seed]
status: draft
created: 2026-09-20
updated: 2026-09-22
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
character. The engine list, **measured on hardware 2026-09-20** (budget section below).
"Voices" is the largest count that stayed under 70% audio load **with the texture chain
running**. Poly mode caps at 8 regardless — a higher number here is headroom, not voices.

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
| Karplus string | DaisySP `String` | 14.0% | **Mono · Unison-2** |
| String voice | DaisySP `StringVoice` | 16.4% | **Mono · Unison-2** |
| ~~Modal~~ | ~~DaisySP `ModalVoice`~~ | ~~46.2%~~ | **DROPPED** |
| Wavetable | *deferred* — would port from Plaits (MIT) | unmeasured | — |
| Chord | *deferred* — would port from Plaits (MIT) | unmeasured, ~4–6 osc/voice | — |

**Wavetable and Chord are deferred** (Derek, 2026-09-20): not ported for now, so the shipping
engine list is the twelve measured above.

**MODAL IS DROPPED** (Derek, 2026-09-22): at 46% for a single voice it could not run at all
beside an expensive Space type, and designing around it was not worth it. The DSP stays in
DaisySP if it is ever worth optimising back in. **String and Karplus remain, as monophonic
engines** — they are struck and plucked models that suit monophonic playing anyway. That
leaves **eleven engines**, and no combination that fails to fit.

Per-voice costs above are the 2026-09-22 hardware fit (raw ticks, post-optimisation); they
are the numbers [`cost_model.h`](../firmware/common/cost_model.h) uses.

Most of the list is already vendored in `rodeo-motherboard/firmware/DaisySP` — these are the
Plaits-derived modules Émilie Gillet contributed upstream, so the "Plaits-like" feel is mostly
a matter of *driving* them from common macros rather than porting new DSP. Anything still
missing comes from the MIT-licensed Mutable source.

**Per voice, after the engine:** a DaisySP `Svf` (Derek chose a real multimode filter over a
Plaits-style low-pass gate), **one ADSR** and one LFO.

**One envelope, shared** (decided 2026-09-21). The single ADSR drives the amp directly, and
the filter, Timbre and Morph through their amount controls (Filter env amount, Env→Timbre,
Env→Morph). That is what the panel has controls for — four envelope stages, not eight — and
it is how the platform's `06_poly_synth` example works.

**Voice modes:** Poly (8, or 6 on Vosim/Z-osc) · Mono (legato + glide) · Unison (stacked,
detuned by Spread).

**Mono-only engines (decided 2026-09-20).** Modal, String and Karplus cost too much per voice
to be polyphonic, so each engine declares which voice modes it supports:

| Engine class | Poly | Mono | Unison |
|---|---|---|---|
| Everything 8-voice or better | 8 *(see below)* | yes | up to 4 |
| Vosim, Z-osc | 6 | yes | up to 4 |
| String, Karplus | — | yes | 2 only |

**The voice outranks the voice count** (Derek, 2026-09-21). FM, Harmonic and Grainlet run 8
voices at 66–69% in the plain build, 63.5–64.7% under the bootloader — close to the ceiling
either way, with ~5 points of room for the finished voice. If the finished voice (LFO, voice management, any
richer-voice additions) pushes one of them past 70%, **that engine drops to 6 voices** rather
than the voice being held back for it. Each engine's poly count is therefore set from the
finished firmware's measured load, not from the benchmark table.

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

### Effect types (decided 2026-09-21)

Chain order is **fixed: Character → Movement → Texture → Space**. Derek approved the lists
below as the starting set, to be revisited during implementation if something doesn't earn
its place.

**Each slot's secondary means one thing across all its types.** The shift layer is printed on
the locked panel, so "Character secondary" has to become a word that is true for every
Character type — otherwise the label could not be silkscreened and the OLED would have to
explain it. The discrete option is shown on the OLED, so it may vary.

| Slot | Amount pot | Secondary *(printed)* | Option *(shift + encoder turn, OLED)* |
|---|---|---|---|
| **Character** | Drive | **Bias** — offset into the shaper, odd ↔ even harmonics | Low cut before the drive: off / 100 Hz / 300 Hz |
| **Movement** | Depth | **Rate** | LFO shape: sine / triangle / square / random |
| **Texture** | Intensity | **Tone** — darker ↔ brighter after the effect | Per type (below) |
| **Space** | Mix | **Damp** — darkening in the feedback path | Grain-pitch quantize: free / semitones / fifths + octaves / octaves |

**CHARACTER** — waveshaping

| Type | What it is | Source |
|---|---|---|
| Warm | Gentle soft saturation — the "always on" sweetener | new, trivial (tanh) |
| Drive | Classic overdrive | DaisySP `Overdrive` |
| Fuzz | Hard clip; high Bias makes it gated and sputtery | new, small |
| Fold | Wavefolder; Bias gives asymmetric folds | DaisySP `Wavefolder` |
| Rectify | Octave-up fuzz; Bias blends half-wave into full-wave | new, trivial |

**MOVEMENT** — modulation

| Type | What it is | Source |
|---|---|---|
| Chorus | Stereo chorus | own, on DaisySP `DelayLine` |
| Vibrato | Pitch wobble only, no dry | own, on DaisySP `DelayLine` |
| Phaser | Swept all-pass, 6 stages with feedback | own |
| Flanger | Short delay with feedback | own, on DaisySP `DelayLine` |
| Tremolo | Amplitude; stereo LFO offset makes it autopan | own |

DaisySP's chorus, flanger and phaser each run a fixed internal LFO, so **all five are driven
from our own LFO** — that is what makes the LFO-shape option uniform. (As implemented; the
original plan had named the DaisySP classes.)

**TEXTURE** — degradation

| Type | What it is | Option | Source |
|---|---|---|---|
| Crush | Bit-depth reduction | smooth / hard steps | DaisySP `Decimator` |
| Downsample | Sample-rate reduction, aliasing | — | DaisySP `SampleRateReducer` |
| Tape | Wow and flutter, saturation, hiss | slow wow / fast flutter | new: modulated delay |
| Dust | Vinyl crackle and noise bed | vinyl / radio | own (xorshift RNG, not `rand()`) |
| Squash | Compressor, up to audible pumping | soft / hard / pump | own |

**SPACE** — the granular block. All types use Time / Feedback / Grain / Mix, grain pitch and
spread; **Freeze works in every type**.

| Type | What it does | Time | Feedback | Grain |
|---|---|---|---|---|
| Echo | Granular delay; small grains sound like a clean echo, large ones smear | delay time | repeats | grain size |
| Cloud | Grains scattered across the last *Time* seconds of the buffer | window | regeneration | density |
| Reverse | Echoes play backwards | delay time | repeats | segment length |
| Wash | Grains feed a diffuser — an ambient, reverb-like tail | size | regeneration | diffusion |
| Reverb | A plain reverb | pre-delay | decay | **shimmer** — pitched grains fed into the tail |

**Reverb is DaisySP-LGPL's `ReverbSc`** (Derek, 2026-09-21). Its controls map naturally:
Feedback is `SetFeedback`, Damp is `SetLpFreq`, and Freeze holds the tail indefinitely. Grain
turns the grain engine into a shimmer: grains pitched by the grain-pitch pot, fed back into the
reverb. It is the only type that runs the granular engine *and* a reverb together, so it is
the one to watch on the budget.

**Two rules the feedback types have to obey** — both learned from hardware on 2026-09-23, where
long decays "oversaturated and the audio eventually cut out, coming back when you lower the
settings":

* **No DC goes into a feedback loop.** The voices carry a small offset (~-0.04 measured, which
  is ordinary for asymmetric and sync'd oscillators) and every Space type feeds itself back —
  ReverbSc's DC gain is near 1/(1 - 0.99) = 100x at full decay, the granular types' about 20x.
  The offset walks the output onto the limiter and holds it at a constant -1.0, which has no AC
  left in it; since the Seed's line out is AC-coupled, that is *heard as silence*. Space blocks
  DC on its effect input and again on the reverb's input, where the shimmer loop makes its own.
  The dry path is deliberately not blocked, so the chain stays transparent at zero.
* **A longer decay must not be a louder reverb.** A feedback network's steady-state level goes
  as 1/sqrt(1 - decay), so the reverb's input trim tracks the decay instead of being fixed. The
  decay knob then lengthens the tail without acting as a volume control.

**Measured on hardware, 2026-09-21, re-measured 2026-09-22 after optimisation.** Load added by
each type at its heaviest settings, over an all-bypassed baseline, under the bootloader.
Measurement note: these figures come from raw timer ticks. libDaisy's `CpuLoadMeter` reported
0.0 for every case in `bench_space` (cause unexplained); in `bench_voices` it agrees with raw
ticks to within 0.2 points, so the voice numbers it produced stand.

| Slot | Types, cheapest → costliest (% of CPU) |
|---|---|
| Character | Drive 1.7 · Fold 1.9 · Fuzz 2.2 · Rectify 2.7 · Warm 3.0 |
| Movement | Tremolo 1.4 · Vibrato 3.4 · Chorus 3.6 · Flanger 3.6 · Phaser 5.2 |
| Texture | Downsample 1.3 · Crush 1.9 · Squash 2.4 · Dust 2.6 · Tape 6.5 |
| Space | Reverse 5.3 · Echo 7.8 · Cloud 11.0 · Wash 11.7 · **Reverb 20.6** |

Space's figures are **after the grain-engine optimisation of 2026-09-22** (Echo −23%,
Cloud −21%; the log before it is
[`bench_voices-2026-09-21-fx.log`](bench_voices-2026-09-21-fx.log), after it
[`bench_voices-2026-09-22-optimised.log`](bench_voices-2026-09-22-optimised.log)).

**Costs add:** the worst-case chain (Warm + Phaser + Tape + Reverb) measured **35.4%**, against
35.3% summed — so any configuration's cost can be predicted from this table. That is still
**~3× the 12.9% the benchmark's stand-in chain cost**, and Space is most of it: Reverb alone
costs more than the other three slots combined, because **ReverbSc is 11.6 of its 20.6**
(profile: [`bench_space-2026-09-22.log`](bench_space-2026-09-22.log) — ReverbSc 11.6, grains
~5.0, Space's own per-sample overhead ~5.2). A lighter in-house reverb is the one remaining
optimisation of any size; it is **not** done.

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

### Re-measured under the bootloader, 2026-09-21

The instrument runs as `BOOT_SRAM` (see *Firmware platform*), so the sweep was repeated in
that mode — log: [`bench_voices-2026-09-21-bootsram.log`](bench_voices-2026-09-21-bootsram.log).
**Every number carries over or improves:** across 125 paired measurements the load is 1.4
points lower on average (−3%), ranging from 5.6 points better to 1.6 worse. The chain is
unchanged at 12.9%. The gains concentrate in the engines with the most per-voice state — FM,
Formant, Grainlet — consistent with voice state moving to zero-wait-state DTCM in this mode.

The table above keeps the original plain-build figures, so it slightly *overstates* cost in
the shipping configuration. No engine changes voice class: Karplus now fits 4 voices at 68.6%,
a hair under the line, which doesn't matter because it is Mono/Unison-2 by design.

### What this means for the design

* **8 voices is the architecture** — not 6, not 12. Only the two VA engines and Particle
  could do more, and designing around a count that a third of the engine list cannot reach
  would make a worse instrument.
* **There is real headroom at 8 voices** on the cheaper engines: 8 VA voices plus the chain
  is ~39%. That is better spent on a *richer voice* — a second oscillator, a better filter, a
  second LFO — than on voices 9 through 16. **Cost anything per-voice against FM, Harmonic
  and Grainlet**, not VA: they are the ones at the ceiling, and they drop to 6 voices if it
  pushes them past it (see *Voice modes*).
* **70% is the ceiling, deliberately.** The benchmark ran with no pot scanning, no encoder
  polling, no OLED and no MIDI. Audio runs in interrupt context and the UI runs in what is
  left, so that 30% remainder is the UI's entire budget — a full OLED frame at 400 kHz is
  ~23 ms of I²C by itself.
* **Wavetable and Chord are deferred, and so unmeasured.** If revisited: Chord stacks 4–6
  oscillators per voice, so expect roughly 4 voices; Wavetable should be cheap.

## MIDI performance mapping (decided 2026-09-21)

A MIDI-keyboard-only instrument, so the keyboard's own controls matter as much as the panel's:

| Source | Destination | Adjust |
|---|---|---|
| Velocity | Amp level, always; filter by a per-patch amount | Patch settings: velocity→filter |
| Pitch bend | Pitch, **±2 semitones** default | Patch settings: bend range |
| Mod wheel (CC1) | **Adds** to LFO depth | — |
| Channel aftertouch | Morph | — |
| Sustain pedal (CC64) | Holds notes | — |

Glide time and LFO shape also sit on the patch-settings page — they have no panel position.

### The real chain changes the voice budget (2026-09-21)

With the real effects, the worst-case chain costs **36.8%**, not 12.9%. Voices at the 70%
ceiling with that chain running (log above; bootloader build):

| Engine | Stand-in chain | Real worst-case chain |
|---|---|---|
| Virtual analog · Variable saw · Particle · Formant | 12+ | 8–10 |
| FM · Harmonic · Grainlet | 8 | **4** |
| Vosim · Z-osc | 6 | **2** |
| Modal | 1 | **0** — does not fit |

A *light* chain is a different story: Warm + Chorus + Crush + Echo costs ~16%, and FM keeps 8
voices under it. The cap is what makes the difference visible instead of audible.

**Polyphony is capped from the cost table** (Derek, 2026-09-21), implemented in
[`cost_model.h`](../firmware/common/cost_model.h) and pinned to the hardware logs by host
tests — it predicts measured load within 2 points. The worst case is reachable by any player,
and an overload is an audible dropout, not a slowdown — so the firmware computes a safe voice
count from the measured costs whenever the engine or a slot's type changes, and the OLED
always shows it ("FM · 6 voices"). **The voice count can therefore change when a Space type
changes.** With Modal dropped, **every remaining engine fits beside every Space type**, so
there is no unplayable combination to rule on.

### Engine quirk worth knowing: virtual-analog Timbre at morph 0.5

`VariableShapeOscillator` crossfades two waveshapes, and **at exactly morph 0.5 the pulse width
cancels out** — measured on the host, bit-identical output for pulse widths 0.2 and 0.8, while
morph 0 or 1 gives ~0.85 relative divergence from the same change. Timbre drives pulse width on
that engine, so at the midpoint the Timbre knob does nothing whatsoever. It is the oscillator's
behaviour rather than a bug in the mapping, but it is invisible and it makes a knob look broken,
so the **Init** preset sits at morph 0.35 rather than 0.5.

## Envelope — ADS (2026-09-24)

One envelope drives amp and filter, and it has **three controls, not four**: Attack, Decay and
Sustain, with **Decay setting the fall time whether or not the key is still held** (Moog style,
Derek's call). The expressive range is the same in practice — a separate release is rarely set
to something very different from the decay — and it buys back a panel slot, which is what let
the whole envelope come onto the main layer and Filter env come out of the shift layer.

`Patch` and `StoredPreset` keep a `release` field, moved in step with decay by the panel and by
CC. Nothing reads it: `VoiceManager` takes the release time from `decay`. It is retained so the
stored format is byte-identical, which means no version bump and no saved preset lost; a later
firmware could separate the two again the same way.

**Two rules about starting a voice**, both learned the hard way on 2026-09-24, when Decay at
zero made the synth "somewhat randomly" stop responding to keypresses in a way no preset change
could clear:

* **Start a voice by retriggering its envelope, never by the gate alone.** DaisySP's `Adsr`
  restarts on a rising edge (`gate && !gate_`), and there is no edge when a voice is re-used for
  the same note or stolen while it is still held -- its gate was already high. The envelope then
  continues from wherever it was, which is inaudible with a long decay and *silent* with a short
  one.
* **A voice is finished only once it has been released and freed**, not merely when it is silent
  and idle. With sustain at zero the envelope reaches IDLE while the key is still down, so a
  note-off that skips the voice means the `Adsr` never sees the falling edge and its internal
  gate flag stays true for ever -- after which no note-on has a rising edge and the voice is
  dead until reset. A negative note number is the marker for properly wound down; rendering one
  more block of silence is what earns it.

Mono is exempt from the first rule on purpose: it is legato, so a new note over a held one
changes pitch without restarting the envelope. `MonoOn` therefore writes the voice directly
rather than going through `Start()`.

## Preset system

Patches in the Daisy's QSPI flash: engine, all pot values (both layers), all four slot types
and their parameters, voice mode, the patch-settings page, **and the gesture loop if one was
recorded**. Browsing and saving live on the main encoder (see the control map).

**Not in presets** (Derek, 2026-09-21): **MIDI channel** and **master volume** are global. A
preset that stored the MIDI channel could leave the synth deaf to the keyboard, and one that
stored volume would jump the output level on load. Pot calibration is global too.

**Gestures save with presets** (Derek, 2026-09-21), so a preset can move by itself: it starts
its loop when loaded. Size is the only cost: gestures sample at the pot scan rate (~108 Hz —
see the control map), so a full 16-second, 16-pot gesture is ~55 KB, and 64 presets with full
gestures would take ~3.5 MB of the QSPI (which also holds the program image — see *Firmware
platform*). Storing only the pots that actually moved makes a
typical gesture far smaller.

**The instrument boots into the preset it was left on**, as STORED — unsaved edits are
discarded (Derek, 2026-09-24), so power-up is always a known-good sound and the header's `*` is
what tells you the patch has drifted since. Slot 1 if nothing has been chosen yet. The slot is
written to its own sector a few seconds after it changes, never the instant a preset is chosen:
the write is a 64 KB erase.

**Built 2026-09-24** (`common/preset.h`, `common/factory_presets.h`): **16 slots, all sixteen
shipping a factory sound**, browsed and loaded from MAIN exactly as the control map describes.
Saving therefore means replacing one, and there is no restore gesture yet — worth adding, along
with more slots, before the bank starts feeling crowded.

**New factory sounds reach an instrument that has already been saved to by filling its empty
slots at boot** (`FillEmptySlots`). The store's defaults are only written while it has never
been used, so after a first save a later firmware's bank would otherwise never be seen. Nothing
already in a slot is touched, whether factory or yours. The one thing it cannot tell is a slot
deliberately emptied from one never filled — there is no gesture for emptying a slot, so that
cannot arise today, but it is a constraint on adding one. What is
not built yet: **gestures** (the recorder itself does not exist) and **naming** — a panel with
no buttons cannot spell, so a saved slot names itself `User N` until there is a UI that can.
Sixteen slots rather than sixty-four because that is what the browser can turn through
comfortably; the sector holds far more when gestures need it.

**Engines are stored by a stable id, never by enum position.** Dropping an engine renumbers
every engine after it, which would silently turn every saved patch into a different sound. Each
engine has a number assigned once in `preset.h` and never reused; a preset naming an engine the
firmware no longer has loads as virtual-analog **and says so**, rather than pretending. Every
other stored enum is range-checked on load, so an old or corrupt bank cannot produce a patch
that indexes past a table. No factory preset uses Grainlet, Particle, String or Karplus, so
dropping those (under consideration, 2026-09-24) orphans nothing.

**LFO rate: 0.05–40 Hz** (2026-09-24). It is ticked once per block, so the block rate is its
sample rate — initialised at the audio rate instead, it ran 48× slow and a nominal 20 Hz
measured 0.45 Hz on the host. Saved presets keep their stored 0–1 knob position, so their LFOs
now run at the speed they were always meant to.

**Physical-position problem:** pots hold their last physical position, so after a preset load
(and after every shift-layer release) the panel disagrees with the patch. Every pot therefore
uses **pickup** — a pot is inert until it crosses the stored value. This applies to the main
layer too, not just the shift layer.

## Firmware platform (decided 2026-09-21)

**The program runs from SRAM via the Daisy bootloader (`APP_TYPE = BOOT_SRAM`), not from
internal flash.** The Seed's internal flash is 128 KB, and `bench_voices` alone — twelve
engines and the chain, with no MIDI, UI, OLED, ADC driver, presets or gestures — already uses
89.7% of it at libDaisy's default `-O2`. The instrument cannot fit there.

Under `BOOT_SRAM` the bootloader copies the program from QSPI into ~480 KB of AXI SRAM at boot
and runs it from there, which is also fast memory, so DSP cost should carry over. Consequences:

* **Zero-initialised globals go to the 128 KB DTCM, not the 480 KB SRAM** — and DTCM also holds
  the stack. This is the real memory budget. `bench_voices` could not link its 16 voices there
  (overflow ~5 KB) and runs 12 in that mode, at 87% of DTCM. The instrument's 8 voices fit
  comfortably, but **any large buffer must be placed explicitly in SDRAM**
  (`DSY_SDRAM_BSS`): the grain buffer already is, and **the live gesture buffer (~55 KB) and
  preset staging must be too**. DTCM is zero-wait-state, so keeping voice state there is a
  speed win rather than a compromise.

* **Install the bootloader once** (`make program-boot`). After that, updates are `make
  program-dfu` into the bootloader: tap RESET, then tap BOOT while the LED pulses to hold it
  in DFU indefinitely. The BOOT button no longer has to be held through a reset.
* **Program image, calibration and presets share the 8 MB QSPI** (7.75 MB usable above the
  bootloader's reserved region). Plenty for all three. The map so far, with erases at 64 KB so
  each tenant gets its own 64 KB boundary:

  | Offset | What |
  |---|---|
  | `0x000000`–`0x03FFFF` | Reserved by the bootloader — do not touch |
  | `0x040000`– | Program image (an SRAM build cannot exceed 480 KB) |
  | `0x780000` | **Panel wiring map** (`PanelPots::Calibration`, ~12 B — which analog runs were built straight) |
  | `0x790000` | **Preset bank** (`PresetBank`, 16 slots, 2.6 KB) |
  | `0x7A0000` | **Instrument state** (the preset slot to boot into) |
  | `0x7B0000`– | Free — where gestures go |

  It lives far past the program on purpose: a reflash rewrites only the image, so what is stored
  survives a firmware update as well as a power cycle. **Pot travel is not stored, because it is
  not measured** — the ranges are fixed per wiring and inset by a dead zone (see the harness doc,
  *Bringing it up*). Learning them was tried on 2026-09-24 and removed: a learned endpoint is
  only as good as the worst sample ever taken, and persisting it made one bad sample permanent.
* **Re-run `bench_voices` under `BOOT_SRAM` once** (`make APP_TYPE=BOOT_SRAM`) to confirm the
  benchmark numbers carry over before relying on them in that mode.
* **Licence: the rodeo-002 firmware is always open source** (Derek, 2026-09-21), so the LGPL
  half of DaisySP (`ReverbSc`, `Compressor`, …) is fair game. Enable it with
  `USE_DAISYSP_LGPL = 1` in the app Makefile; the library is already built in the vendored
  tree (`DaisySP-LGPL/build/libdaisysp-lgpl.a`).
* **The enclosure needs access to the Daisy's own micro-USB** — updates and the debug log both
  go through it, and the panel USB-C is power-only. Derek is designing this into the case.

## Open questions

1. ~~Gesture recorder~~ **RESOLVED (Derek, 2026-09-21):** in, on MAIN's long-press, saved with
   presets — see the control map. It costs well under 1% CPU; control space was the constraint.
2. ~~The three physical models miss the 6-voice minimum~~ **RESOLVED (Derek, 2026-09-20):**
   keep them, restricted to Mono/Unison — see Voice modes above.
3. ~~Panel layout~~ **LOCKED (Derek, 2026-09-20):** 4×4 pots, a column of 4 slot encoders, MAIN
   isolated under the OLED, 30 × 35 mm pitch — see the control map.
4. ~~Texture-slot effect lists~~ **RESOLVED (Derek, 2026-09-21):** see *Effect types* — 5 / 5 /
   5 / 5, printed secondaries Bias / Rate / Tone / Damp, fixed chain order. Every type still
   needs a cost measurement.
5. MIDI CC in/out map for the pots.
6. ~~The real chain's cost~~ **RESOLVED (Derek, 2026-09-22):** grain engine optimised
   (Space types −14 to −23%), worst chain now 35.4%; polyphony cap implemented in
   `cost_model.h`; Modal dropped, which removes the only unplayable combination. **Deferred:**
   replacing ReverbSc with a lighter in-house reverb (it is 11.6 of Reverb's 20.6) — worth
   ~2 voices on the heavy engines if Reverb patches ever feel too tight.
