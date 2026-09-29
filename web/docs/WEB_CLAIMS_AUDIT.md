---
title: "Rodeo Web — Claims Audit & Accuracy Guardrail"
tags: [rodeo, web, claims, accuracy, specs]
status: active
created: 2026-09-17
updated: 2026-09-29
---

# Rodeo Web — Claims Audit & Accuracy Guardrail

**Owner:** Derek. What the website is allowed to say today, what it must not say, and the
mechanism that keeps the two apart as the hardware moves.

Every `path:line` citation below was checked against the working tree on 2026-09-17. They
are load-bearing: a discovery doc that miscites its evidence is worse than no doc.

## The rule

**No page states a product spec in prose.** Every spec statement renders from
`web/src/data/specs.ts` through a component that displays its status. If a claim is not in
that file, it does not appear on the site.

Statuses:

| Status | Means |
|---|---|
| `shipped` | Built **and** verified on real hardware |
| `in-progress` | Code or hardware exists; not verified |
| `planned` | Designed, not built |
| `not-planned` | Explicitly ruled out — kills "will it do X?" in advance |
| `retired` | Existed, then cut |

## The split that matters

The product is the **platform**; `rodeo-001` is a **reference build**
([WEB_DISCOVERY.md](./WEB_DISCOVERY.md)). This is good news for accuracy, because the
platform is the mature half and almost every unfinished thing belongs to the demo.

## The second axis: board revision

**Every board that physically exists is Rev-A.** The boards a campaign would ship are
Rev-B, and Rev-B is not a hypothetical — the repo already contains a documented change list.
So a claim has a status *and* a revision, and the site must never blur them.

Use the repo's existing vocabulary — **Rev-A / Rev-B** — not "rev1/rev2". It appears
consistently across `RODEO_HUB_I2C_CONTRACT.md`, `RODEO_CONTROL_MAP.md`,
`RODEO_ENCLOSURE_SPEC.md`, `RODEO_ADC_LIB_DESIGN.md` and both firmware READMEs. Introducing a
second scheme on the website would fork the vocabulary against a dozen existing documents.

### Two claim classes

| Class | Means | On the site |
|---|---|---|
| **`rev-a`** | True of the board that exists and was photographed | Safe to state flatly, and provable — the files are published |
| **`rev-b-intent`** | Intended for the board a backer would receive | Must be visibly marked as intent, never as fact |

Conflating these is the classic hardware-campaign failure. Every spec claim in `specs.ts`
carries `rev: 'A' | 'B' | 'A+B'`, and a `rev-b-intent` claim renders with different chrome
from a `rev-a` one.

### The documented Rev-B change list

These are already written down in the repo, and **two of them change the site's most
prominent numbers**:

| Rev-B change | Effect on site copy | Evidence |
|---|---|---|
| **3rd ADS114S08 (U5) + J50–J61 — "Path A"** | **Analog channels 24 → 36.** The hero number is rev-scoped. Forces a motherboard respin. | `RODEO_CONTROL_MAP.md:218,372,391`; `RODEO_ENCLOSURE_SPEC.md:260,317` |
| **Panel USB-C data — open decision** | If J1 D+/D− are wired to Daisy 37/36, the prominent "no USB-MIDI / no USB audio" claim **inverts**. Currently framed as "worth a decision". | `rodeo-motherboard/docs/hardware-map.md:218-222` |
| I²C pull-ups move from the hub to the motherboard | Removes the "no hub = no I²C" caveat | `RODEO_HUB_I2C_CONTRACT.md:37` |
| 10 kΩ pull-ups on both ADC CS nets | Hardening; no copy change | `RODEO_CONTROL_MAP.md:523-534` |
| Dedicated 2nd SPI header / SPI OLED option | Frees the OLED off I²C | `RODEO_ADC_LIB_DESIGN.md:83-94` |

