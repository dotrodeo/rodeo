---
title: "The `#S` state line — what rodeo-console needs from the firmware"
tags: [rodeo, firmware, serial, tooling, console]
status: implemented in rodeo-demo (j and J), 2026-11-02
created: 2026-11-02
updated: 2026-11-03
---

# The `#S` state line

One serial command, so `tools/rodeo-console`'s chain view can read the Rodeo's
state instead of screen-scraping it. Everything else in the console is built and
needs nothing: card mode works, and the Web MIDI keyboard works because Rodeo
already enumerates as USB-MIDI.

**Do not change `PrintState()`.** Its prose is for a human with a terminal and it
is good at that. This is a second, additional output.

## Why not parse what is already there

`PrintState()` emits sentences — "chain: 2 satellites, pot bank and encoder bank
found; 24581 good scans, 0 bad". A monitor that parses that breaks the first time
a sentence is reworded, and the breakage is silent: the console would show stale
numbers rather than an error. The console deliberately refuses to do it.

The other half of the problem is that the prose does not carry what the chain view
is *for*. There is no way to learn, from any current output, where pot 2 is or
which satellite it is on. `SatChain` knows (`Pot`, `EncoderSteps`, `Pressed`,
`SwitchPosition`, `Satellite`); nothing prints it.

## The command

Add `j` to `SerialTask()`'s switch, beside `s`. One line, terminated `\r\n`:

```
#S {"preset":3,"name":"bass","layer":"both","shape":"saw","sound":2,"nsounds":18,
"cut":41,"res":12,"env":60,"atk":8,"rel":220,"voices":3,"cpu":34,"peak":51,
"card":1,"loaded":18,"samples":18,"skipped":0,"kb":31204,"loadms":25140,
"chain":{"state":"scan","sats":2,"broken":-1,"good":24581,"bad":0,"panel":1,
"s":[{"kind":5,"fw":"1.0","in":[412,900,77,512]},
     {"kind":6,"fw":"1.0","in":[3,-1,0,0],"btn":[0,1,0,0]}]}}
```

Prefixed `#S ` so a human terminal can ignore the line, and so the console can
find it in a buffer that also holds prose. One line, no pretty-printing: the
console splits on newlines and takes the ones starting `#S `.

### Fields, and where each comes from

| Key | Source | Note |
|---|---|---|
| `preset` | `panel::Preset()` | **0-based, or -1** — not the `+ 1` the prose prints |
| `name` | `presets::Name(panel::Preset())` | `""` when none |
| `layer` | `params.layer` | `"synth"`, `"sampler"`, `"both"` |
| `shape` | `synth::ShapeName(params.shape)` | the string, not the enum |
| `sound` | `params.sound` | **0-based** |
| `nsounds` | `samples::GetStats().sounds` | |
| `cut` `res` `env` | `params.cutoff` etc | 0..100, rounded, as the prose does |
| `atk` `rel` | `params.attack * 1000` | ms |
| `drv` `sus` `fsus` | `params.drive` etc | 0..100 (added 2026-11-08, with the second banks) |
| `dec` `fatk` `fdec` `glide` | `params.decay * 1000` etc | ms; `fatk` 0 when it follows attack |
| `hpf` | `params.hpf` | Hz; 0 off |
| `mono` | `params.mono` | 0 or 1 |
| `voices` | `synth::Active()` | 0..8 |
| `cpu` `peak` | `cpu.GetAvgCpuLoad()`, `GetMaxCpuLoad()` | percent |
| `card` | `s.card`, `disk::Active()` | `1` mounted, `0` none, `2` the computer's |
| `loaded` `samples` `skipped` | `samples::GetStats()` | |
| `kb` `loadms` | `s.bytes_used / 1024`, `s.load_ms` | |

Please **do not** call `cpu.Reset()` from this command the way `PrintState()` does
— a console polling at 10 Hz would reset the peak ten times a second and the
figure would mean nothing. Leave the reset on `s`.

### The chain object, which is the point

| Key | Source |
|---|---|
| `state` | `chain.GetState()` as a word: `powerwait`, `discover`, `scan`, `findbreak`, `partial`, `version` |
| `sats` | `chain.Satellites()` |
| `broken` | `chain.BrokenAfter()` |
| `good` `bad` | `chain.GoodScans()`, `chain.BadScans()` |
| `panel` | `panel::Found()` as 0/1 |
| `s[]` | one object per satellite, in chain order |

Per satellite:

| Key | Source |
|---|---|
| `kind` | `chain.Satellite(i).kind` — the number, the console has the names |
| `fw` | `"<fw_major>.<fw_minor>"` |
| `in[]` | one entry per slot, `sat_kind_slots(kind)` of them |
| `btn[]` | optional, same length: 1 where `chain.Pressed(i, s)` |
| `fault[]` | optional, present only when some slot faults: `[code, detail]` or `null` |

`in[]` is per slot kind, as an integer so the line stays short:

- **POT** — `Pot(i, s) * 1000`, rounded. 0..1000.
- **ENCODER** — an accumulated detent count, not `EncoderSteps()`. **This matters:**
  `EncoderSteps()` is destructive — it returns the steps since the last call and
  zeroes them — so if the state line calls it, `panel::Update()` stops seeing
  encoder movement and the instrument's own controls break. Keep a separate
  running total for this, or have the line report `Input(i, s)` raw.
- **BUTTON** — 0 or 1, same as `btn[]`; send either, not both.
- **SWITCH** — `SwitchPosition(i, s)`.

