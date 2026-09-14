---
title: "Rodeo Control Map — Interface Contract"
tags: [rodeo, hardware, control-map, daisy-seed, pinout, ads114s08]
status: active
created: 2026-08-08
updated: 2026-08-22
---

# Rodeo Control Map — Interface Contract

**Owner:** Bumble (hardware/electronics). Source of truth for the pin/channel → function
assignment. Firmware (Fizz) reads controls exactly as mapped here; the panel (Honey) must
physically fit what is specified here. Keep all three in sync.

> Status: verified against the KiCad schematics in `rodeo-motherboard/kicad/` (root
> `rodeo-motherboard.kicad_sch` + subsheets `daisy_seed`, `digital_io`, `jst_connectors`,
> `serial_io`) and cross-checked with the firmware-side `docs/hardware-map.md`. The **PCB**
> (`rodeo-motherboard.kicad_pcb`, 87 parts) is the net authority — the root schematic does not
> hierarchically instantiate the subsheets, so a schematic-only netlist is incomplete.

## ✅ SPI orientation — RESOLVED & bench-verified (2026-08-15)

The earlier "SPI data lines crossed" concern was a **stale local snapshot**. The current
copy under `rodeo-motherboard/kicad/` is drawn **correctly** — verified pin-by-pin this
session: A1 pad11 `SPI1_PICO` (controller-out/MOSI) → `SPI_MOSI` ─R16─ `ADC_MOSI` → U3/U4
**DIN** (pin 10); A1 pad10 `SPI1_POCI` (controller-in/MISO) → `SPI_MISO` ─R13─ `ADC_MISO` →
U3/U4 **DOUT** (pin 12). And it is **proven in silicon**: `04_adc_1ch` read live data on U3
and `05_adc_scan` streams U4, so DOUT→MISO / DIN→MOSI is correct on the real board. No
reroute needed. (See resolved Issue 1 below.)

## Design maturity (Derek, 2026-08-08)

- **Finalized / trustworthy:** `rodeo-potentiometer` and the motherboard analog+digital
  fan-out it mates to. *(2026-08-20/21: `rodeo-rotary-switch` was here but is now **dropped**
  — see Ecosystem Update in the Path B section. Locked control types: pot, encoder (I2C),
  momentary button, OLED.)*
- **Done / interface-verified (2026-08-20):** `rodeo-button`, `rodeo-encoder`,
  `rodeo-encoder-hub` (STM32C031C6, 9-slot). The motherboard itself is the only remaining
  moving piece (this local copy may lag the latest).
- **Dropped:** `rodeo-rotary-switch`, `rodeo-switch` (SPDT), `rodeo-pixel`,
  `rodeo-encoder-bank` (RP2040 4-gang → replaced by the hub-and-satellite above).

---

## Confirmed budget (Derek, 2026-08-08)

| Resource | Count | Reality |
|----------|-------|---------|
| Analog inputs | 24 | Two external **TI ADS114S08** 12-ch 16-bit ΔΣ ADCs (U3/U4) on **SPI1**, not the Seed's native ADC pins |
| Digital I/O | 12 | Raw Daisy Seed GPIO — **no buffers, no expanders, no muxes** |
| I2C | I2C1 | Qwiic/STEMMA-QT 4-pin header (J42); smart modules live here |
| SPI | SPI1 | Shared with the two ADCs; also broken out on J43 (no dedicated CS) |
| UART/MIDI | USART1 | J44 header **and** the MIDI DIN share USART1 → mutually exclusive |
| Display | **SSD1309 OLED, 128×64** | **I2C @ 0x3C** (Decision A, 2026-08-15) — feedback-only, shares I2C1/J42 with the encoder hub(s). **DD-001 originally chose SPI but that was SUPERSEDED:** the only SPI bus (SPI1) is fully owned by the two ADS114S08 ADCs, so the OLED had to move to I2C. So the I2C bus = **OLED 0x3C + encoder hub(s) 0x30–0x33.** (2026-08-20: I briefly mis-corrected this to SPI — reverted; Derek confirmed I2C.) |

## Governing design principle: NO HIDDEN CONTROLS

Derek: *"one of the goals of the synth is to have no hidden controls."* Every
player-adjustable parameter gets its own dedicated physical control — no menu-diving, no
shift layers. The OLED is feedback/visualization only. Consequences:

1. Continuous parameters are bounded by the **24 analog channels**.
2. Discrete selectors remain "visible" if labeled — a resistor-ladder rotary switch reads
   as one analog voltage giving N silk-screened positions on a single channel.
3. Encoders arrive over I2C (smart modules), so they do **not** consume the 24/12 budget —
   they're bus-limited, not pin-limited (see budget note under Modules).

---

## Motherboard is a HUB; controls are satellite PCBs

Rodeo is a modular ecosystem, not a monolith. The motherboard exposes its I/O as JST-SH
connectors; each control type is its own satellite PCB that wires back to the hub. This
**supersedes** the earlier "single co-designed control PCB" seam Honey and I sketched.

- **Dumb analog satellite** (`rodeo-potentiometer`): 3-pin JST-SH → an ADC channel.
- **Dumb digital satellite** (`rodeo-button`, momentary): 2-pin JST-SH → a digital GPIO.
- **Dumb encoder satellite** (`rodeo-encoder`, EC11): 4-pin (A/B/S/GND) → a hub slot.
- **Smart I2C hub** (`rodeo-encoder-hub`, STM32C031C6, 9 encoders): Qwiic → J42 @0x30.

  *(Dropped: rotary-switch, SPDT switch, pixel-LED, and the RP2040 encoder-bank strip.)*

### Ownership seam (revised)

- **Honey owns:** enclosure, panel layout as a *tiling of fixed satellite-board geometries*
  over the hub, panel cutouts, mounting, standoffs, knob/cap.
- **Bumble owns:** the **satellite↔hub wiring harness** (JST-SH pinouts, lengths, routing),
  the **I2C bus** (address allocation, pull-up sizing, chain length), ADC/SPI integrity,
  grounding, and any anti-alias filtering that isn't already on a satellite.

### Harness cable orientation (netlist-verified 2026-09-13)

JST-SH cables come in two builds — **straight** (pin 1 → pin 1) and **reversed**
(pin 1 → last pin) — and the ecosystem does **not** use one pin-1 convention
everywhere, so which one a run needs depends on the link. Verified pad-by-pad from
each board's `.kicad_pcb`:

| Link | Host pins (1 → N) | Satellite pins (1 → N) | Cable |
|---|---|---|---|
| **Analog:** motherboard J6–J29 ↔ `rodeo-potentiometer` / `rodeo-rotary-switch` | `+3V3 / signal / GND` | `GND / signal / supply-in` | **REVERSED** |
| **Digital:** motherboard J30–J41 ↔ `rodeo-button` | `DIG / GND` | `signal / GND` | straight |
| **I²C:** motherboard J42 ↔ `rodeo-encoder-hub` J1/J2 | `GND / +3V3 / SDA / SCL` | same | straight |
| **Encoder:** hub J3–J11 ↔ `rodeo-encoder` J1 | `A / B / S / GND` | same | straight |