**Consequence to act on:** do not put a bare "24" in the hero until it is decided whether the
campaign sells Rev-A or Rev-B. That is open decision 4 in
[WEB_ROADMAP.md](./WEB_ROADMAP.md). Until it is settled, the honest framing is the channel
count *of the board shown*, captioned as Rev-A.

### Imagery rule

**Every board image carries its revision in the caption.** No exceptions, including OG
images and the hero. A photograph of a Rev-A board presented as the product, when Rev-B ships
with a different channel count and possibly a live USB port, is exactly the grievance that
sinks a campaign's comment section.

Showing the prototypes is right — see [WEB_DISCOVERY.md](./WEB_DISCOVERY.md), "Prototypes are
an asset". Showing them *unlabelled* is not.

---

## Platform — `shipped`

Safe to state plainly, today. **Every claim in this table is `rev: 'A'`** — it describes the
boards that physically exist and were photographed. Two of them (the channel count, and the
USB-C limitation in the next table) have documented Rev-B successors; see the change list
above.

| Claim | Evidence |
|---|---|
| 24 analog input channels, 16-bit | 2× TI ADS114S08 on SPI1; `rodeo-motherboard/docs/hardware-map.md:76` |
| ADC conversion path proven on real silicon | SPI orientation and conversion bench-confirmed 2026-08-14 via `04_adc_1ch` / `05_adc_scan`; `rodeo-motherboard/firmware/README.md:90-92` |
| 12 digital inputs | Raw Daisy Seed GPIO, no expanders; `rodeo-motherboard/docs/hardware-map.md:114` |
| I²C control bus | `rodeo-motherboard/docs/hardware-map.md:173` |
| MIDI **in and out**, TRS | J2 in via 6N138 opto; J3 out. `rodeo-motherboard/docs/hardware-map.md:165-166` |
| Line in and line out, 3.5 mm | `rodeo-motherboard/docs/hardware-map.md:143` |
| Encoder hub: 9 encoders, one I²C target | `rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md:39` |
| Hub addresses 0x30–0x33 (4 hubs per bus) | Two cuttable jumpers; `RODEO_HUB_I2C_CONTRACT.md:35` |
| Hub firmware **running on hardware** | First bring-up on a fabbed board 2026-09-12; enumerates, serves telemetry, counts correctly alongside the OLED. `rodeo-encoder-hub/firmware/README.md:177-191` |
| I²C register contract silicon-verified | `rodeo-motherboard/firmware/README.md:94-96` (locked, silicon-verified 2026-08-20); map at `RODEO_HUB_I2C_CONTRACT.md:80-99` |
| `rodeo-potentiometer` finalized | `rodeo-001/docs/RODEO_CONTROL_MAP.md:33-36` |
| `rodeo-encoder`, `rodeo-encoder-hub` interface-verified | `rodeo-001/docs/RODEO_CONTROL_MAP.md:37-39` |
| Satellite dimensions: pot 25×30 mm, encoder 25×25 mm, button 25×25 mm | Board outlines; uniform ~25 mm panel grid |
| Motherboard mounting 95.0 × 75.0 mm, 4× M2 | Verified from `rodeo-motherboard/kicad/rodeo-motherboard.kicad_pcb` |
| 4-layer stackup, black soldermask | Board stackup declaration |
| Host is a Daisy Seed3 on the classic 40-pin footprint | `rodeo-motherboard/docs/hardware-map.md:15` |

## Platform — `not-planned`, and stated plainly

These are not omissions to bury. This audience checks specs, and burying one is the most
damaging thing the site could do.

