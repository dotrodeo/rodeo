---
title: "rodeo-demo over MIDI — the protocol the phone app needs"
tags: [rodeo, rodeo-demo, firmware, midi, sysex, mobile, app]
status: built in rodeo-demo (params.cpp, remote.cpp) and tested on a Rodeo, 2026-11-07
created: 2026-11-07
updated: 2026-11-07
---

# rodeo-demo over MIDI

Phase 1 of [the mobile plan](RODEO_MOBILE_PLAN.md). The app talks to Rodeo over
USB-MIDI only (an iOS app can't open the serial port), so everything it needs has to
go over MIDI. Today the firmware understands notes, pitch bend (±2), sustain
(CC 64) and all-notes-off (CC 120, 123), from USB and the TRS jack alike, and the
state goes out only on the serial port's `#S` line.

This adds two layers:

1. **Standard MIDI for anyone:** a CC map and Program Change, so any controller or
   DAW can drive the synth. 7-bit, standard numbers where they exist.
2. **SysEx for the app:** finding the Rodeo, a self-describing parameter table,
   14-bit parameter sets, the state as a stream, and the preset and sound lists.

Both end up in one function that sets a parameter, so a knob, a CC and the app
all behave the same way. **Nothing on the serial port changes**: `s`, `j`, `J`
and the rest stay as they are for rodeo-console.

## Parameters

One table, keyed by **the preset files' own keys** (`presets.h`), so a preset, the
state and the app all use the same names. Values are integers in the units below
(the preset files' units, with the pots' parameters in ‰ rather than %, for the
resolution). The firmware clamps whatever it's sent.

| id | key | range | unit | on the panel |
|---|---|---|---|---|
| 0 | `cutoff` | 0–1000 | ‰ of the pot's travel | pot 1 |
| 1 | `resonance` | 0–1000 | ‰ | pot 2 |
| 2 | `env` | 0–1000 | ‰ | pot 3 |
| 3 | `release` | 5–4000 | ms | pot 4 |
| 4 | `attack` | 1–4000 | ms | E3 |
| 5 | `detune` | 0–50 | cents | E2 |
| 6 | `octave` | −2–2 | | E2 held |
| 7 | `mix` | 0–100 | % | E4 |
| 8 | `lfo` | 0–1 | off, on | E3 tap |
| 9 | `fx` | 0–2 | chorus, reverb, both | E4 tap |
| 10 | `layer` | 0–2 | synth, sampler, both | E1 tap |
| 11 | `shape` | 0–6 | saw, square, pulse, triangle, saw + sub, square + sub, fm | E1 (synth) |
| 12 | `sound` | 0–(sounds − 1) | the sample library's sounds | E1 (sampler) |
| 16 | `decay` | 1–10000 | ms | none |
| 17 | `sustain` | 0–100 | % | none |
| 18 | `fattack` | 0–4000 | ms; 0 = follows `attack` | none |
| 19 | `fdecay` | 1–10000 | ms | none |
| 20 | `fsustain` | 0–100 | % | none |
| 21 | `filter` | 0–2 | svf, ladder, ladder12 | none |
| 22 | `drive` | 0–100 | % | none |
| 23 | `hpf` | 0–2000 | Hz; 0 = off | none |
| 24 | `keytrack` | 0–100 | % | none |
| 25 | `mono` | 0–1 | off, on | none |
| 26 | `glide` | 0–2000 | ms | none |

Ids 13–15 are left free for panel parameters; 27 and up for anything new. The app
doesn't hard-code this table: it asks for it (`0x01`, below) and builds its
controls from the reply.

### Setting a parameter, whoever does it

`panel::Set(id, value, params)` does what the panel does when its own control moves:

* **The pots' parameters** (cutoff, resonance, env, release) are *held*, exactly as
  a preset holds them: the pot stops writing the parameter until it moves more than
  2 % from where it was at the time (`kTakeover`). Without this the pot would
  overwrite the app's value on the next scan, within a millisecond. The state
  reports which are held (`hold`), so the app can show that the knob and the sound
  disagree.
* **Attack:** E3 steps through 25 attack times, so after a set the encoder carries
  on from the nearest step, as it does after a preset (`attack_step`).
* **Sound** is clamped to the sounds on the card; **layer** stays `synth` while the
  card has none, as a tap does.
* It writes the same one-line description the panel does ("cutoff: 40 %"), for the
  serial port and the app.

Loading a preset puts every parameter back to the defaults plus the preset, as now.
What was set from the app is lost then: nothing in v1 saves to the card.

## Layer 1: standard MIDI

Omni, as now: the channel is ignored. From USB and the TRS jack alike.

| Message | Does |
|---|---|
| Note on/off, pitch bend, CC 64, CC 120, CC 123 | as now |
| **Program Change n** | loads preset n (0-based), if there is one |
| CC 74 | cutoff (GM2: brightness) |
| CC 71 | resonance (GM2: timbre/harmonic intensity) |
| CC 79 | filter envelope amount |
| CC 72 | release (GM2) |
| CC 73 | attack (GM2) |
| CC 75 | decay (GM2) |
| CC 94 | detune (GM: celeste/detune depth) |
| CC 91 | effects mix (GM: effects depth) |
| CC 102 | octave: 0–25 → −2, 26–51 → −1, 52–76 → 0, 77–102 → +1, 103–127 → +2 |
| CC 103 | shape, the 128 values split evenly over the 7 |
| CC 104 | layer, split evenly over the 3 |
| CC 105 | sound, split evenly over the sounds on the card |
| CC 106 | LFO: 0–63 off, 64–127 on |
| CC 107 | effects: split evenly over the 3 |

A CC value v maps to `min + v × (max − min) / 127` of the parameter's range
(release and attack exponentially, as the pot and E3 do). Same `panel::Set()`,
so a CC on a pot's parameter holds it too.

**Rodeo sends no CCs in v1.** Turning a knob doesn't send a CC (that comes later
if a DAW should record it). The app follows the knobs through the state stream.