On the analog satellites, pin 3 feeds the top of the pot / ladder through the 1 kΩ + 100 nF
supply filter, and pin 1 is ground — the mirror image of the motherboard side. What the wrong
cable does, per link:

- **Analog, built straight:** the satellite's supply and ground swap. Everything on those boards
  is passive, so nothing is damaged — but **every pot reads backwards** and a rotary-switch ladder
  inverts into the wrong bins. The wiper is the centre pin, so it still produces a plausible
  0–3.3 V sweep, which makes this look like a firmware sign bug rather than a harness fault.
- **I²C, built reversed:** SCL is grounded and SDA is tied to +3V3. The whole bus dies (the OLED
  goes dark too), and the hub MCU is back-powered through its SDA pin.
- **Encoder, built reversed:** A lands on the encoder's ground pin and B swaps with the switch, so
  the slot never counts.
- **Button:** straight matches the netlist. A reversed 2-pin still happens to switch, but build
  them straight.

**Never judge a cable by wire colour.** Motherboard connectors are vertical and satellite
connectors horizontal, so the ribbon colours land mirrored *even on a correct cable* — that is
exactly how the reversed I²C and encoder cables got built on first bring-up. Ohm out pin 1 at each
end before plugging in. Off-the-shelf Qwiic / STEMMA QT cables are straight and suit the I²C and
encoder runs (colours are meaningless on the encoder runs); the analog runs need reversed builds.

---

## Analog subsystem — 24 channels via 2× ADS114S08 (U3/U4) on SPI1

- 16-bit delta-sigma, 12 channels each. **Ratiometric to the +3V3 rail** (U2 TLV76733 LDO):
  `AVDD/DVDD/IOVDD/REFP0 = +3V3`, `AVSS/REFN0 = GND`. Pots share that same rail, so supply
  variation cancels in the reading. START/SYNC tied high (continuous conversion); `DRDY#`
  unconnected → firmware **polls with RDATA** on a fixed cadence, no data-ready line.
- Shared SPI1 with 33 Ω series resistors (R13/R14/R16); individually chip-selected:
  - `SPI_SCK` → Seed pin 9
  - `SPI_MISO` net → pad 10 (`SPI1_POCI`, controller-in) → ADC **DOUT**; `SPI_MOSI` net → pad 11 (`SPI1_PICO`, controller-out) → ADC **DIN**. Orientation verified correct & bench-proven — Issue 1 CLOSED (see SPI note up top). Firmware handle: `MISO=D9`, `MOSI=D10`.
  - `SPI_ADC1_CS` → Seed pin 32 (PA0, GPIO) = U3 (ADC A)
  - `SPI_ADC2_CS` → Seed pin 33 (PD11, GPIO) = U4 (ADC B)
  - Firmware: `NSS::SOFT`, drive PA0/PD11 as GPIO.

### Analog channel ↔ connector map (uniform)

| Connectors | Net | Goes to | Pinout |
|---|---|---|---|
| **J6–J17** | `ADC1_1..12` | U3 AIN0–AIN11 | 1=+3V3, 2=signal(wiper), 3=GND |
| **J18–J29** | `ADC2_1..12` | U4 AIN0–AIN11 | 1=+3V3, 2=signal, 3=GND |

Rule: `ADC1_k ↔ J(5+k)`, `ADC2_k ↔ J(17+k)`, and `AINindex = k−1`. All 24 are 3-pin
JST-SH 1.0 mm (`BM03B-SRSS-TB`, vertical). **These runs take REVERSED cables** — the
satellites are pinned `GND / signal / supply-in`; see *Harness cable orientation* above.

**Firmware channel-index convention (contract with `RodeoAdc::GetCode(ch)`):** the
canonical linear index is `ch = chip·12 + AINindex`, so:

| `ch` | Chip | AINx | Connector |
|---|---|---|---|
| `0..11`  | U3 (ADC A) | AIN0..AIN11 | J6..J17 |
| `12..23` | U4 (ADC B) | AIN0..AIN11 | J18..J29 |

i.e. `ch 0→J6`, `ch 11→J17`, `ch 12→J18`, `ch 23→J29`. This binding is the
source of truth; firmware `board.h` and the ADC lib must match it so a logical
control lands on the connector this map names.

**AINCOM is board-tied to GND** on both U3 and U4 (verified in KiCad), so each
single-ended read `AINx − AINCOM = AINx − GND` = the full 0–3.3 V wiper voltage,
ratiometric to the +3V3 pot supply. Unipolar → the 16-bit signed code spans
`0..+32767` (the negative half is unused); clamp small sub-zero excursions at
0 V to 0 (see firmware note).

**Latent channel footgun:** AIN6/AIN7 alias REFP1/REFN1 and AIN8–AIN11 alias
GPIO0–GPIO3 on the ADS114S08. We use REFP0/REFN0 (`REFSEL=00`) and no GPIO, so
all 12 per chip are free as inputs — but never set `REFSEL=01` or configure
those pins as GPIO, or the corresponding channels silently disappear.

> **⚡ ECOSYSTEM UPDATE (Derek, 2026-08-20/21): rotary switches + LEDs REMOVED.**
> Locked-available control types are now exactly four: **potentiometers (analog),
> rotary encoders (I2C), momentary buttons (digital), and the SSD1309 OLED (I2C).**
> Consequences for the allocation below, reconciled and now LOCKED:
> - `rodeo-rotary-switch` is **dropped**. **✅ DECIDED (Derek, 2026-08-21): Wave-select = encoders
>   (option b).** OSC1 Wave + OSC2 Wave become `rodeo-encoder` singles inside their own OSC
>   boxes, on **hub slots 7 & 8** @0x30 (hub now **8/9**, 1 spare). **ch0/ch3 (J6/J9) are freed
>   as spare ADS inputs** — reserved for a future CV/mod input. **Channel call (Bumble): leave
>   the gaps at J6/J9** (do NOT re-pack the 22 pots to the tail) — the 22 pots keep their exact
>   frozen ch assignments and harness wiring; the only `board.h` delta is ch0/ch3 → SPARE.
>   Rationale: zero disturbance to the frozen 1:1 physical-order mapping vs. a full 22-channel
>   renumber + harness rewire for a marginal spare-adjacency gain.
> - All **LED** hardware (per-encoder rings, pixel strip, indicators) removed from the
>   panel; the OLED is the **single feedback surface**. Hub LED chain stays on-board but
>   firmware-stubbed (parked future upgrade). No LED cutouts/lightpipes on the panel.
> Final locked panel tally: **22 pots + 8 encoders + 6 buttons + 1 OLED = 37 controls.**
> Firmware note (Fizz): encoders are incremental → **persist waveform + encoder state across
> power cycles**, value shown on the OLED; wave-encoder steps ±1 through the 4 waveforms
> (wrap), direction per `CONFIG.ENC_DIR_INVERT`.