| Claim | Status | Evidence |
|---|---|---|
| **Panel USB-C is power only** — no USB-MIDI, no USB audio, no DFU over the panel connector. The Seed's USB D+/D− dead-end at pins 37/36. Flash via the Seed's own port or SWD. | `not-planned` | `rodeo-motherboard/docs/hardware-map.md:213-221` |
| MIDI DIN and the J44 UART header **share USART1** — mutually exclusive | `not-planned` (by design) | `rodeo-motherboard/docs/hardware-map.md:182` |
| CV I/O | `not-planned` for Rev-A; ch0/ch3 (J6/J9) are spare ADC headers reserved for it | `rodeo-001/docs/RODEO_CONTROL_MAP.md:304,307` |
| `rodeo-rotary-switch` | `retired` 2026-08-20/21 | `rodeo-001/docs/RODEO_CONTROL_MAP.md:461` |
| `rodeo-switch` (SPDT), `rodeo-pixel`, `rodeo-encoder-bank` | `retired` | `rodeo-001/docs/RODEO_CONTROL_MAP.md:40,462-463` |

## Platform — `in-progress` / unverified

| Claim | Note | Evidence |
|---|---|---|
| ADS114S08 **absolute-voltage accuracy** | Conversion path is proven; absolute accuracy is still unverified pending a jumper/pot test | `rodeo-motherboard/firmware/README.md:92-93` |
| Hub I²C at 400 kHz | Boots at 100 kHz and works; the 400 kHz `TIMINGR` value has never been generated, and SDA/SCL setup-hold has never been looked at on a logic analyser | `rodeo-encoder-hub/firmware/README.md:179-183`; `RODEO_HUB_I2C_CONTRACT.md:36` |
| Motherboard board outline | Edge.Cuts not finalized — the mounting rectangle is verified, the overall outline is provisional | `rodeo-motherboard/kicad/rodeo-motherboard.kicad_pcb` |
| `rodeo-button` | Interface verified, but the switch footprint is not yet assigned | `rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md` open items |
| Rev-A bus anchoring | The hub carries the **only** bus pull-ups, so the host's I²C bus needs a hub present. Rev-B moves them to the motherboard. Say this — a builder will hit it. | `RODEO_HUB_I2C_CONTRACT.md:37` |

## Reference builds — there are now two

**Revised 2026-09-29.** When this audit was first written `rodeo-001` was the only
instrument. Since then `rodeo-002` has taken 86 commits to `rodeo-001`'s zero and is tagged
through `rodeo-002/v3.0.0`. It is the active reference build; `rodeo-001` is dormant.

This is good for the platform argument rather than awkward for it. Two instruments with
deliberately opposite philosophies — `rodeo-001` gives every parameter its own control,
`rodeo-002` accepts a single shift layer to reach far more parameters from 16 pots — running
on the same host board and the same satellites is the strongest available evidence that
Rodeo is a platform and not one synth. Say exactly that.

**Two entries below are now wrong and are corrected here:**

| Claim | Was | Now | Evidence |
|---|---|---|---|
| **Effects** | `not-planned` — "none exist anywhere in the repo" | **`in-progress` on rodeo-002.** A four-slot texture chain: CHARACTER → MOVEMENT → TEXTURE → SPACE, ending in a granular delay with Echo / Cloud / Reverse / Wash / Reverb types. | `rodeo-002/firmware/common/fx_space.h:1-8`, `rodeo-002/firmware/common/texture_chain.h` |
| **Presets** | `planned` | **`in-progress` on rodeo-002.** Patch storage, a factory bank, user slots, naming from the panel. MIDI channel and master volume are deliberately excluded from a patch. | `rodeo-002/firmware/common/preset.h:1-6`, `rodeo-002/firmware/common/factory_presets.h` |

Consequently **`preset` and `effects` come off the banned-phrase list** for rodeo-002 copy —
but they stay banned for platform and `rodeo-001` copy, which is why the banned list needs to
become per-scope rather than global when `specs.ts` is built.

Not yet re-audited, and required before any rodeo-002 page ships: the voice count (the cost
model caps it, and the bench logs sweep to 12 under the bootloader rather than 16), the engine
list, and the encoder count — the concept doc says five, a later commit adds a sixth, so the
doc lags the firmware. Do not state any of these numbers until they are checked against
`rodeo-002/docs/` and the bench logs.