## Layer 2: SysEx

### Framing

```
F0 7D 52 44 01 <cmd> <payload…> F7
```

* `7D`: the MIDI Association's ID for non-commercial and educational use, which is
  what this is until Rodeo has a manufacturer ID of its own. `52 44` ("RD") follow
  it so another 7D device's messages aren't taken for ours.
* `01`: the protocol version. A Rodeo ignores messages of a version it doesn't
  speak and answers `0x7F` (error) once.
* Commands from the app are `0x01`–`0x3F`; Rodeo's replies and events are
  `0x40`–`0x7F`. A reply is the command plus `0x40`.
* **Numbers** are 14-bit, two bytes, **LSB first** (`v & 0x7F`, `v >> 7`), with
  the parameter's minimum subtracted first, so every wire value is 0–16383
  (`octave` −2 goes as 0).
* **Text** payloads are JSON, ASCII only: the firmware replaces any byte ≥ 0x80 in a
  name with `?`, so the payload is valid SysEx as it stands. Rodeo is the only
  sender of JSON; what the app sends is all bytes.
* SysEx is read **from USB only** in v1. The TRS jack keeps to notes, CCs and
  Program Change.

### Commands

| cmd | From the app | Payload | Rodeo replies |
|---|---|---|---|
| `0x01` | Hello | none | `0x41`: who it is and the parameter table |
| `0x02` | State, once | none | `0x42`: the state |
| `0x03` | Stream the state | 1 byte: 1 on, 0 off | `0x42` while anything changes |
| `0x04` | Set parameters | (id, value LSB, value MSB) × n | nothing, then the change shows in the stream; `0x43` with the description |
| `0x05` | Preset list | none | `0x45` |
| `0x06` | Load a preset | index, 14-bit | `0x43`, then the stream |
| `0x07` | Sound list | none | `0x47` |

Rodeo also sends, unasked:

| cmd | Event | Payload |
|---|---|---|
| `0x43` | Said | `{"said":"cutoff: 40 %"}`: the line the serial port prints when anything changes, from the panel or the app. The app can show it as a toast. |
| `0x7F` | Error | `{"error":"unknown command 0x09"}` and the like |

#### `0x41` hello

Answers the app's first question ("is this a Rodeo, and what does it speak?") and
describes the parameters, so adding one means changing the firmware only:

```json
{"fw":"rodeo-demo","proto":1,"build":"555fb5f","voices":8,
 "params":[{"id":0,"key":"cutoff","min":0,"max":1000,"unit":"‰","pot":1},
           {"id":4,"key":"attack","min":1,"max":4000,"unit":"ms"},
           {"id":11,"key":"shape","min":0,"max":6,
            "names":["saw","square","pulse","triangle","saw + sub","square + sub","fm"]},
           …]}
```

`sound`'s `max` is the card's count at the time of asking; the sound list has the
names. (The `‰` here is written `‰`, keeping the payload ASCII.)

#### `0x42` state

The state the app mirrors, keyed by the parameter table:

```json
{"preset":2,"name":"bass","p":{"cutoff":412,"resonance":120,"env":600,
 "release":220,"attack":8,"detune":8,"octave":0,"mix":25,"lfo":0,"fx":2,
 "layer":0,"shape":0,"sound":0,"decay":200,…},
 "hold":5,"voices":3,"card":1,"loaded":18,"samples":18,"panel":1,"cpu":34}
```

* `p` holds every parameter in the table, in its units.
* `hold`: bit 0 cutoff … bit 3 release, set while a preset or the app holds the
  parameter and its pot hasn't taken it back.
* `card` is 0 none, 1 mounted, 2 disk mode, as on `#S`. `panel` is 1 when both banks
  are on the chain. The chain's per-satellite detail stays on `#S` only.
* About 500 bytes.

**Streaming** works like `J`: about 20 times a second while anything but `cpu`
changed in the last second, quiet after that. Unlike the serial port, MIDI has no
"someone is listening" signal: the Rodeo is a MIDI device as soon as it's plugged
into anything. So **a stream lasts 5 s from the last `0x03 01`**, and the app resends
it every 2 s while it's in the foreground. If the app is killed or the phone locks,
the stream stops by itself.

#### `0x45` preset list, `0x47` sound list

```json
{"presets":["bass","lead","organ","pad"],"current":0}
{"sounds":[{"name":"basic","kit":1,"ready":1},{"name":"pluck","kit":0,"ready":0}]}
```

Index order is the firmware's (name order on the card), which is what `0x06` and
Program Change take.

## The app's side

* **On connect:**
  1. Send `0x01`. If no `0x41` comes within 500 ms, the device is not a Rodeo
     speaking this protocol; say so.
  2. Then send `0x05` and `0x07`, and `0x03 01`.
* **Knob drags** send `0x04` at most every 20 ms per parameter, the latest value
  only.
* **When it goes to the background**, send CC 123 (all notes off): the sequencer
  can't play on, and a held key mustn't hang.

## Firmware notes

* **Never send part of a SysEx.** A half-sent message followed by the next one's
  `F0` corrupts both on the app's side. As built: everything Rodeo sends goes into a
  16 KB queue of USB-MIDI packets, a whole message or none of it (counted as
  dropped), and the main loop hands packets to TinyUSB as it takes them. The main
  loop never waits on a host that isn't reading. `CFG_TUD_MIDI_TX_BUFSIZE` went
  from 64 to 256 bytes.
* Reading SysEx needs the USB packets reassembled (CIN 0x4–0x7): `MidiTask()` reads
  packets and takes bytes 1–3 as one message today. Up to 256 bytes per incoming
  message is plenty; longer is dropped.
* `panel::Set()` is new, in `panel.cpp` beside `LoadPreset()`, which already does
  the hold, anchor and `attack_step` bookkeeping that `Set()` needs. The table and
  the unit conversions are `params.cpp`; the protocol is `remote.cpp`.
* `filter`'s preset value is `ladder12` (the parser); `presets.h`'s comment says
  `ladder 12`. Fix the comment.

## Testing without the app

`instruments/rodeo-demo/firmware/tools/midi_protocol_test.py` (needs
`pip3 install mido python-rtmidi`) runs every command against a Rodeo on the Mac,
and `--watch` prints the stream while the panel is played.

## Not in v1

* Saving presets, or anything else, to the card: the card is read-only to Rodeo.
* CCs out of Rodeo when the panel moves.
* SysEx on the TRS jack.
* The satellite chain's detail over MIDI.