> **🎛️ VOICE UPDATE (Derek, 2026-08-22): voice = "less-complicated MiniFreak"** — 12-voice
> poly, oscillators **subtractive + FM + formant**, **polyphonic SVF** filter, Filter ADSR +
> Amp ADSR + **1 LFO single destination, no mod matrix**. Impact on this contract:
> - **Filter / mod / mix / global / voice controls: UNCHANGED and frozen** — they map 1:1 onto
>   the new voice (SVF ↔ Filter Type, LFO Dest ↔ the single destination, both ADSRs, etc.).
> - **✅ OSCILLATOR section RESOLVED — Option A (Derek, 2026-08-22): semantics-only, no channel
>   remap, stays 22/24.** Per-osc **Shape → model-dependent Timbre** (PW for subtractive /
>   FM-index for FM / formant-position for formant — the *interpretation* changes per engine,
>   the pot keeps its channel). The **Wave encoders (hub slots 7/8) = engine select**. The
>   global **Vowel pot stays as-is**. No FM/formant params get new channels; the ch0/ch3 spares
>   are preserved. Only firmware interpretation changes — the pin/channel/slot bindings below
>   are unaffected. (This is the one place we lean on per-engine macro re-use; Derek accepted it
>   for the oscillator only, filter/mod/mix stay strictly one-control-per-parameter.)

### THIS BUILD — Path B allocation (locked 2026-08-15, Derek)

**Decision (Derek, 2026-08-15):** the motherboard is already produced → **no respin → Path B.**
**Wheels removed** (Derek: desktop synth — pitch-bend + mod come over MIDI, not the panel),
dropping the surface to **35 analog** (26 pots + 9 selectors) + 1 toggle. We fit it onto the
board as-built: 24 ADS channels + digital cycle-buttons + one I2C encoder-hub + the OLED for
enum readout. (Path A / 3rd-ADC is the documented **Rev-B upgrade path** — see below.) Class
allocation is frozen here; the exact pot→connector *ordering* is deferred to a sync with Honey
so channel order matches physical left-to-right panel grouping.

| Subsystem | Budget | Gets | Count |
|---|---|---|---|
| **24 ADS channels** (J6–J29) | 24 | 22 live-sweep **pots** (ch0/ch3 = J6/J9 now **SPARE**) | **22 used, 2 spare** |
| **Digital GPIO** (J30–J41) | 12 | 6× momentary `rodeo-button` (5 cycle-enum + 1 hard-sync) → **DIG_1–6 / J30–J35** | **6 used** (6 spare) |
| **I2C encoder-hub** (J42) | — | one `rodeo-encoder-hub` @`0x30`, 9-slot: 4 **trims** + 2 **octaves** + 2 **Wave** | **8 encoders → 1 hub (8/9)** |

**The 24 ADS channels** — 22 live-sweep pots (Wave/engine selectors moved to the encoder-hub):
OSC1 Timbre, OSC1 Spread, OSC2 Timbre, OSC2 Spread · OSC1 lvl, OSC2 lvl, Noise lvl ·
Cutoff, Resonance, Env amount, Vowel · Filt-ENV A/D/S/R, Amp-ENV A/D/S/R · LFO Rate, LFO
Amount · Master volume  = **22 pots**. **ch0 (J6) and ch3 (J9) are now SPARE** (Wave went to
encoders) — first free ADS headers, reserved for a future CV/mod input. **22 used, 2 spare.**

**Digital** — 6× momentary `rodeo-button` (2-pin JST-SH sig/GND, idle-high via STM32 internal
pull-up, 100 nF debounce on the satellite):
- **5 cycle-buttons** (momentary push → firmware cycles enum, OLED shows value): Filter Type,
  Keytrack, LFO Shape, LFO Dest, Voice Mode.
- **1 hard-sync** (OSC2): also a momentary `rodeo-button`; firmware treats the press as a
  toggle (SPDT `rodeo-switch` was dropped).
= **6 of 12 digital used, 6 spare.**

#### 🔒 FROZEN digital-button table (2026-08-23, from Honey's Panel Layout, physical order)

Connector↔digital-index mapping is uniform: **`J(29+n) = DIG_n`** (J30 = DIG_1 … J41 = DIG_12).
The 6 buttons take the **first 6 connectors J30–J35 = DIG_1–DIG_6 = Seed pins 2–7** (the SDMMC
pins — digital-only, no ADC/timer capability, so nothing is wasted on them). **J36–J41 =
DIG_7–DIG_12 = Seed pins 22–27 stay SPARE** — these are the *ADC-capable* pins, deliberately
reserved so a future need (native-ADC input, CV, a timer function) can reclaim them.

| Button (signal-flow order) | Section | `DIG_n` | Connector | Seed pin |
|---|---|---|---|---|
| OSC2 Hard-sync | OSC/MIX seam | DIG_1 | **J30** | 2 |
| Filter Type / SVF mode | FILTER | DIG_2 | **J31** | 3 |
| Filter Keytrack | FILTER | DIG_3 | **J32** | 4 |
| LFO Shape | LFO | DIG_4 | **J33** | 5 |
| LFO Dest | LFO | DIG_5 | **J34** | 6 |
| Voice Mode | GLOBAL | DIG_6 | **J35** | 7 |

**Firmware contract:** bind each button's logical enum to its `DIG_n` (→ the libDaisy pin for
Seed pins 2–7 in `board.h`), `GPIO::Pull::PULLUP`, active-low (pressed = 0). No SD card in
Rodeo, so the SDMMC pins are free as plain GPIO — no boot-strap conflict.

**Encoder-hub** (detented, OLED feedback, off the 24/12 budget — hub-and-satellite, one
9-slot `rodeo-encoder-hub` @`0x30`; see the LOCKED hub section below):
- **`0x30` index↔param table (LOCKED 2026-08-21):**
  slot 1 = OSC2 Detune · slot 2 = Unison detune · slot 3 = Glide · slot 4 = Master tune ·
  slot 5 = OSC1 Octave · slot 6 = OSC2 Octave · **slot 7 = OSC1 Engine/Wave select** ·
  **slot 8 = OSC2 Engine/Wave select** (Option A: encoder picks the osc engine/waveform;
  steps ±1 with wrap) · slot 9 = spare. **8 of 9 slots used, 1 spare.**
  Firmware binds these hub indices; physical hub-header↔slot wiring is Honey's harness (I
  confirm the index↔param map once her v2 harness is drawn — convention: header order = slot order).

**Electrical contract points I own for Path B (new, because we now lean on digital + I2C):**
- **Cycle-buttons = momentary-to-GND on the 2-pin digital connectors (sig/GND, no 3V3).**
  Idle-high is defined by the **STM32 internal pull-up** (Fizz: `GPIO::Pull::PULLUP`, ≈40 kΩ)
  — no hub-side resistor. Actuation is *momentary*, distinct from the SPDT `rodeo-switch`;
  Honey is adding a **`rodeo-button`** momentary satellite (confirmed 2026-08-15) — same
  2-pin electrical interface.
