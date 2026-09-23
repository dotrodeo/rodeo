---
title: "rodeo-002 Harness — what plugs in where"
tags: [rodeo, rodeo-002, harness, build, wiring]
status: active
created: 2026-09-23
updated: 2026-09-23
---

# rodeo-002 Harness — what plugs in where

The build sheet for wiring the panel. Derived from the frozen channel map in
[`RODEO_002_CONTROL_MAP.md`](RODEO_002_CONTROL_MAP.md) and **verified against the
firmware that reads it** (`rodeo-002/firmware/synth/src/main.cpp`, `kPotChannel`
and `kEncoderSlot`). If this and the control map ever disagree, the control map
wins and the firmware is wrong.

## Pots — 16 runs, REVERSED cables

Panel positions read left-to-right, top-to-bottom; R1C1 is top-left.

| Panel | Main layer | Shift layer | Motherboard |
|---|---|---|---|
| R1C1 | Harmonics | Env→Timbre | **J6** |
| R1C2 | Timbre | Env→Morph | **J7** |
| R1C3 | Morph | LFO rate | **J8** |
| R1C4 | Character amount | Bias | **J9** |
| R2C1 | Cutoff | Filter type *(stepped)* | **J10** |
| R2C2 | Resonance | Filter env amount | **J11** |
| R2C3 | Spread | LFO dest *(stepped)* | **J12** |
| R2C4 | Movement amount | Rate | **J13** |
| R3C1 | Attack | Decay | **J18** |
| R3C2 | Release | Sustain | **J19** |
| R3C3 | Master volume *(global)* | LFO depth | **J20** |
| R3C4 | Texture amount | Tone | **J21** |
| R4C1 | Space Time | Grain pitch | **J22** |
| R4C2 | Space Feedback | Grain spread | **J23** |
| R4C3 | Space Grain | Tilt EQ | **J24** |
| R4C4 | Space Mix | Damp | **J25** |

**Mind the gap:** rows 1–2 land on J6–J13 and rows 3–4 start at **J18, not J14**.
That is the 8 + 8 split across the two ADC chips — both convert in parallel, so
eight channels each reads every pot ~108 times a second instead of ~72.
**J14–J17 and J26–J29 stay empty.**

## Encoders — 5 runs, STRAIGHT cables

| Panel | Encoder | Hub connector | Hub slot |
|---|---|---|---|
| R1C5 | CHARACTER | **J3** | 1 |
| R2C5 | MOVEMENT | **J4** | 2 |
| R3C5 | TEXTURE | **J5** | 3 |
| R4C5 | SPACE — long-press = Freeze | **J6** | 4 |
| R4C6 | **MAIN** — hold = shift | **J7** | 5 |

Hub slots 6–9 (J8–J11) stay empty.

## Bus and power

| Link | From | To | Cable |
|---|---|---|---|
| I²C | motherboard **J42** | hub J1 or J2 | straight (Qwiic/STEMMA works) |
| OLED | hub's other I²C connector | OLED module | straight |
| MIDI | TRS jack J2 (IN) | keyboard | — |
| Audio | TRS jack J5 (OUT) | amp/interface | — |

**The hub must be plugged in for the OLED to work.** On Rev-A the hub carries
the I²C pull-ups — the motherboard has none and the display module has none — so
no hub means no I²C at all, and the screen stays dark.

## Telling a straight cable from a reversed one

Both ends of an encoder run are pinned the same way -- hub `J3` and satellite
`J1` are both **1=A, 2=B, 3=S, 4=GND** -- so the run needs a **1:1 cable, pin 1
to pin 1**.

**The test that cannot lie.** Unplug the cable, set a meter to continuity, probe
one end's pin 1 against the other end's pin 1:

* **beeps → straight (1:1)** — what the encoders and I²C need;
* **silent, but beeps against pin 4 → reversed** — what the pots want.

**The visual rule.** Hold each connector the same way — **contacts facing you,
cable running away from you** — and read the wire colours left to right:

```
STRAIGHT                              REVERSED
 end A            end B                end A            end B
 ┌─────────┐      ┌─────────┐          ┌─────────┐      ┌─────────┐
 │ 1 2 3 4 │======│ 1 2 3 4 │          │ 1 2 3 4 │======│ 1 2 3 4 │
 │ blk red │      │ blk red │          │ blk red │      │ ylw wht │
 │ wht ylw │      │ wht ylw │          │ wht ylw │      │ red blk │
 └─────────┘      └─────────┘          └─────────┘      └─────────┘
   A B S GND        A B S GND            A B S GND        GND S B A
   same order at both ends                mirrored
```

Equivalently, and how they are sold: a **straight** cable has its exposed metal
contacts on the **same face** of the ribbon at both ends; a **reversed** one has
them on opposite faces. Off-the-shelf **Qwiic / STEMMA QT cables are straight**,
so they drop straight into the encoder and I²C runs.

The catch is that holding the two ends *the same way* is the part people get
wrong — see below for why the colours on the board will tell you the opposite of
the truth.

## Cable types, and what the wrong one does

**Never judge a cable by wire colour.** Motherboard connectors are vertical and
satellite connectors horizontal, so the ribbon lands mirrored even on a correct
cable. Ohm out pin 1 at both ends.

* **Pots need REVERSED cables** (pin 1 → last pin). The satellite is pinned
  `GND / signal / supply-in`, the mirror of the motherboard. Built straight,
  nothing is damaged — everything on the board is passive — but **the pot reads
  backwards**, which looks exactly like a firmware sign bug.
* **Encoders and I²C need STRAIGHT cables.** A reversed I²C cable grounds SCL,
  ties SDA to 3V3 and back-powers the hub through its SDA pin: the whole bus
  dies, OLED included. A reversed encoder cable simply never counts.

## Bringing it up

The firmware probes each of the pots, hub and OLED at startup and runs without
any of them, so wire it in any order and reflash nothing. The serial log says
what it found:

```
panel: pots yes | encoder hub yes | OLED yes
```

Then, per pot:

1. **Unplugged channels float**, so the screen will show parameters changing on
   their own until everything is connected. That is expected and stops channel
   by channel as you plug them in.
2. **Turn the pot.** The screen should name the parameter from the table above.
   A different name means the cable is on the wrong connector.
3. **Turn it up.** The value bar should fill. If it empties, that run was built
   straight instead of reversed.
4. **The bar may not move at all at first** — that is pickup, not a fault. The
   pot has to reach the value the patch already holds; the screen shows `UP` or
   `DN` for which way to turn.

For the encoders: turning one should name an effect type, a tap should strike
the slot through, and holding SPACE for over half a second should show `FREEZE`.
Holding MAIN should show `SHIFT`, after which the pots address their shift-layer
parameters.
