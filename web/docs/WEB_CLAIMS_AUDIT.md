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
`web/src/data/boards.ts` through a component that displays its status. If a claim is not in
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
([WEB_DISCOVERY.md](https://github.com/dotrodeo/rodeo-internal/blob/main/web/docs/WEB_DISCOVERY.md)). This is good news for accuracy, because the
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

Conflating these is the classic hardware-campaign failure. Every spec claim in `boards.ts`
carries `rev: 'A' | 'B' | 'A+B'`, and a `rev-b-intent` claim renders with different chrome
from a `rev-a` one.

### Rev-B: what the campaign actually sells

**Rewritten twice on 2026-10-15, and the second time is the one to trust.** The
first rewrite was done against the stale `rev-b.md` that used to sit in
`rev-a/rodeo-motherboard/docs/`, which described a USART2 chain and two on-board ADCs.
(Named without a full path on purpose: the file is deleted, and a resolvable
citation to it would now fail the very check below.) The
real document is `hardware/rodeo-motherboard/docs/rev-b.md`, 767 lines, which came
onto `main` with the Rev-B merge. The stale copy is deleted. **If you are reading
a Rev-B fact that is not cited to a `hardware/` path, distrust it.**

**Status: designed and ready to order** (`hardware/rodeo-motherboard/docs/rev-b.md:3`) — the
motherboard and the six Rev B daughterboards are drawn, routed and DRC-clean, and were
uploaded to JLC on 2026-10-16 but not yet ordered. None has been built or bench-tested yet, so **every
Rev-B capability is `planned`.**

Rev-B is not a revision of the Rev-A motherboard. It is a different division of
labour, and almost every number on the site changes:

| | Rev-A, which the site describes today | **Rev-B, which the campaign sells** | Evidence |
|---|---|---|---|
| Analog inputs | 24 × 16-bit, two on-board ADS114S08 | **The ADCs are gone.** Analog is the Seed's own ADC across 8 of the 10 general-purpose I/O connectors | `hardware/rodeo-motherboard/docs/rev-b.md:606-610`, `hardware/rodeo-motherboard/docs/rev-b.md:81` |
| Where the ADCs went | on the motherboard | an **optional SPI expander board** — and ~$19 of parts per board left with them | `hardware/rodeo-motherboard/docs/rev-b.md:218` |
| What the freed pins do | — | a **second codec on SAI2: 4 in / 4 out audio** | `hardware/rodeo-motherboard/docs/rev-b.md:328` |
| General-purpose I/O | 12 digital | **10**, each digital or analog; two of them are the DACs | `hardware/rodeo-motherboard/docs/rev-b.md:81` |
| Growing past that | respin the motherboard | **satellites on a USART3 chain** — position is the address, 1 Mbaud, JST SH 5-pin, self-closing ring via a `PRESENT` pin | `hardware/rodeo-motherboard/docs/rev-b.md:131`, `hardware/rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md:32-33`, `hardware/rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md:55` |
| Satellite brain | — | **CH32V003F4P6** chosen 2026-10-14 | `hardware/rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md:89` |
| Panel USB-C | power only; no USB-MIDI, no USB audio | **data, through the power board** — USB-MIDI, USB audio and DFU flashing | `hardware/rodeo-motherboard/docs/rev-b.md:304` |
| MIDI | on the motherboard | **a daughterboard** | `hardware/rodeo-motherboard/docs/rev-b.md:430` |
| I²C | no on-board pull-ups; the hub is the bus anchor | pull-ups on the motherboard, **4 taps** | `hardware/rodeo-motherboard/docs/rev-b.md:242` |
| Storage | none | **internal SD**, 1-bit SDMMC, loaded over USB | `hardware/rodeo-motherboard/docs/rev-b.md:260` |

**The boards that now exist as designs**, none yet fabricated:

| Board | What it is | Evidence |
|---|---|---|
| `rodeo-pot-bank-4` | Four 9 mm pots on 25 mm centres, one CH32V003, four chain slots | `hardware/rodeo-pot-bank-4/README.md` |
| `rodeo-encoder-bank-4` | Four 12 mm encoders with push switches, same geometry | `hardware/rodeo-encoder-bank-4/README.md` |
| `rodeo-audio-3.5mm` | Four stereo 3.5 mm jacks for the eight audio signals | `hardware/rodeo-audio-3.5mm/README.md` |
| `rodeo-power-usbc` | Default power input, and the USB data path | `hardware/rodeo-power-usbc/README.md` |
| `rodeo-power-barrel` | The same plus a 9–12 V barrel jack | `hardware/rodeo-power-barrel/README.md` |
| mounting system | Rear rail and motherboard frame, both printed | `hardware/mounting/README.md` |

**Consequence to act on.** The campaign sells Rev-B (settled 2026-10-15), so the
site is about hardware that has not been built. Three rules follow, and they are
the whole difficulty of this phase:

1. **Every photograph is Rev-A** and stays captioned as such. `BoardFigure`
   enforces this; do not add an override. A Rev-A photo beside a Rev-B spec is
   honest only while the caption says so.
2. **No Rev-B capability is published as a capability.** 4-in/4-out audio,
   USB-MIDI, the SD card and the satellite chain are all `planned`. The
   temptation on a crowdfunding page is to write the finished spec sheet; that
   is the exact failure this audit exists to prevent, and it matters more here
   than anywhere earlier, because the reader is being asked for money.
3. **The old numbers must go, not soften.** "24 × 16-bit analog in" is not a
   smaller truth about Rev-B, it is a fact about a board that is not for sale.
   Leaving it on the site would be the single most misleading thing here.

### Imagery rule

**Every board image carries its revision in the caption.** No exceptions, including OG
images and the hero. A photograph of a Rev-A board presented as the product, when Rev-B ships
with a different channel count and possibly a live USB port, is exactly the grievance that
sinks a campaign's comment section.

Showing the prototypes is right — see [WEB_DISCOVERY.md](https://github.com/dotrodeo/rodeo-internal/blob/main/web/docs/WEB_DISCOVERY.md), "Prototypes are
an asset". Showing them *unlabelled* is not.

---

## Platform — `shipped`

Safe to state plainly, today. **Every claim in this table is `rev: 'A'`** — it describes the
boards that physically exist and were photographed. Two of them (the channel count, and the
USB-C limitation in the next table) have documented Rev-B successors; see the change list
above.

| Claim | Evidence |
|---|---|
| 24 analog input channels, 16-bit | 2× TI ADS114S08 on SPI1; `rev-a/rodeo-motherboard/docs/hardware-map.md:76` |
| ADC conversion path proven on real silicon | SPI orientation and conversion bench-confirmed 2026-08-14 via `04_adc_1ch` / `05_adc_scan`; `rev-a/rodeo-sdk/README.md:133-134` |
| 12 digital inputs | Raw Daisy Seed GPIO, no expanders; `rev-a/rodeo-motherboard/docs/hardware-map.md:114` |
| I²C control bus | `rev-a/rodeo-motherboard/docs/hardware-map.md:173` |
| MIDI **in and out**, TRS | J2 in via 6N138 opto; J3 out. `rev-a/rodeo-motherboard/docs/hardware-map.md:165-166` |
| Line in and line out, 3.5 mm | `rev-a/rodeo-motherboard/docs/hardware-map.md:143` |
| Encoder hub: 9 encoders, one I²C target | `rev-a/rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md:39` |
| Hub addresses 0x30–0x33 (4 hubs per bus) | Two cuttable jumpers; `RODEO_HUB_I2C_CONTRACT.md:35` |
| Hub firmware **running on hardware** | First bring-up on a fabbed board 2026-09-12; enumerates, serves telemetry, counts correctly alongside the OLED. `rev-a/rodeo-encoder-hub/firmware/README.md:177-191` |
| I²C register contract silicon-verified | `rev-a/rodeo-sdk/README.md:137-138` (locked, silicon-verified 2026-08-20); map at `RODEO_HUB_I2C_CONTRACT.md:80-99` |
| `rodeo-potentiometer` finalized | `instruments/rodeo-001/docs/RODEO_CONTROL_MAP.md:33-36` |
| `rodeo-encoder`, `rodeo-encoder-hub` interface-verified | `instruments/rodeo-001/docs/RODEO_CONTROL_MAP.md:37-39` |
| Satellite dimensions: pot 25×30 mm, encoder 25×25 mm, button 25×25 mm | Board outlines; uniform ~25 mm panel grid |
| Motherboard mounting 95.0 × 75.0 mm, 4× M2 | Verified from `rev-a/rodeo-motherboard/kicad/rodeo-motherboard.kicad_pcb` |
| 4-layer stackup, black soldermask | Board stackup declaration |
| Host is a Daisy Seed3 on the classic 40-pin footprint | `rev-a/rodeo-motherboard/docs/hardware-map.md:15` |

## Platform — `not-planned`, and stated plainly

These are not omissions to bury. This audience checks specs, and burying one is the most
damaging thing the site could do.

| Claim | Status | Evidence |
|---|---|---|
| **Panel USB-C is power only** — no USB-MIDI, no USB audio, no DFU over the panel connector. The Seed's USB D+/D− dead-end at pins 37/36. Flash via the Seed's own port or SWD. | `not-planned` | `rev-a/rodeo-motherboard/docs/hardware-map.md:213-221` |
| MIDI DIN and the J44 UART header **share USART1** — mutually exclusive | `not-planned` (by design) | `rev-a/rodeo-motherboard/docs/hardware-map.md:216` |
| CV I/O | `not-planned` for Rev-A; ch0/ch3 (J6/J9) are spare ADC headers reserved for it | `instruments/rodeo-001/docs/RODEO_CONTROL_MAP.md:304,307` |
| `rodeo-rotary-switch` | `retired` 2026-08-20/21 | `instruments/rodeo-001/docs/RODEO_CONTROL_MAP.md:461` |
| `rodeo-switch` (SPDT), `rodeo-pixel`, `rodeo-encoder-bank` | `retired` | `instruments/rodeo-001/docs/RODEO_CONTROL_MAP.md:40,462-463` |

## Platform — `in-progress` / unverified

| Claim | Note | Evidence |
|---|---|---|
| ADS114S08 **absolute-voltage accuracy** | Conversion path is proven; absolute accuracy is still unverified pending a jumper/pot test | `rev-a/rodeo-sdk/README.md:133-134` |
| Hub I²C at 400 kHz | Boots at 100 kHz and works; the 400 kHz `TIMINGR` value has never been generated, and SDA/SCL setup-hold has never been looked at on a logic analyser | `rev-a/rodeo-encoder-hub/firmware/README.md:179-183`; `RODEO_HUB_I2C_CONTRACT.md:36` |
| Motherboard board outline | Edge.Cuts not finalized — the mounting rectangle is verified, the overall outline is provisional | `rev-a/rodeo-motherboard/kicad/rodeo-motherboard.kicad_pcb` |
| `rodeo-button` | Interface verified, but the switch footprint is not yet assigned | `instruments/rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md` open items |
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
| **Effects** | `not-planned` — "none exist anywhere in the repo" | **`in-progress` on rodeo-002.** A four-slot texture chain: CHARACTER → MOVEMENT → TEXTURE → SPACE, ending in a granular delay with Echo / Cloud / Reverse / Wash / Reverb types. | `instruments/rodeo-002/firmware/common/fx_space.h:1-8`, `instruments/rodeo-002/firmware/common/texture_chain.h` |
| **Presets** | `planned` | **`in-progress` on rodeo-002.** Patch storage, a factory bank, user slots, naming from the panel. MIDI channel and master volume are deliberately excluded from a patch. | `instruments/rodeo-002/firmware/common/preset.h:1-6`, `instruments/rodeo-002/firmware/common/factory_presets.h` |

Consequently **`preset` and `effects` come off the banned-phrase list** for rodeo-002 copy —
but they stay banned for platform and `rodeo-001` copy, which is why the banned list needs to
become per-scope rather than global, now that `boards.ts` is the one source.

Not yet re-audited, and required before any rodeo-002 page ships: the voice count (the cost
model caps it, and the bench logs sweep to 12 under the bootloader rather than 16), the engine
list, and the encoder count — the concept doc says five, a later commit adds a sixth, so the
doc lags the firmware. Do not state any of these numbers until they are checked against
`instruments/rodeo-002/docs/` and the bench logs.

## Reference build (`rodeo-001`) — mostly `planned`

`instruments/rodeo-001/firmware/src/main.cpp:1` describes itself as a **scaffold**, and its own status
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
| Enclosure envelope ~380–420 × 190–220 mm | `planned`, render as **"(draft)"** | `instruments/rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md:4` (`status: draft`), `:439` |
| Panel aesthetic (Cassette Futurism, two-layer acrylic) | `planned` — still "proposed, for Derek's nod", colour pairing open | `instruments/rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md:474-491` |

Note the demonstrated poly examples (`06_poly_synth`, `09_poly_synth_oled`) are **6-voice**;
the only 12-voice code is the scaffold. Don't conflate them.

## Never on the site

| Phrase | Why |
|---|---|
| `pre-order` | Not a thing that exists |
| ~~`open source`~~ **— lifted 2026-10-16** | The LICENSE files landed with the Rev-B merge, so the gate that made this a banned phrase is satisfied. See "Saying it accurately" below; the phrase is now allowed and the *licence names* are what need care. |
| **Bumble, Honey, Fizz** | Agent personas in the engineering docs, not team credits |
| `USB MIDI`, `USB audio` | **Rev-scoped since 2026-10-15.** Banned for Rev-A, where the panel USB-C is power only. Permitted for Rev-B as `planned`, never as a capability, until a Rev-B board has done it on a bench. |
| `unison`, `mono mode`, `hard sync` | Planned on `rodeo-001`, not built |
| `preset`, `effects` | **Scope-dependent.** Banned for platform and `rodeo-001` copy; permitted for `rodeo-002`, where both now exist — see "Reference builds — there are now two". The banned list must become per-scope. |

## The lockfile — check 3, finally built

**Built 2026-10-18**, after the gap it closes was exploited twice.

Checks 1 and 2 prove a cited path exists and that its lines are not blank.
Neither proves the lines still **say** what the claim says. Both failures looked
identical from CI — green:

1. A document moved to another repository and the links to it kept pointing at
   nothing.
2. Freezing the Rev-A SDK shifted a README by four lines, so a claim of
   "bench-confirmed on real hardware" came to sit against a paragraph about
   control maps.

`web/scripts/claims.lock.json` now records the exact text under every cited line
range — 48 of them — and `check-claims.mjs` compares it on every run. The stored
value is the **text itself, not a hash**, so `git diff` on the lockfile shows a
reviewer precisely what moved.

| Command | What it does |
|---|---|
| `npm run claims` | Checks against the lock. Drift fails the build. |
| `npm run claims:update` | Re-records, after you have re-read what moved. |

A new citation is a failure until it is recorded, which forces the lockfile to
be committed with the claim it protects. Updating is deliberate and the diff is
meant to be read — a lockfile refreshed without a re-read is ceremony.

Proven against three planted defects before being trusted: rewording a cited
line, inserting a line above a citation so the range slides, and adding a claim
that is not in the lock. All three fail, with the previous and current text
printed side by side.

## Saying it accurately — the licences

**Settled 2026-10-16.** `LICENSE` and `LICENSES/` are on `main`, so "open source" is
true and may be said plainly. What replaces the old ban is a narrower rule: **the
licence names and what they require are legal statements, and getting them wrong is
worse than saying nothing.**

| Part | Licence |
|---|---|
| Hardware — KiCad projects, Gerbers, BOMs, placement and panel files | **CERN-OHL-W-2.0** |
| Software — `rodeo-sdk`, the firmware trees, `tools/` | **MIT** |
| Documentation | **CC-BY-SA-4.0** |

Copyright is **DOT RODEO LLC**, and the published source location is
`https://github.com/dotrodeo/rodeo`.

Three things the site must not get wrong:

1. **CERN-OHL-W is *weakly* reciprocal, and the W matters.** Modify a Rodeo design and
   distribute it and your modified design files must be published under the same
   licence — but a product that merely *uses* Rodeo boards, or connects to them through
   their published interfaces, does not have to be open. Do not describe this as
   "strongly reciprocal", as CERN-OHL-S, or as "any product using Rodeo must be open".
   That would deter exactly the commercial builders the permissive boundary was chosen
   to attract.
2. **It is three licences, not one.** "Rodeo is MIT" is wrong and so is "Rodeo is
   CERN-OHL". Name the part if the licence is named at all.
3. **`rodeooh.rodeo/source` is printed on the Rev-B boards** as their CERN-OHL-W source
   location, so that redirect must keep working for as long as those boards exist. It
   lives in `web/public/_redirects`. Point it somewhere new if the repository moves;
   never remove it.

**One thing still pending:** the repository is private until launch, so the GitHub links
on the site — and the `/source` redirect the boards carry — resolve to a 404 for
visitors in the meantime. That resolves itself when the repo goes public, and needs no
code change.

## Sourcing caveat — two docs disagree

`rev-a/rodeo-motherboard/docs/hardware-map.md:186-188` still carries
**"### 1. CRITICAL — SPI MOSI/MISO are swapped; the MCU cannot talk to the ADCs"** with
"Fix before fab". This is **stale**. `instruments/rodeo-001/docs/RODEO_CONTROL_MAP.md:21-29` records the
issue as resolved and bench-verified pin-by-pin, and it is proven in silicon because
`04_adc_1ch` and `05_adc_scan` read live data on both converters.

**Where the two disagree, the control map wins.** This is also worth fixing in the hardware
map itself — an open-source platform whose own docs contain a CRITICAL warning that is no
longer true will cost a reader an afternoon.

## The guardrail to build

Specified here; implemented in a later phase. Five checks in `web/scripts/check-claims.mjs`.

1. **Evidence exists.** Every claim carries a non-empty `evidence` path; it resolves from the
   repo root; if `path:line`, that line exists. (This is why CI sparse-checkout includes
   `instruments/rodeo-001/**` and `rev-a/rodeo-motherboard/**` — see
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
   whenever firmware moves under a claim. **Do not cut this check** — without it `boards.ts`
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