- **Wave rotary switches** ride `rodeo-rotary-switch` (finalized): resistor ladder → one
  analog voltage, 4 silk-labeled positions on 1 ADC channel each. Firmware thresholds the
  voltage into 4 bins (ladder has margin for ≥6 positions, so 4 is comfortable).
- **I2C is now a live bus** (OLED 0x3C + one encoder-hub 0x30 on J42). Chain length is
  finally known enough to size pull-ups (Issue 2): boot at 100 kHz, target **2.2–3.3 kΩ to
  +3V3** for 400 kHz. SSD1309 full-frame at 400 kHz ≈ 23 ms (~30 fps), so OLED does partial/low-rate
  refresh while encoder polls (16-byte bursts from `0x10`) interleave — Fizz schedules; no contention.
- **Release valve (0-spare ADS):** if we ever need a free ADS channel, demote one Wave rotary
  back to a cycle-button (frees 1 channel, costs 1 of the 6 spare digital). Trivial swap.

#### 🔒 FROZEN pot-by-pot table (2026-08-15, from Honey's Panel Layout v1)

Honey delivered the dimensioned panel's **physical left-to-right ADC sequence** (top band
then bottom, signal-flow order); I mapped it 1:1 onto the connectors via `ch = chip·12 +
AINindex`. **This table is the frozen firmware/panel contract** — `board.h` control enum
must bind these `ch` indices to these functions.

| `ch` | Function | Class | Chip | AINx | Connector |
|---|---|---|---|---|---|
| 0  | **SPARE** (was OSC1 Wave; moved to encoder-hub slot 7) — free ADS header, future CV/mod | — | U3 | AIN0  | **J6**  |
| 1  | OSC1 Timbre (model-dependent: PW / FM-index / formant-pos) | pot | U3 | AIN1  | **J7**  |
| 2  | OSC1 Spread    | pot | U3 | AIN2  | **J8**  |
| 3  | **SPARE** (was OSC2 Wave; moved to encoder-hub slot 8) — free ADS header, future CV/mod | — | U3 | AIN3  | **J9**  |
| 4  | OSC2 Timbre (model-dependent: PW / FM-index / formant-pos) | pot | U3 | AIN4  | **J10** |
| 5  | OSC2 Spread    | pot | U3 | AIN5  | **J11** |
| 6  | OSC1 level     | pot | U3 | AIN6  | **J12** |
| 7  | OSC2 level     | pot | U3 | AIN7  | **J13** |
| 8  | Noise level    | pot | U3 | AIN8  | **J14** |
| 9  | Cutoff         | pot | U3 | AIN9  | **J15** |
| 10 | Resonance      | pot | U3 | AIN10 | **J16** |
| 11 | Filter Env amt | pot | U3 | AIN11 | **J17** |
| 12 | Vowel (formant)| pot | U4 | AIN0  | **J18** |
| 13 | Filt-ENV A     | pot | U4 | AIN1  | **J19** |
| 14 | Filt-ENV D     | pot | U4 | AIN2  | **J20** |
| 15 | Filt-ENV S     | pot | U4 | AIN3  | **J21** |
| 16 | Filt-ENV R     | pot | U4 | AIN4  | **J22** |
| 17 | Amp-ENV A      | pot | U4 | AIN5  | **J23** |
| 18 | Amp-ENV D      | pot | U4 | AIN6  | **J24** |
| 19 | Amp-ENV S      | pot | U4 | AIN7  | **J25** |
| 20 | Amp-ENV R      | pot | U4 | AIN8  | **J26** |
| 21 | LFO Rate       | pot | U4 | AIN9  | **J27** |
| 22 | LFO Amount     | pot | U4 | AIN10 | **J28** |
| 23 | Master Volume  | pot | U4 | AIN11 | **J29** |

- **U3 = ch0–11 (J6–J17):** both OSC blocks + MIX + top of FILTER (Cutoff/Reso/EnvAmt).
- **U4 = ch12–23 (J18–J29):** Vowel + both ADSRs + LFO + Volume.
- **ch0/ch3 (U3 AIN0/AIN3, J6/J9) are SPARE** — the Wave selectors moved to encoder-hub
  slots 7/8 (Derek 2026-08-21). Headers left unpopulated; `board.h` ch0/ch3 = SPARE, all 22
  pots keep their frozen ch. Firmware may skip them in the populated-channel list; if a defined
  reading is ever wanted, tie AIN0/AIN3 to GND (rev-B). Reserved for a future CV/mod input
  (which brings its own buffer + protection per the analog front-end note).
- **Chip boundary lands mid-FILTER** (ch11 EnvAmt on U3 │ ch12 Vowel on U4). **Kept as-is,
  not rotated:** the two chips convert in parallel and all 24 channels are read every scan,
  so a section spanning the boundary has no coherence or scan-grouping penalty — Honey's
  physical order is preserved 1:1.
- **Alias-pin reminder (Issue footgun):** ch6/ch7 use AIN6/AIN7 (= REFP1/REFN1 pads) and
  ch8–11 use AIN8–11 (= GPIO0–3 pads). Correct as inputs under `REFSEL=00` + no GPIO config;
  never repurpose those pins or the channel vanishes.

---

### REV-B UPGRADE PATH — 36-channel expansion (Path A, NOT this build)

> **Not built.** Derek chose Path B (board already produced). This is the documented upgrade
> for a future motherboard rev that wants the pure all-pots-and-switches Prophet surface.

Honey's Voice Inventory v0.1 tallies **37 analog controls** (26 pots + 9 rotary selectors +
2 wheels) for the all-visible 2-osc surface — 13 over the 24-channel budget. **Adding a 3rd
ADS114S08 (U5) on the shared SPI1 bus is feasible and low-risk.** Verdict and spec:

- **Bus:** U5 hangs on the same `SPI_SCK`/`SPI_MOSI`/`SPI_MISO` as U3/U4 — add 33 Ω series
  R's matching R13/R14/R16. Multi-drop MISO is safe: the ADS114S08 **tri-states DOUT when
  CS is high** (already relied on for U3/U4), so no bus contention.
- **Chip-select — the one new pin:** needs exactly one more GPIO. **Seed pin 8 = PG10
  (SPI1 hardware NSS), currently NC** — the natural `SPI_ADC3_CS`. We already run
  `NSS::SOFT` and drive CS as GPIO, so PG10-as-GPIO is fine. Leaves Seed pins 29/30/31/34/35
  still spare. → three CS: PA0(U3) / PD11(U4) / **PG10(U5)**.
- **Latency is ~free — parallel conversion.** Each ADS114S08 has its own ΔΣ core + input
  mux; the 3 chips convert **simultaneously**. The SPI bus only carries MUX-select + result
  readout. Going 24→36 does **not** deepen any chip's mux (each stays ≤12 channels), so
  full-surface scan wall-time is essentially unchanged — you read 3 chips over the bus
  instead of 2 (marginal SPI overhead). Expansion by adding chips (parallel) beats muxing
  deeper (serial). This is the key reason A is cheap.