## Reference build (`rodeo-001`) — mostly `planned`

`rodeo-001/firmware/src/main.cpp:1` describes itself as a **scaffold**, and its own status
block at `:18-27` lists what is wired vs stubbed.

| Claim | Status | Evidence |
|---|---|---|
| 12-voice polyphony, MIDI-driven | `shipped` (in the scaffold) | `main.cpp:49` (`kMaxVoices = 12`), `main.cpp:113` |
| Two independent ADSRs and a per-voice SVF | `shipped` | `main.cpp:102-104` (`Svf svf; Adsr filt_env; Adsr amp_env;`) |
| 22 pots → params; global LFO → cutoff | `shipped` | `main.cpp:19-21` — **adjacency exception, see below** |
| Four subtractive waveforms (saw / pulse / triangle / sine) | `shipped` | `main.cpp:60-63` — **adjacency exception, see below** |
| **FM oscillator engine** | `planned` | `main.cpp:64` reads `kFm, // Timbre = FM index (TODO — falls back to sine)` |
| **Formant oscillator engine** | `planned` | `main.cpp:65` reads `kFormant, // Timbre = formant position (TODO — falls back to saw)` |
| **Mono / unison voice modes** | `planned` | `main.cpp:165`, `main.cpp:519` (`// TODO: mono/unison allocation`) |
| **OSC2 hard-sync** | `planned` — needs both the hub read and the DSP | `main.cpp:166` |
| **Encoder-hub reads in the rodeo-001 app** | `in-progress` — so detune, glide, octave and live model-select do not work yet | `main.cpp:521-524` |
| Vowel / formant filter | `planned` | `main.cpp:18-27` |
| QSPI preset persistence | `planned` | `main.cpp:18-27` |
| OLED feedback | `planned` | `main.cpp:521-524` |
| **Effects of any kind** | `not-planned` | None exist anywhere in the repo |
| Enclosure envelope ~380–420 × 190–220 mm | `planned`, render as **"(draft)"** | `rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md:4` (`status: draft`), `:439` |
| Panel aesthetic (Cassette Futurism, two-layer acrylic) | `planned` — still "proposed, for Derek's nod", colour pairing open | `rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md:474-491` |

Note the demonstrated poly examples (`06_poly_synth`, `09_poly_synth_oled`) are **6-voice**;
the only 12-voice code is the scaffold. Don't conflate them.

## Never on the site

| Phrase | Why |
|---|---|
| **`MiniFreak`** | A competitor's product name, used throughout the internal docs as voice shorthand. It must never reach public copy. |
| Any **price**, including "TBA" | `specs.ts` has no price field at all. An early wrong number anchors the campaign badly, and platform pricing depends on kit composition, which is undecided. |
| `pre-order` | Not a thing that exists |
| `open source` **before the LICENSE files land** | Openness is decided; the files are not written. Gate the words on the files existing. |
| **Bumble, Honey, Fizz** | Agent personas in the engineering docs, not team credits |
| `USB MIDI`, `USB audio` | Panel USB-C is power only |
| `unison`, `mono mode`, `hard sync` | Planned on `rodeo-001`, not built |
| `preset`, `effects` | **Scope-dependent.** Banned for platform and `rodeo-001` copy; permitted for `rodeo-002`, where both now exist — see "Reference builds — there are now two". The banned list must become per-scope when `specs.ts` is built. |

## Sourcing caveat — two docs disagree

`rodeo-motherboard/docs/hardware-map.md:186-188` still carries
**"### 1. CRITICAL — SPI MOSI/MISO are swapped; the MCU cannot talk to the ADCs"** with
"Fix before fab". This is **stale**. `rodeo-001/docs/RODEO_CONTROL_MAP.md:21-29` records the
issue as resolved and bench-verified pin-by-pin, and it is proven in silicon because
`04_adc_1ch` and `05_adc_scan` read live data on both converters.

