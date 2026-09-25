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

## Pots — 16 runs, REVERSED cables (straight also works — see below)

Panel positions read left-to-right, top-to-bottom; R1C1 is top-left.

| Panel | Main layer | Shift layer | Motherboard |
|---|---|---|---|
| R1C1 | Harmonics | Env→Timbre | **J6** |
| R1C2 | Timbre | Env→Morph | **J7** |
| R1C3 | Morph | LFO rate | **J8** |
| R1C4 | Character amount | Bias | **J9** |
| R2C1 | Cutoff | Filter type *(stepped)* | **J10** |
| R2C2 | Resonance | LFO depth | **J11** |
| R2C3 | Filter env | LFO dest *(stepped)* | **J12** |
| R2C4 | Movement amount | Rate | **J13** |
| R3C1 | Attack | Spread | **J18** |
| R3C2 | Decay *(= release)* | Glide | **J19** |
| R3C3 | Sustain | Master volume *(global)* | **J20** |
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

**Do not sort them by eye, and do not trust the packaging.** Vendors sell these
as "same direction" / "identical" / "1:1" versus "reverse direction" /
"opposite direction" / "reverse-side-head", and on 2026-09-23 a bag labelled
*reverse direction* turned out to be electrically 1:1 — it worked on an encoder
run, which needs 1:1. A "contacts on the same face" heuristic is no better: it
describes how the ribbon looks, and which pin lands where flips with how you
hold the two ends. **Meter it, or match it against a cable known to work.**

For reference only, the colour check (hold each connector the same way —
contacts facing you, cable running away — and read left to right):

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

Off-the-shelf **Qwiic / STEMMA QT cables are 1:1**, so they drop straight into
the encoder and I²C runs — a known-good reference worth keeping on the bench.

**Which run needs which**, by electrical mapping rather than by name:

| Run | Needs | Because |
|---|---|---|
| Encoders (hub J3–J11 ↔ satellite J1) | **1:1** | both ends are pinned A/B/S/GND |
| I²C (motherboard J42 ↔ hub J1/J2) | **1:1** | both ends GND/VDD/SDA/SCL |
| Pots (motherboard J6–J29 ↔ satellite J1) | **crossed** | satellite is `GND/signal/supply`, the mirror of the motherboard |

## Cable types, and what the wrong one does

**Never judge a cable by wire colour.** Motherboard connectors are vertical and
satellite connectors horizontal, so the ribbon lands mirrored even on a correct
cable. Ohm out pin 1 at both ends.

* **Pots are spec'd REVERSED** (pin 1 → last pin): the satellite is pinned
  `GND / signal / supply-in`, the mirror of the motherboard. **Built straight,
  the firmware copes** — measured on this panel 2026-09-23. Nothing is damaged
  (the satellite is passive); supply and ground swap, so the travel becomes
  0.30–3.30 V instead of 0.00–3.00 V. `panel_pot.h` detects that (a correct
  satellite cannot read above ~3.05 V through its 1 kΩ resistor) and rescales.
  **Direction is unaffected** — that depends on the pot's pin orientation, not
  on the cable, and on this panel it reads correctly either way. The dump marks
  a rescaled channel with `S`.
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
3. **Turn it up.** The value bar should fill. If it empties, that pot's own
   orientation is reversed — not the cable; see the note on straight cables
   above.
4. **The bar may not move at all at first** — that is pickup, not a fault. The
   pot has to reach the value the patch already holds; the screen shows `UP` or
   `DN` for which way to turn.
5. **There is nothing to calibrate.** Each wiring's travel is fixed in the
   firmware and inset by a small dead zone, so every pot within tolerance
   reaches a true 0 and a true 1 on its first turn. The cost is about twelve
   degrees of dead travel at each stop, which is the deliberate trade: full
   access to the value range beats a perfectly fitted one.

**Calibration by measurement was tried and removed (2026-09-24).** Learning each
channel's real endpoints is the obvious improvement and it was worse on
hardware: a learned end is only as good as the worst sample ever taken, so one
reading from a settling ADC or a dry joint moved it for good, and *"all but one
is unable to reach 0"*. Persisting it to flash made that permanent. The fixed
ranges have no state to rot.

**The one thing still stored is the wiring** — which runs were built straight —
because that is a fact about the cable, not a measurement. **Either stop proves
it:** only a swapped run can read above 3.15 V, and only a correct one can read
below 0.20 V, in both cases for several readings in a row so a spike cannot set
it. The verdict is then written to flash.