- **Ref/power identical:** REFP0/REFN0 = +3V3/GND, AINCOM→GND, `REFSEL=00`, same AIN6/7 &
  AIN8–11 alias footgun (never enable as GPIO/REF). ~few mA on the 3V3 LDO — negligible;
  add 0.1 µF + 10 µF local decoupling like U3/U4.
- **CS hygiene:** three CS nets now — extend Fizz's "park all CS idle-high before probe"
  init (commit `611f48a`) to U5, and the **Rev-B 10 kΩ CS pull-ups (Issue 6) now cover 3
  nets** not 2.
- **Connectors:** +12 3-pin JST-SH → propose **J50–J61** (J45–J49 are power JST-ZH, so J50
  is the next free number). Channels extend the linear convention: `ch = chip·12 + AINx`
  with chip index 2 → **`ch 24..35` = U5 AIN0..AIN11 = J50..J61**.

**The two wheels ride the Seed's native ADC, not U5.** Only **1 of the 12 digital GPIO** is
used (the panel toggle), and pins 22–27 are ADC-capable (`ADC_0..5`, currently `DIG_7..12`).
Put pitch-bend + mod on **Seed pins 22/23 (ADC_0/ADC_1)** — 12-bit is plenty for a wheel, and
it keeps the 3rd chip's budget for the 35 pots+selectors (36 ch ⇒ **1 ADS spare**). Native
ADC shares the H7 die (noisier than the ratiometric ADS path); if pitch-wheel noise is ever
audible, **promote pitch-bend to the spare ADS channel** — we'll have it. Each native-ADC
wheel needs its own RC anti-alias (≈1 kΩ + 100 nF) and firmware oversampling.

**Rejected alternative:** an analog mux (74HC4051/4067) ahead of existing channels
reintroduces time-multiplexed settling + charge-injection on a *precision* ΔΣ input and adds
a scanned/"hidden" feel; a dedicated 3rd ADC keeps the firmware scan loop identical (just one
more CS) and the signal path clean. Not recommended.

**Cost of A:** it forces a **motherboard respin** (U5 + CS trace + J50–J61). Rev-B is already
anticipated for the CS pull-ups (Issue 6) and possibly the pixel connector (Issue 3), so U5
folds into that spin at modest marginal cost — but the currently-fabbed board can't do the
full 37-control surface; the all-visible build needs the next rev. **Gating question for
Honey answered: yes, 36 channels + 2 native-ADC wheels covers the whole surface.** Full
channel-by-channel assignment table lands once Derek makes the A-vs-B vibe call.

## Digital subsystem — 12 raw GPIO

- **J30–J41**, 2-pin JST-SH (`BM02B-SRSS-TB`), pinout `1=signal, 2=GND` (no 3V3).
- `DIG_1..DIG_6` → Seed pins 2–7 = **`seed::D1..D6` = PC11 / PC10 / PC9 / PC8 / PD2 / PC12**
  (the SDMMC1 pins D3/D2/D1/D0/CMD/CK — digital-only, no ADC/timer). Verified 1:1 against
  libDaisy's port table (Fizz, 2026-08-23). No SD card in Rodeo → free as plain GPIO.
- `DIG_7..DIG_12` → Seed pins 22–27 (ADC-capable pins used as GPIO; firmware could reclaim
  up to 6 as native analog if ever needed). PB1/PA7 are 3.3 V-tolerant only. **Kept SPARE.**
- **Canonical DIG_n→Seed-pin fact now also lives in firmware `common/board.h`** (Fizz added the
  generic map 2026-08-23; matches this table). board.h = firmware-consumed source, this doc =
  human contract; they must stay in sync (currently identical). *Instance* button→DIG_n
  assignment stays in rodeo-001's `control_map.h`, not board.h.
- No expanders/muxes on the board. To expand digital, either mux off-board or move the
  function to a smart I2C satellite.

## Buses

| Bus | Seed pins | Header | Notes |
|---|---|---|---|
| I2C1 | SCL=12 (PB8), SDA=13 (PB9) | J42, 4-pin Qwiic (`+3V3,GND,SDA,SCL`) | **No on-board pull-ups** — Issue 2. OLED + encoder banks live here |
| SPI1 | SCK=9, pin10, pin11 | J43, 6-pin | Shares the ADC bus; **no dedicated CS**. Seed pin 8 (PG10 CS) is NC |
| USART1 | TX=14 (PB6), RX=15 (PB7) | J44, 4-pin | **Shared with MIDI DIN** (opto 6N138 U1, TRS-A J2/J3) → mutually exclusive |
| Audio | in=16/17, out=18/19 | J4 in / J5 out, 3.5 mm TRS | Single-ended, 100 Ω series (R4/R5) |
| USB-C | — | J1 | **Power only** (VBUS→VIN); Seed USB D+/− dead-end. Flash via Seed's own port |
| Power | VIN=39 | J45–J49, 3-pin JST-ZH | `GND,+3V3,VIN`; U2 TLV76733 LDO 5→3.3, D1 TVS on VIN |
| Status LED | Seed pin 28 | — | `DAISY_STATUS` → R10 1K → D5 → GND |

## Daisy Seed pin map (verified from `daisy_seed.kicad_sch`)

| Pin | Seed name | Net | Function |
|---|---|---|---|
| 2–7 | SDMMC | DIG_1..DIG_6 | Digital I/O |
| 8 | SPI1_CS (PG10) | NC | unused |
| 9 | SPI1_SCK (PG11) | SPI_SCK | ADC SPI clock |
| 10 | SPI1_POCI (PB4=MISO) | `SPI_MOSI` net | ADC SPI data ⚠ |
| 11 | SPI1_PICO (PB5=MOSI) | `SPI_MISO` net | ADC SPI data ⚠ |
| 12 | I2C1_SCL (PB8) | I2C_SCL | I2C clock |
| 13 | I2C1_SDA (PB9) | I2C_SDA | I2C data |
| 14 | USART1_TX (PB6) | UART_TX | UART/MIDI TX |
| 15 | USART1_RX (PB7) | UART_RX | UART/MIDI RX |
| 16/17 | AUDIO_IN 1/2 | AUDIO_IN_L/R | Line in |
| 18/19 | AUDIO_OUT 1/2 | AUDIO_OUT_L/R | Line out |
| 22–27 | ADC_0..5 | DIG_7..DIG_12 | Digital I/O (ADC-capable pins as GPIO) |
| 28 | ADC_6 | DAISY_STATUS | Status LED |
| 32 | SAI2_SD_B (PA0) | SPI_ADC1_CS | U3 chip-select (GPIO) |
| 33 | SAI2_SD_A (PD11) | SPI_ADC2_CS | U4 chip-select (GPIO) |
| 36/37 | USB D−/D+ | NC | no panel USB data |
| 39 | VIN | VIN | 5 V power in |
| 20/40 | AGND/DGND | GND | ground |