That encoder note is the one place this ask can break the instrument if it is
implemented the obvious way.

## When to emit

`j` on demand is enough to be useful, and is the whole ask.

If it is easy: `J` toggling a ~20 Hz stream while anything is moving, going quiet
when nothing has changed for a second. That turns the chain view live without the
console polling, and 20 Hz of ~400 bytes is 8 KB/s, which USB CDC will not notice.
If it is not easy, say so and the console will poll on a timer.

## Constraints

- `PrintState()`'s `buf[800]` is not enough for a full chain. Size for the worst
  case: `SAT_MAX_SLOTS` is 64, so a 64-slot chain is roughly 1.2 KB. Either size
  the buffer for it or emit the line in pieces with one flush at the end.
- The serial port closes in disk mode, so the chain view is live mode only. That
  is already how the console is built; nothing needed here.
- No floats in the JSON. Everything above is an integer or a short string, which
  keeps `snprintf` simple and the line short.

## As built (rodeo-demo, 2026-11-02)

`j` and `J` are both in, in `instruments/rodeo-demo/firmware/src/state_line.cpp`
(the chain object) and `main.cpp` (the rest). Tested on the bring-up Rodeo with
the pot bank and encoder bank: every encoder turn and press, pot move, shape and
layer change showed in the line while the panel kept working. Where it differs
from the ask above:

- **Key order.** `good` and `bad` come last in `chain`, and `cpu` and `peak` last
  in the line. They change constantly, so the stream leaves them out when it
  checks whether anything has moved. JSON consumers should not care.
- **Pots have 3/1000 of hysteresis.** A pot resting on a rounding boundary
  flickered 493/494 and the stream never went quiet. A pot's value in the line
  moves once it is 3 away from the last one sent; 0 and 1000 always come through.
- **Encoders** are running detent totals kept from the raw counters, starting at
  0 at power-up and whenever the chain's layout changes. `EncoderSteps()` is not
  touched.
- **Buttons** go in `in[]` only; `btn[]` is sent for encoder satellites.
- **`J`** sends every 50 ms while anything has changed in the last second, and
  stops when the serial port closes. It starts with one second of lines so the
  console gets the state at once.
- **Long lines** go out in pieces (the CDC buffer is 512 bytes; this line is
  about 440 with two banks). The buffer is 4 KB. If the computer stops reading
  for 50 ms the rest of the line is dropped, so a line that fails to parse
  should simply be skipped.
- The preset name has `"` and `\` stripped rather than escaped.

## What the console does with it

`tools/rodeo-console/js/live.js` gains a chain panel over Web Serial: the
satellites drawn in ring order with their kind, four pots and four encoders moving
as they are turned, the good/bad scan counts, and the break marked when there is
one. The parser is a dozen lines — `JSON.parse` on anything after `#S ` — and the
same line drives the Layer control that live mode currently has to ask the user
for, and the preset and sound names the voice panel has to guess at today.

Buttons for `s`, `d`, `n` and `B` come with the same Web Serial plumbing, so the
serial commands become discoverable rather than remembered.

## Addendum: five fields the line still does not carry (2026-11-03)

**Done, 2026-11-03:** `det`, `oct`, `lfo`, `fx` and `mix` are in the line, after
`atk` and before the hash, as below. Checked on the board against
"01 warm pad" (12, 0, 1, 1, 40); the line is 458 bytes with two banks.

Building the console's paired panel view — each knob beside the number it
drives — left three rows reading *not in the state line*, because the panel
has controls whose values `#S` does not report:

| Encoder | What it drives | In the line? |
|---|---|---|
| E2 turn | detune | no |
| E2 held | octave | no |
| E3 tap | LFO on/off | no |
| E4 turn | effects mix | no |
| E4 tap | chorus / reverb / both | no |

The console shows the parameter's name and says the value is not reported,
rather than printing a blank that reads as zero. That is honest but it is the
only gap left in the panel view.

**Every one of these is already in `synth::Params`** (synth.h:30-48), which
`StateLine()` has in scope — so this is five more conversions in the existing
`snprintf`, not new plumbing:

```c
"det":%d    (int)lroundf(params.detune * 100.f)   cents, 0-50 (presets.cpp:126)
"oct":%d    params.octave                          -2..2
"lfo":%d    params.lfo ? 1 : 0
"fx":%d     (int)params.fx                         0 chorus, 1 reverb, 2 both
"mix":%d    (int)lroundf(params.fx_mix * 100.f)    percent (presets.cpp:140)
```

`fx` can go as the enum's own index: the console already mirrors that order in
`js/preset.js`'s `FX`, which came from `presets.cpp`. No `FxName()` needed,
though one would be no worse.

Notes:

- **Put them in the first `snprintf`**, before the FNV hash is taken, so a
  change to any of them wakes the `J` stream. That happens by itself if they
  go in the existing format string; it would not if they were appended beside
  `cpu` and `peak`.
- **No buffer change.** About 40 bytes on a line that is ~440 with two banks,
  against a 4 KB buffer.
- **The console needs nothing new to use them** beyond pointing four rows at
  the new keys — the parse and the layout already handle a key being present.

Worth doing when something else takes you into that function; not worth a trip
on its own. The panel view is useful as it stands.

### Done (2026-11-03)

In as `det`, `oct`, `lfo`, `fx`, `mix`, in the first `snprintf` so the stream
wakes on them. The console reads all five; no panel row says *not reported* any
more. `fx` goes as the enum index and `js/chain.js`'s `FX_NAMES` mirrors it.