**The two ends are not equally trustworthy.** A high reading is proof on its own
— nothing that has not converted reads 3.3 V. A low one counts only once the
channel has been seen above 0.5 V at some point, because **the platform's ADC
cache is zero-initialised**: every channel reads exactly 0.00 V until its first
conversion lands, and a full 16-channel scan takes ~19 ms while the panel loop
runs at 25 kHz. Those zeros looked exactly like proof of correct wiring, so on
2026-09-25 every channel was being marked correctly wired at every boot —
overwriting the map that had *just* been restored from flash. A supply-swapped
pot then mapped its true bottom of 0.30 V to 0.065 and could not reach zero
until it was swept to the top, which flipped the verdict back. Symptom: *"I
frequently have to re-sweep knobs to them to reach 0."*

**Until a run proves itself it is treated as neither**, and mapped so that both
wirings reach a true 0 and 1 — at the cost of more dead travel than either needs.
Assuming correct wiring instead is what broke pickup on 2026-09-24: a swapped pot
read 0.065 at its stop rather than 0, so any parameter sitting at 0 could never
be caught and the screen showed `DN` for ever. Turning each pot to either stop
once clears it for good. **If you change a satellite or a cable, hold the MAIN
encoder while powering up** to clear it; the log says `panel wiring: CLEARED`,
and turning each pot fully up once re-establishes it.

For the encoders: turning one should name an effect type, a tap should strike
the slot through, and holding SPACE for over half a second should show `FREEZE`.
Holding MAIN should show `SHIFT`, after which the pots address their shift-layer
parameters.

## Symptoms seen on first assembly (2026-09-23)

Kept because each of these took a while to pin down, and each has a signature
that identifies it immediately next time.

| Symptom | Cause |
|---|---|
| Pots read 0.30–3.30 V instead of 0.00–3.00 V, never reaching zero | Analog run built with a **straight** cable: supply and ground swapped. Direction is unaffected. **Handled in firmware** (`panel_pot.h` detects it — a correct satellite cannot exceed ~3.05 V — and rescales), so these cables can stay. |
| One channel pegged at exactly 0.00 V through a full sweep | Dead pot (confirmed by moving that satellite to another connector). Not pickup, not calibration: no signal at all. The faulty pot on this build came off **R3C4** and was swapped onto **J25** to prove it was the pot and not the channel, so the dead channel is now **J25, Space Mix** — the fault follows the pot, not the connector. The firmware pins that parameter to a fixed mix (`kPinSpaceMix` / `kSpaceMixValue`, currently 50%) until the pot is replaced, because a pot stuck at 0 otherwise holds the mix at zero. |
| **Turning** an encoder registers as a **tap on that same encoder**, and no counts | Encoder run built with a **reversed** cable. It lands the encoder's B line on the hub's S pin, so every detent looks like a button press, while the encoder's common sits on a pulled-up input instead of ground so quadrature cannot work. **Not fixable in firmware** — rebuild the cable straight. |
| A high-pitched hum on the line out that gets louder with each key held | **Not firmware** — it persists with master volume at zero, and host renders of the whole signal path contain no fixed-frequency component. The audio callback's per-block current draw scales with voice count and couples into the output through VIN and the shared ground. Logged as **Issue 4 in the motherboard's [`hardware-map.md`](../../rodeo-motherboard/docs/hardware-map.md)** for Rev-B review, with the bench tests that apportion it. |
| Occasional taps on slots with nothing plugged in | Floating inputs. The hub's pins are inputs with pull-ups polled at 1 kHz; unused slots wander. Harmless. |
| Hub answers I²C but reports no encoder activity at all | Check the encoder runs first (above). The hub's own `SW_STATE` register is the arbiter: it is the hub MCU's view of its pins, so nothing on the Daisy side can fake it. |
| `panel: ... encoder hub NO | OLED NO`, screen dark, pots seem dead | The I²C run to the hub. The OLED is only initialised when the hub answers, because on Rev-A the hub carries the bus pull-ups. Reseat that cable; a power cycle cleared it once. |

**Two diagnostics earn their keep here.** The synth's serial dump prints each pot's
true voltage beside its control value and the hub's own registers every 2 s; and
the startup banner carries the build date and time, because a flash that
silently did not take is otherwise indistinguishable from a firmware bug.
