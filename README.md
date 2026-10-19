# Rodeo

A modular **synthesizer hardware ecosystem**: one host *motherboard*, a family of
control-surface *daughterboards*, and *instrument instances* built by combining them.
This repository is the single home for all of it — board designs (KiCad), firmware,
and the design docs that tie them together.

## Layout

The current hardware is **Rev B**, in [`hardware/`](hardware/). Rev A, the prototype
generation that rodeo-001 and rodeo-002 were built on, is kept whole in
[`rev-a/`](rev-a/); the tree as it stood before this split is tagged `rev-a-final`.

| Directory | What it is |
|---|---|
| [`hardware/`](hardware/) | **Rev B, the boards in production.** One self-contained KiCad project per board, each with its README and JLC fab outputs. |
| [`hardware/rodeo-motherboard/`](hardware/rodeo-motherboard/) | The host board, a Daisy Seed carrier: two codecs (8 audio channels), 10 general-purpose I/O, MIDI, I²C taps, internal SD, and the satellite chain on USART3. Every decision and its reasoning: [`docs/rev-b.md`](hardware/rodeo-motherboard/docs/rev-b.md). |
| [`hardware/rodeo-pot-bank-4/`](hardware/rodeo-pot-bank-4/), [`hardware/rodeo-encoder-bank-4/`](hardware/rodeo-encoder-bank-4/) | Satellites: four pots or four encoders with switches, each read by a CH32V003 on the chain. |
| [`hardware/rodeo-power-usbc/`](hardware/rodeo-power-usbc/), [`hardware/rodeo-power-barrel/`](hardware/rodeo-power-barrel/) | Power input boards: USB-C (power and USB data), or USB-C plus a barrel jack and an LED-strip rail. |
| [`hardware/rodeo-power-switch/`](hardware/rodeo-power-switch/) | The power switch: a latching push button on the back panel (with a printed cap), cabled to the motherboard's J3. |
| [`hardware/rodeo-audio-3.5mm/`](hardware/rodeo-audio-3.5mm/), [`hardware/rodeo-midi/`](hardware/rodeo-midi/) | Rear-panel jack boards: four stereo 3.5 mm audio jacks, and TRS MIDI in and out. |
| [`hardware/panels/`](hardware/panels/) | The back panels: one FR4 panel per rear board, generated from the cut-out table and screwed to the rails. |
| [`hardware/mounting/`](hardware/mounting/) | The printed rear rail, top rail and motherboard frame, and the grid every board's holes sit on. |
| [`hardware/docs/`](hardware/docs/) | Cross-board docs, such as the [silkscreen checklist](hardware/docs/silkscreen.md). |
| [`rodeo-sdk/`](rodeo-sdk/) | **The code everything is built on**: driver libraries, the pure host-tested logic, bring-up examples, a copy-to-start template, host tools, and the vendored libDaisy/DaisySP. An instrument points `RODEO_ROOT` here and consumes it; it does not copy it. It is being rewritten for Rev B ([plan](planning/REV_B_PLAN.md)); the Rev A instruments build against the frozen Rev A SDK in `rev-a/rodeo-sdk/`. |
| [`instruments/`](instruments/) | **Reference instruments** built from the boards. [`rodeo-001`](instruments/rodeo-001/) is the first ([control map](instruments/rodeo-001/docs/RODEO_CONTROL_MAP.md), [enclosure spec](instruments/rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md)); [`rodeo-002`](instruments/rodeo-002/) is a multi-engine digital polysynth into a texture chain ending in a granular delay ([concept](instruments/rodeo-002/docs/RODEO_002_CONCEPT.md), [firmware](instruments/rodeo-002/firmware/), tagged `rodeo-002/v5.0.0`). Both run on Rev A. |
| [`rev-a/`](rev-a/) | **The Rev A prototypes**: the [motherboard](rev-a/rodeo-motherboard/) (2× ADS114S08, 24 analog channels), the [encoder hub](rev-a/rodeo-encoder-hub/) and its firmware, the encoder, button, potentiometer and rotary-switch daughterboards, the panelisation templates, and [the SDK as it was for Rev A](rev-a/rodeo-sdk/) (frozen at `rodeo-sdk/v0.2.0`, with its own libDaisy/DaisySP). Superseded, but still what the two instruments run on. |
| [`tools/`](tools/) | Host tools, including the KiCad review and JLC fab-output scripts in [`tools/kicad/`](tools/kicad/). |
| [`web/`](web/) | **The website**: an Astro site and the build log, plus the docs that govern it: [brand](planning/WEB_BRAND.md), [architecture](planning/WEB_ARCHITECTURE.md), [claims audit](planning/WEB_CLAIMS_AUDIT.md). |

> **Naming, so nobody trips on it again:** `rodeo-motherboard` is the *host board*;
> `rodeo-001` is the *first instrument built from the ecosystem*. They are different
> things.
>
> **The SDK is not the motherboard's firmware.** It serves every instrument, so it is
> its own thing at the top level, and anything in it that names a specific knob or a
> specific sound is in the wrong place. (`rev-a/rodeo-encoder-hub/firmware/` is
> different again: that really is one board's own firmware, for the STM32C0 on the hub.)

## Where the source-of-truth docs live

- **Rev B motherboard** → [`hardware/rodeo-motherboard/docs/rev-b.md`](hardware/rodeo-motherboard/docs/rev-b.md), and the satellite wire protocol in [`RODEO_SATELLITE_CHAIN_CONTRACT.md`](hardware/rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md).
- **Rev A motherboard pin map / topology** → `rev-a/rodeo-sdk/common/board.h` (generic board facts only) and [`rev-a/rodeo-motherboard/docs/hardware-map.md`](rev-a/rodeo-motherboard/docs/hardware-map.md) (verified against the PCB).
- **Encoder-hub ↔ motherboard I²C register contract** (Rev A) → [`rev-a/rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md`](rev-a/rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md).
- **ADC driver-library design** → [`planning/RODEO_ADC_LIB_DESIGN.md`](planning/RODEO_ADC_LIB_DESIGN.md).
- **rodeo-001 control map** (which physical control does what) → [`instruments/rodeo-001/docs/RODEO_CONTROL_MAP.md`](instruments/rodeo-001/docs/RODEO_CONTROL_MAP.md). This is the instance contract shared by firmware, hardware, and the panel layout.
- **rodeo-001 enclosure / panel** → [`instruments/rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md`](instruments/rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md).
- **rodeo-002 concept + control map** → [`instruments/rodeo-002/docs/`](instruments/rodeo-002/docs/): the voice and engine budget measured on hardware, and the locked panel/channel map.

## Building the firmware

See [`rodeo-sdk/README.md`](rodeo-sdk/README.md) for
the toolchain, `make deps`, and the numbered bring-up examples (blink → audio → MIDI →
ADC → poly synth → encoder scan → OLED).

## Repo notes

- One git repo; `libDaisy` and `DaisySP` are submodules (`git submodule update --init --recursive`).
- Every board is a self-contained KiCad project. A Rev B board keeps its own symbols, footprints and 3D models in its `lib/`; everything else comes from KiCad's standard libraries. Nothing points outside the board's folder, so a folder can be moved or copied on its own.