Seed pins 8, 29, 30, 31, 34, 35 = no-connect. Seed 3V3 outputs (21/38) unconnected —
carrier runs its own LDO rail.

---

## Control modules (satellites)

| Module | Maturity | Class | Sense element | Interface | Budget cost |
|---|---|---|---|---|---|
| **rodeo-potentiometer** | ✅ final | dumb analog | Alps RK09K pot; on-board R1 1K + C1 100 nF RC anti-alias | 3-pin JST-SH (3V3/wiper/GND) | 1 ADC channel |
| **rodeo-button** | ✅ done | dumb digital | momentary tact + 100 nF debounce; idle-high via **STM32 internal pull-up** (no on-board R) | 2-pin JST-SH (sig/GND) | 1 digital GPIO |
| **rodeo-encoder** | ✅ done | dumb (into hub) | 1× EC11 detented encoder + push-switch | 4-pin (A/B/S/GND) → a hub slot | **0** on 24/12 budget (rides I2C via hub) |
| **rodeo-encoder-hub** | ✅ done | **smart I2C** | up to **9× EC11**, **STM32C031C6** target (quad decode + press-latch); LED chain HW present but **firmware-stubbed** | Qwiic 4-pin I2C @`0x30` (addr jumpers JP1/JP2) | **0** on 24/12 budget; 1 I2C address, 9 encoder slots |
| ~~**rodeo-rotary-switch**~~ | ❌ DROPPED 2026-08-20/21 | — | resistor-ladder rotary; **removed** (control set locked to pot/encoder/button/OLED) | — | — |
| ~~**rodeo-switch** (SPDT)~~ | ❌ DROPPED | — | hard-sync now a momentary **rodeo-button** + firmware toggle, not an SPDT | — | — |
| ~~**rodeo-pixel**~~ | ❌ DROPPED | — | all LED feedback removed → OLED is sole feedback surface | — | — |

Encoder-hub (hub-and-satellite, current): **STM32C031C6** target, addr **0x30 default,
0x30–0x33** via 2 cuttable jumpers JP1/JP2 (on PD0 / PA15), read-clear delta register map +
press-latch, up to **9 encoders per hub**. Full electrical spec in the **LOCKED hub section**
below and `RODEO_HUB_I2C_CONTRACT.md`. *(Superseded: the old RP2040 4-gang `rodeo-encoder-bank`
strip — struck 2026-08-20.)*

### Budget accounting under "no hidden controls"

- **Continuous (pots):** each = 1 of the 24 ADC channels.
- **Buttons (`rodeo-button`, momentary):** each = 1 of the 12 digital GPIO. (Multi-position
  selection = press-to-cycle a button, value on OLED.)
- **Encoders:** via the I2C encoder-hub — **do not** touch 24/12. Up to 9 per hub; add hubs
  (0x30–0x33) if ever needed. Bus-limited, not pin-limited.
- **OLED:** I2C @ 0x3C, shares J42 with the encoder-hub (0x30 — no collision).

I2C bus is now a shared managed resource I own: address allocation, pull-up value sized for
the total daisy-chain capacitance, and chain-length limits.

---

## Known issues / flags

### 1. ✅ RESOLVED — ADC SPI MOSI/MISO orientation correct & bench-verified (2026-08-15)

**Status: CLOSED.** The current local KiCad is drawn correctly and the ADC path is proven in
silicon — no reroute, not a fab blocker. Verified this session: ADC `DIN` (U3/U4 pin 10)
─R16─ `SPI_MOSI` → A1 pad11 `SPI1_PICO` (controller-out/MOSI); ADC `DOUT` (U3/U4 pin 12)
─R13─ `SPI_MISO` → A1 pad10 `SPI1_POCI` (controller-in/MISO). So the MCU drives DIN and reads
DOUT — correct. Bench (2026-08-14): `04_adc_1ch` read live pot data on U3; `05_adc_scan`
streams U4. The old "crossed" description was a stale snapshot and no longer applies.
Owner: Bumble. Blocks firmware ADC bring-up (Fizz).

### 2. No on-board I2C pull-ups (Bumble to size once chain length is known)

The I2C header sheet has no pull-up resistors. Plan: firmware runs standard-mode 100 kHz at
boot (Fizz), bumps to 400 kHz once I size the pull-ups for the final chain. Pull-up value
depends on total bus capacitance = (# encoder-banks + OLED + cable) — so this resolves with
the control wishlist. Target ~2.2–4.7 kΩ to +3V3; if the daisy chain is long, a single
pull-up pair at the host end sized for the aggregate load. Encoder-banks 0x30–0x37 and OLED
0x3C/0x3D don't collide; firmware enumerates at boot.

### 3. ~~Pixel data pin~~ — MOOT (`rodeo-pixel` dropped, LEDs removed 2026-08-20)

Resolved by removal: all panel LEDs were cut (OLED is the sole feedback surface), so there is
no pixel data pin to assign. `DIG_11 / PA6 / J40` is now simply one of the **6 spare digital
connectors** (J36–J41). The prior SK6812 data-level-vs-supply question no longer applies to the
panel; the hub's on-board LED chain (fw-stubbed, future upgrade) carries its own power/level
choice in the LOCKED hub section if it's ever revisited.

### 4. UART header vs MIDI on USART1 — DECIDED: MIDI wins (Fizz)

Firmware claims USART1 for MIDI (libDaisy `MidiUartTransport`, no overrides). **J44 stays
unpopulated by default.** Aux serial, if ever needed, goes to USB-CDC or a reassigned
digital pin — never USART1. No board change.

### 5. Root schematic does not instantiate subsheets — PCB is the net authority.

### 6. No pull-ups on ADC CS nets — Rev-B hardening (Bumble owns)

`SPI_ADC1_CS` (U3, seed::D25/PA0) and `SPI_ADC2_CS` (U4, seed::D26/PD11) have **no pull-up
resistors** — each floats whenever the MCU pin is Hi-Z (power-on before firmware, MCU reset,
DFU reflash). On the shared SPI1 bus a floating-low CS selects that chip; if a sibling is
mid-transaction both drive the shared MISO → contention. This already bit us once as a
firmware bug (2026-08-15: `05_adc_scan` U3 presence-check failed because U4's CS floated
during U3's probe — the two-chip init didn't park both CS high first). **Firmware fix landed**
(commit `611f48a`: `RodeoAdc::Init` now parks both CS idle-high before probing either), which
is sufficient for this board.

**Rev-B change:** add **10 kΩ pull-ups to +3V3 on both CS nets** so a Hi-Z CS can never select
a chip or cause contention regardless of firmware state — covers the boot/reset/DFU window
firmware can't. Not a fab blocker for the current board; fold into the next spin. Owner: Bumble.

---

## Open items — need input

- [x] **Control wishlist** — Honey's Voice Inventory v0.1 (2026-08-15): 37 analog (26 pot +
      9 rotary sel + 2 wheel) + 1 toggle. Drives the 36-ch expansion below.
