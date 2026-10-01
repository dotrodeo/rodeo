# Rodeo

A modular **synthesizer hardware ecosystem**: one host *motherboard*, a family of
control-surface *daughterboards*, and *instrument instances* built by combining them.
This repository is the single home for all of it — board designs (KiCad), firmware,
and the design docs that tie them together.

## The pieces

| Directory | What it is |
|---|---|
| [`rodeo-sdk/`](rodeo-sdk/) | **The code everything is built on** — driver libraries, the pure host-tested logic, bring-up examples, a copy-to-start template, host tools, and the vendored libDaisy/DaisySP. An instrument points `RODEO_ROOT` here and consumes it; it does not copy it. |
| [`rodeo-motherboard/`](rodeo-motherboard/) | **The host board** — an Electrosmith Daisy Seed carrier. Audio I/O + MIDI, a 24-channel external ADC surface (2× ADS114S08 over SPI1), and the I²C control bus. The [`kicad/`](rodeo-motherboard/kicad/) board and hardware [`docs/`](rodeo-motherboard/docs/). |
| [`rodeo-encoder-hub/`](rodeo-encoder-hub/) | **Encoder aggregator** — an STM32C031C6 that reads a bank of encoders and presents them to the motherboard as an I²C target. Has its own bare-metal `firmware/`. Wire contract: [`RODEO_HUB_I2C_CONTRACT.md`](rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md). |
| [`rodeo-encoder/`](rodeo-encoder/) | Encoder daughterboard (rotary encoders + pushbutton) that feeds a hub. |
| [`rodeo-button/`](rodeo-button/) | Momentary-button daughterboard. |
| [`rodeo-potentiometer/`](rodeo-potentiometer/) | Potentiometer daughterboard — analog, into the motherboard's ADC channels. |
| [`rodeo-rotary-switch/`](rodeo-rotary-switch/) | Rotary-switch daughterboard (resistor-ladder → ADC). |
| [`rodeo-panels/`](rodeo-panels/) | KiCad **panel templates** for fabbing daughterboards in arrays — `Panel_2x8` is sixteen up. Templates only: no schematic of its own to build. |
| [`rodeo-001/`](rodeo-001/) | **The first instrument instance** — the specific synth built from the boards above. Instance-only material: the [control map](rodeo-001/docs/RODEO_CONTROL_MAP.md), the [enclosure spec](rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md), and its instance KiCad. |
| [`rodeo-002/`](rodeo-002/) | **The second instrument instance** — a multi-engine digital polysynth into a Chroma-Console-style texture chain ending in a granular delay. 16 pots, 6 encoders, OLED, no buttons. [Concept](rodeo-002/docs/RODEO_002_CONCEPT.md), [control map](rodeo-002/docs/RODEO_002_CONTROL_MAP.md), and [firmware](rodeo-002/firmware/) — playable, tagged `rodeo-002/v5.0.0`. |

> **Naming, so nobody trips on it again:** `rodeo-motherboard` is the *host board*;
> `rodeo-001` is the *first instrument built from the ecosystem*. They are different
> things. The motherboard's KiCad project is named `rodeo-motherboard.*` (an earlier
> partial rename left it as `rodeo-001` — now corrected).
>
> **The SDK is not the motherboard's firmware.** It lived at
> `rodeo-motherboard/firmware/` until 2026-10-01, which read as if it belonged to one
> board. It serves every instrument, so it is its own thing at the top level — and
> anything in it that names a specific knob or a specific sound is in the wrong place.
> (`rodeo-encoder-hub/firmware/` is different again: that really is one board's own
> firmware, for the STM32C0 on the hub.)

## Where the source-of-truth docs live

- **Motherboard pin map / topology** → `rodeo-sdk/common/board.h` (generic board facts only) and [`rodeo-motherboard/docs/hardware-map.md`](rodeo-motherboard/docs/hardware-map.md) (verified against the PCB).
- **Encoder-hub ↔ motherboard I²C register contract** → [`rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md`](rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md).
- **ADC driver-library design** → [`rodeo-sdk/docs/RODEO_ADC_LIB_DESIGN.md`](rodeo-sdk/docs/RODEO_ADC_LIB_DESIGN.md).
- **rodeo-001 control map** (which physical control does what) → [`rodeo-001/docs/RODEO_CONTROL_MAP.md`](rodeo-001/docs/RODEO_CONTROL_MAP.md). This is the instance contract shared by firmware, hardware, and the panel layout.
- **rodeo-001 enclosure / panel** → [`rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md`](rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md).
- **rodeo-002 concept + control map** → [`rodeo-002/docs/`](rodeo-002/docs/): the voice and engine budget measured on hardware, and the locked panel/channel map.

## Building the firmware

See [`rodeo-sdk/README.md`](rodeo-sdk/README.md) for
the toolchain, `make deps`, and the numbered bring-up examples (blink → audio → MIDI →
ADC → poly synth → encoder scan → OLED).

## Repo notes

- One git repo; `libDaisy` and `DaisySP` are submodules (`git submodule update --init --recursive`).
- KiCad daughterboards are drag-drop/Qwiic-style parts; each is a self-contained project.