**Where the two disagree, the control map wins.** This is also worth fixing in the hardware
map itself — an open-source platform whose own docs contain a CRITICAL warning that is no
longer true will cost a reader an afternoon.

## The guardrail to build

Specified here; implemented in a later phase. Five checks in `web/scripts/check-claims.mjs`.

1. **Evidence exists.** Every claim carries a non-empty `evidence` path; it resolves from the
   repo root; if `path:line`, that line exists. (This is why CI sparse-checkout includes
   `rodeo-001/**` and `rodeo-motherboard/**` — see
   [WEB_ARCHITECTURE.md](./WEB_ARCHITECTURE.md).)
2. **TODO proximity.** Any `shipped` claim whose evidence line — or a line within ±3 — contains
   `TODO`, `stub`, `DRAFT` or `not implemented` **fails the build**. This is the sharp end:
   mark the FM engine `shipped` while `main.cpp:64` still says `(TODO — falls back to sine)`
   and CI fails with the offending line quoted.

   **The adjacency exception, found by running this rule by hand over this very document.**
   Three of the citations above tripped it, correctly by the letter and wrongly in substance:
   `main.cpp:19-21` is the `DONE :` half of a status block whose `TODO :` half begins at
   line 22, and `main.cpp:60-63` is the shipped half of an oscillator-model enum whose next
   two entries are the `planned` FM and formant engines. In both cases shipped and unshipped
   items are *deliberately* adjacent, which is good code and a false positive for this check.

   Two of the four original citations were simply lazy and got retargeted to real code
   (`kMaxVoices = 12`, the per-voice `Svf`/`Adsr` members) — which is the check working as
   intended, since better evidence existed and I hadn't looked for it. The other two have no
   better evidence available. So the implementation needs a per-claim
   `evidenceAdjacency: "<reason>"` field: present, the check passes and prints the reason;
   absent, it fails. An explicit written acknowledgement per exception, never a blanket
   suppression and never a silent pass — otherwise the check decays into noise that gets
   switched off.
3. **Evidence drift.** A committed `scripts/claims.lock.json` stores a hash of each evidence
   line's text. If the source line changes, the check fails with *"evidence for `osc-fm`
   changed — re-verify and re-run `npm run claims:accept`."* This forces a human re-read
   whenever firmware moves under a claim. **Do not cut this check** — without it `specs.ts`
   becomes a third stale doc within a month and the guardrail becomes theatre that licenses
   bad copy.
4. **Banned phrases in built HTML.** Grep `dist/**/*.html` for the table above, with
   per-page allowlists and a written reason in `scripts/claims.config.json`. `open source`
   is permitted only when a root `LICENSE` exists. **This is the check that catches real
   drift**, because drift happens in hand-written prose, not in data files.
5. **Cross-references.** Every `relatedSpecs` id in every log post resolves to a real claim.
6. **Revision scope.** Every claim carries `rev`. A claim marked `rev: 'A'` whose subject
   appears in the Rev-B change list above must also carry `supersededBy` naming the Rev-B
   claim, and the renderer shows both. A `rev-b-intent` claim rendered without intent chrome
   fails the build. This is the check that keeps a Rev-A photograph and a Rev-B spec from
   ending up in the same sentence.

### Rendering

`<SpecTable group="…">` for tables, `<Claim id="…" />` inline, `<ClaimLegend />` once per
page. Badge styling comes from the board's own knockout chip — gold fill for `shipped`,
white outline for `in-progress`, grey outline for `planned`, struck through for `retired`
(see [WEB_BRAND.md](./WEB_BRAND.md)). Each claim's `updated` date renders, so staleness is
visible to visitors; public pressure outlasts a lint rule.

And `<BuildProgress>`: *"N of M spec lines verified on hardware."* Generated from the data,
so it cannot lie, it moves as the project moves, and it is genuinely good build-in-public
copy. That component is why this mechanism is worth building rather than merely tolerating.