- [x] **3rd-ADS114S08 feasibility** (Honey's gating question) — **YES** (CS on PG10/pin 8),
      but needs a respin → deferred to Rev-B (see upgrade-path section).
- [x] **A-vs-B vibe call** (Derek, 2026-08-15) — **Path B** (board already produced). Class
      allocation locked above.
- [x] **Pot → J6–J29 ordering** (2026-08-15) — **FROZEN.** Honey's Panel Layout v1 delivered
      the physical L→R sequence; mapped 1:1 onto J6–J29 (see the 🔒 frozen pot-by-pot table in
      the Path B section). Chip boundary kept mid-FILTER (no scan penalty; parallel convert).
- [x] **Octave placement** (2026-08-15) — OSC1/OSC2 octave → **encoders** (bank 0x31).
- [x] **Momentary-button satellite** (2026-08-15) — `rodeo-switch` is SPDT-latching (covers
      the hard-sync toggle only); Honey adding a **`rodeo-button`** momentary variant for the
      5 cycle-buttons. Same 2-pin sig/GND interface, STM32 internal pull-up.
- [x] **Wave-select feel** (2026-08-21) — RESOLVED. Rotary switches dropped; **Derek chose
      encoders (option b)**. OSC1/OSC2 Wave → encoder-hub slots 7/8 @0x30; ch0/ch3 (J6/J9) kept
      as spare ADS headroom (leave-the-gaps, no re-pack). Firmware persists waveform + encoder
      state across power cycles, value on OLED.
- [ ] **SPI swap fix** — Bumble to reroute once Derek closes the KiCad session and approves
      the pre-fab change.
- [ ] **Pixel data GPIO** — assign with Fizz (Issue 3).
- [ ] **I2C pull-ups** — confirm source (Issue 2).
- [ ] **Sampling strategy** — with Fizz: ADS114S08 data rate, per-channel scan cadence,
      averaging/oversampling (DRDY# is unconnected, so it's polled).

## Change log

- 2026-09-13 — **Harness cable orientation documented** after first hub bring-up. The ecosystem
  has no single pin-1 convention: I²C, encoder and button runs are straight-through, but the 24
  analog runs are mirrored (motherboard `+3V3/sig/GND` vs satellite `GND/sig/supply-in`) and need
  **reversed** cables — a straight build reads every pot backwards. Encoder-poll burst length
  corrected 12 → 16 bytes to match `RODEO_HUB_TELEMETRY_LEN` and the contract.
- 2026-08-23 — **Digital-button pinning FROZEN.** 6 momentary `rodeo-button`s bound to
  **DIG_1–6 / J30–J35 / Seed pins 2–7** in physical signal-flow order (Hard-sync, Filter Type,
  Keytrack, LFO Shape, LFO Dest, Voice Mode). J36–J41 (DIG_7–12, ADC-capable pins 22–27) kept
  SPARE on purpose. Clears Fizz's cycle-button GPIO stub — control map now frozen on all three
  control classes (analog / digital / encoder). Retired the stale SPDT-toggle + pixel-on-DIG_11 notes.
- 2026-08-22 — **Voice v2 = "less-complicated MiniFreak"** (Derek): 12-voice poly, osc
  subtractive+FM+formant, polyphonic SVF, Filter+Amp ADSR + 1 LFO single-dest (no matrix).
  **OSC fork resolved — Option A:** per-osc **Shape → model-dependent Timbre** (PW/FM-index/
  formant-pos by engine), Wave encoders → **engine select**, Vowel pot unchanged. **Semantics
  only — zero channel remap, stays 22/24, ch0/ch3 spares preserved.** Filter/mod/mix/global/voice
  block confirmed 1:1 with the new voice. Confirmed the electrical/physical contract FROZEN for
  Fizz to bind; he's standing up `rodeo-001/firmware` (consume-not-copy). ADC bench-verified on
  both ADCs. Encoders/hub ordered (~1 wk) → motherboard-voice-first, hub firmware later.
- 2026-08-21 — **Control set locked to 4 types (pot / encoder / button / OLED); rotary switches
  + LEDs removed** (Derek). **Wave-select = ENCODERS (option b)** after a 3-way (button/encoder/pot)
  eval: OSC1/OSC2 Wave → encoder-hub slots 7/8 @0x30 (hub 8/9). **ch0/ch3 (J6/J9) freed as spare
  ADS** — kept as gaps (no re-pack). Encoder-bank (RP2040 4-gang) struck; superseded by the
  hub-and-satellite `rodeo-encoder-hub` (STM32C031C6) + `rodeo-encoder` singles. Panel = 37
  (22 pot + 8 enc + 6 btn + 1 OLED). Fizz to persist waveform + encoder state across power-cycle
  (encoders are incremental), value on OLED. Supersedes the 2026-08-15 "Wave as rotary switches" entry.
- 2026-08-15 — **Pot-by-pot table FROZEN** from Honey's Panel Layout v1. Mapped her physical
  L→R ADC sequence 1:1 onto J6–J29 via `ch = chip·12 + AIN`: ch0–11 = U3/J6–J17 (both OSC
  blocks + MIX + Cutoff/Reso/EnvAmt), ch12–23 = U4/J18–J29 (Vowel + both ADSRs + LFO + Volume).
  Wave rotaries at ch0/ch3. Kept the mid-FILTER chip boundary (ch11│ch12) un-rotated — parallel
  ΔΣ convert ⇒ no scan-grouping penalty. This is the firmware `board.h` binding contract.
- 2026-08-15 — **Wheels removed (Derek): desktop synth, pitch/mod over MIDI.** Re-locked Path B:
  35 analog (26 pot + 9 sel). 24 ADS = 22 pots + **OSC1/OSC2 Wave as 4-pos rotary switches**
  (Honey re-spent the 2 freed channels); digital = 5 cycle-buttons + 1 toggle (6 of 12);
  encoders = bank 0x30 (4 trims) + **0x31 (2 octaves)**. Octaves→encoders and the new
  **`rodeo-button`** momentary satellite confirmed. Wheel native-ADC release valve replaced
  by "demote a Wave rotary to a button" for a spare channel.
- 2026-08-15 — **Path B locked (Derek): build on the produced board, no respin.** Froze the
  Path B class allocation (24 ADS = 22 pots + 2 wheels; 7 enum cycle-buttons + hard-sync
  toggle on digital; 4 trims on 1 encoder-bank @ 0x30; octave placement TBD). Recorded new
  Path-B electrical contract points (button pull-ups = STM32 internal, I2C pull-up sizing now
  actionable, wheel native-ADC release valve). Demoted the 36-ch/3rd-ADC work to a documented
  **Rev-B upgrade path**. Pot→connector ordering deferred to a layout sync with Honey.
- 2026-08-15 — Answered Honey's gating question: **3rd ADS114S08 (U5) on SPI1 is feasible**
  (Path A). CS = PG10/Seed pin 8 (NC hardware NSS); parallel ΔΣ conversion ⇒ ~zero added scan
  latency; ch 24..35 = U5 AIN0..11 = J50..J61; two wheels ride Seed native ADC (pins 22/23).
  Recorded the 36-ch expansion section, closed the wishlist + feasibility open items.
- 2026-08-08 — Rewritten to verified topology from KiCad + firmware hardware-map. Confirmed
  2× ADS114S08 on SPI1, full JST connector map, Daisy pin map, module classification.
  Recorded the critical SPI MOSI/MISO swap (Issue 1) and the satellite-hub architecture
  (supersedes the single co-designed control-PCB seam).
- 2026-08-08 — Doc created; budget + "no hidden controls" recorded (provisional).

## 🔒 Encoder Hub — LOCKED (2026-08-20)

Board `rodeo-encoder-hub` (was `rodeo-encoder-bank`). **STM32C031C6** (LQFP-48, Cortex-M0+,
48 MHz, 32 KB flash / 12 KB RAM) acting as an **I2C target**; the motherboard/Daisy is
master. Verified against the KiCad schematic + PCB this session.

**Locked parameters (Derek 2026-08-20):**
- **9 encoders per hub.** Dumb `rodeo-encoder` satellites (Alps EC11, 4-pin JST-SH
  `1=A 2=B 3=S 4=GND`, common→GND, active-low). No on-board pull-ups → **hub enables STM32
  internal pull-ups.** Read by a ~1 kHz Gray-code poll ISR (Fizz) — EXTI per-edge is out
  (shared interrupt lines collide across ports).
- **I2C1 target:** `PB6=SCL`, `PB7=SDA`.
- **Address = 2 cuttable solder jumpers** (JP1, JP2) on **PD0 + PA15**. Each: 10 kΩ pull-up
  to +3V3 (R2/R3) + `SolderJumper_2_Bridged` to GND. Default bridged = 0 → **base 0x30**;
  cut a jumper for a 1. → **4 addresses, 0x30–0x33** (2 bits; the 3-bit/8-addr option was
  dropped). Address is set in **firmware** from these GPIO at boot — the jumpers are GPIO
  straps, not hardware address pins.
  - **Bit map (silicon-verified 2026-08-20):** `JP1 → PD0 (pin38) = I2C_ADDR_1 = bit0` (+0x01);
    `JP2 → PA15 (pin37) = I2C_ADDR_2 = bit1` (+0x02). Address = `0x30 | (PA15<<1) | PD0`.
    Cut JP1 → +1, cut JP2 → +2. Silkscreen JP1="A0", JP2="A1".
- **Per-slot A/B/S is uniform** (silicon-verified): slot = connector, `A`=pin1/`_A` net,
  `B`=pin2/`_B` net, `S`=pin3/`_S` net, across all 9. Pins: E1 PA0/PA1/PA2, E2 PA3/PA4/PA5,
  E3 PA6/PA7/PB0, E4 PB1/PB2/PB10, E5 PB11/PB12/PB13, E6 PB14/PB15/PA8, E7 PA9/PC6/PC7,
  E8 PA10/PA11/PA12, E9 PB3/PB4/PB5. Because A/B ordering is uniform, **one** CW-sign
  convention covers all 9 (firmware `CONFIG.ENC_DIR_INVERT` flips globally if bench reads CCW).
- **I2C PULL-UPS — REV-A on the hub (Derek + Bumble 2026-08-20).** The **produced motherboard
  has no I2C pull-ups** (Issue 2) and the Daisy's internal ~40 kΩ pull-ups are far too weak for
  I2C (rise time ~µs vs the <300 ns needed at 400 kHz) → external pull-ups mandatory. Bus =
  **OLED 0x3C + encoder hub(s)** (OLED is on I2C, not SPI). **DECIDED (Derek 2026-08-20): the
  encoder hub is the "always-present" anchor for Rev-A** — the HiLetgo 2.42" SSD1309 module has
  **no onboard pull-ups**, so the hub carries them. **Spec (final, Derek 2026-08-20): 2× 2.2 kΩ, SDA→+3V3
  and SCL→+3V3 (the Qwiic/MCU +3V3 rail — NOT `LED_3V3`), placed near J1/J2. NO solder jumper**
  (dropped for space). Consequence: the OLED shares the bus, so the hub is a **mandatory bus
  member** — no hub, no I2C (incl. OLED). Accepted. **Pairing discipline (replaces the jumper):**
  every Rev-A hub ships with a Rev-A (no-pull) motherboard 1:1, and **both** boards get the Rev-B
  update together — so there is never a Rev-A hub (with pulls) on a Rev-B motherboard (with
  pulls). If revisions are ever mixed, the parallel 2.2 k‖2.2 k ≈ 1.1 kΩ (~3 mA sink, at the
  fast-mode limit) → lift the hub resistors in that case. **Rev-B:** pulls move to the
  motherboard; the respun Rev-B hub drops these two resistors.
- **Hub count for the current voice:** the Path-B voice needs **8 encoders** (OSC2 Detune,
  Unison Detune, Glide, Master Tune, OSC1 Octave, OSC2 Octave, **OSC1 Wave, OSC2 Wave** —
  Wave added 2026-08-21) → they fit on **ONE hub @0x30**, **8 of 9 slots used, 1 spare.** A
  second hub (0x31…) is only warranted if a future voice exceeds 9 encoders. **Slot↔parameter
  binding LOCKED (see the Path-B "Encoder-hub index↔param table" above): slots 1–8 assigned,
  slot 9 spare; physical hub-header↔slot wiring confirmed with Honey off her v2 harness.**
- **LED subsystem (opt-in):** `PB8 = TIM16_CH1` → 330 Ω (R1) → **J13** (JST-SH 3-pin,
  `GND / LED_3V3 / DATA`) drives a WS2812/SK6812 chain out to satellites (LED-IN/OUT chain).
  Power from **J14 (JST-ZH)** motherboard feed; **`LED_3V3` is a rail separate from the MCU
  `VDD`/Qwiic supply, common GND**, 100 µF bulk (C at J14). Use **SK6805 / SK6812-mini @
  3.3V** (no level shifter); size total chain **< 1 A** (ZH + SH both ~1 A).
- **SWD:** J12 (1.0 mm 1×5, GND/+3V3/SWDIO/SWCLK/NRST).
- **Hard-sync:** handled by a **momentary `rodeo-button`** (2-pin SIG/GND) + firmware toggle.
  `rodeo-rotary-switch` is **dropped** — no dedicated toggle satellite.

**Status:** buildable / fab-ready pending ERC + DRC (no `kicad-cli` on this box → Derek runs
Inspect→ERC/DRC in KiCad). **Open (product, not blocking fab):** the LED chain has no host
board yet (pixel satellite dropped, encoder satellite carries no LEDs) — decide a future
encoder-with-ring variant vs a slim pixel board before the LED driver is wired for real.
