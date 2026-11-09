---
title: "rodeo-demo over MIDI — the protocol the phone app needs"
tags: [rodeo, rodeo-demo, firmware, midi, sysex, mobile, app]
status: built in rodeo-demo (params.cpp, remote.cpp) and tested on a Rodeo, 2026-11-07; protocol 2 (the SDK's host protocol) 2026-11-09
created: 2026-11-07
updated: 2026-11-09
---

# rodeo-demo over MIDI

> **Protocol 2, since 2026-11-09.** The SysEx layer is now the SDK's host
> protocol (`rodeo-sdk/common/host.h`; the wire format is
> `rodeo-sdk/docs/guides/host.mdx`), which any firmware on the SDK speaks.
> rodeo-demo adds its own commands and state fields on top. Below, Layer 2 says
> what's rodeo-demo's; the rest is in the guide. Protocol 1, which this page
> first described, was rodeo-demo's alone.

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

Pots 1–4 are the first pot bank, 5–8 the second; E1–E4 the first encoder bank,
E5–E8 the second (`panel.h`; two of each since 2026-11-08). Release moved from
pot 4 to pot 8 then, and attack from E3 to pot 5.

| id | key | range | unit | on the panel |
|---|---|---|---|---|
| 0 | `cutoff` | 0–1000 | ‰ of the pot's travel | pot 1 |
| 1 | `resonance` | 0–1000 | ‰ | pot 2 |
| 2 | `env` | 0–1000 | ‰ | pot 3 |
| 3 | `release` | 5–4000 | ms | pot 8 |
| 4 | `attack` | 1–4000 | ms | pot 5 |
| 5 | `detune` | 0–50 | cents | E2 |
| 6 | `octave` | −2–2 | | E2 held |
| 7 | `mix` | 0–100 | % | E4 |
| 8 | `lfo` | 0–1 | off, on | E3 tap |
| 9 | `fx` | 0–2 | chorus, reverb, both | E4 tap |
| 10 | `layer` | 0–2 | synth, sampler, both | E1 tap |
| 11 | `shape` | 0–6 | saw, square, pulse, triangle, saw + sub, square + sub, fm | E1 (synth) |
| 12 | `sound` | 0–(sounds − 1) | the sample library's sounds | E1 (sampler) |
| 16 | `decay` | 1–10000 | ms | pot 6 |
| 17 | `sustain` | 0–100 | % | pot 7 |
| 18 | `fattack` | 0–4000 | ms; 0 = follows `attack` | E5 (tap: 0) |
| 19 | `fdecay` | 1–10000 | ms | E6 |
| 20 | `fsustain` | 0–100 | % | E7 |
| 21 | `filter` | 0–2 | svf, ladder, ladder12 | none |
| 22 | `drive` | 0–100 | % | pot 4 |
| 23 | `hpf` | 0–2000 | Hz; 0 = off | E3 |
| 24 | `keytrack` | 0–100 | % | none |
| 25 | `mono` | 0–1 | off, on | E8 tap |
| 26 | `glide` | 0–2000 | ms | E8 |

Ids 13–15 are left free for panel parameters; 27 and up for anything new. The app
doesn't hard-code this table: it asks for it (`0x01`, below) and builds its
controls from the reply.

### Setting a parameter, whoever does it

`panel::Set(id, value, params)` does what the panel does when its own control moves:

* **The pots' parameters** (cutoff, resonance, env, drive, attack, decay, sustain,
  release) are *held*, exactly as
  a preset holds them: the pot stops writing the parameter until it moves more than
  2 % from where it was at the time (`kTakeover`). Without this the pot would
  overwrite the app's value on the next scan, within a millisecond. The state
  reports which are held (`hold`), so the app can show that the knob and the sound
  disagree.
* **The encoders' parameters** (high-pass, the filter envelope, glide) step in
  ratio, and each turn starts from the step nearest the value as it stands, so
  after a set the encoder carries on from there, as it does after a preset.
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
(release, attack and decay exponentially, as pots 8, 5 and 6 do). Same `panel::Set()`,
so a CC on a pot's parameter holds it too.

**Rodeo sends no CCs in v1.** Turning a knob doesn't send a CC (that comes later
if a DAW should record it). The app follows the knobs through the state stream.

## Layer 2: SysEx

### Framing

```
F0 7D 52 44 02 <cmd> <payload…> F7
```

The SDK's host protocol: the framing, the version, 14-bit numbers above the
minimum, JSON replies (a reply is the command plus `0x40`), whole messages only,
and streaming are all in `rodeo-sdk/docs/guides/host.mdx`. SysEx is read **from
USB only**; the TRS jack keeps to notes, CCs and Program Change.

### Commands

The SDK's, which every firmware on it answers:

| cmd | | Reply |
|---|---|---|
| `0x01` | Hello: the parameter table, each with its default | `0x41` |
| `0x02` | State, once | `0x42` |
| `0x03` | Stream: 1 state, 2 chain, 3 both, 0 stop | `0x42`, `0x48` as they change |
| `0x04` | Set parameters: (id, value LSB, value MSB) × n | none; the change shows in the stream, with `0x43` |
| `0x08` | The satellite chain | `0x48` |
| `0x09` | Reboot into the bootloader | `0x49`, then DFU |

rodeo-demo's own, in the range the SDK leaves to a firmware:

| cmd | | Payload | Reply |
|---|---|---|---|
| `0x20` | Preset list | none | `0x60` |
| `0x21` | Load a preset | index, 14-bit | `0x43`, then the stream |
| `0x22` | Sound list | none | `0x62` |

Unasked, `0x43` (said: the serial port's line for each change, from the panel or
the computer) and `0x7F` (error).

#### Hello

The SDK's (`fw`, `build`, `proto`, `sdk`, `chain`, `params`), plus `voices`: 8.
Every parameter carries its `def`, filled in at start-up from a default
`synth::Params`, so it is what `synth.h` says and what a preset that leaves the
parameter out goes back to. `sound`'s `max` is the card's count at the time of
asking; the sound list has the names.

#### State

The SDK's (`p`, `hold`, `cpu`), plus rodeo-demo's:

```json
{"p":{"cutoff":412,…},"hold":5,"preset":2,"name":"bass","voices":3,"card":1,
 "sounds":4,"loaded":18,"samples":18,"panel":1,"cpu":34}
```

* `hold`: one bit a pot, bit 0 pot 1 (cutoff) … bit 7 pot 8 (release), set while a
  preset or the computer holds the parameter and its pot hasn't taken it back.
* `card` is 0 none, 1 mounted, 2 disk mode, as on `#S`. `panel` is 1 when the first
  pot bank and the first encoder bank are on the chain.

#### `0x60` preset list, `0x62` sound list

```json
{"presets":["bass","lead","organ","pad"],"current":0}
{"sounds":[{"name":"basic","kit":1,"ready":1},{"name":"pluck","kit":0,"ready":0}]}
```

Index order is the firmware's (name order on the card), which is what `0x21` and
Program Change take.

## The computer's side

* **On connect:**
  1. Send `0x01`. If no `0x41` comes within 500 ms, the device is not a Rodeo
     speaking this protocol; say so.
  2. If `fw` is `rodeo-demo`, send `0x20` and `0x22`. Then `0x03 01`, again every
     2 s while it's wanted.
* **Knob drags** send `0x04` at most every 20 ms per parameter, the latest value
  only.
* **When it goes to the background**, send CC 123 (all notes off): the sequencer
  can't play on, and a held key mustn't hang.

## Firmware notes

* The SDK's `rodeo::Host` does the protocol: reassembling incoming SysEx, the queue
  of whole outgoing messages (24 KB in SDRAM, a message or none of it), the stream,
  the chain report. `remote.cpp` is rodeo-demo's side: the `HostApp` (get and set
  through `params.cpp` and `panel::Set()`, the hold bits, the extra fields), its
  three commands, and the CC map. `CFG_TUD_MIDI_TX_BUFSIZE` is 256.
* `panel::Set()` is the one way in for a set, so a knob, a CC and the computer
  behave the same way. The table and the unit conversions are `params.cpp`.
* The computer's dfu (`0x09`) does what `B` on the serial port does: stops the
  audio and reboots into the bootloader for `make program-dfu`.

## Testing without the app

`instruments/rodeo-demo/firmware/tools/midi_protocol_test.py` (needs
`pip3 install mido python-rtmidi`) runs every command against a Rodeo on the Mac,
and `--watch` prints the stream while the panel is played.

## Not yet

* Saving presets, or anything else, to the card: the card is read-only to Rodeo.
* CCs out of Rodeo when the panel moves.
* SysEx on the TRS jack.
