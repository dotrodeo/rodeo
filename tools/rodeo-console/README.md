# rodeo-console

A browser page for working with a Rodeo on the bench. **Card mode** builds the
SD card: drop in audio of any format, lay a kit out across the keys, edit
presets as a form, and find out what the firmware would quietly skip.
**Live mode** plays the board over USB-MIDI.

Phases 1 and 2 of
[`planning/RODEO_CONSOLE_PLAN.md`](../../planning/RODEO_CONSOLE_PLAN.md).
A bench tool, not a product: Chrome or Edge, no build step, no dependencies.

The two modes are a switch rather than one screen because the hardware makes
them exclusive: entering disk mode hands the card to the computer and closes
the serial port, and the USB-MIDI interface goes with it.

## Running it

```sh
npx serve tools/rodeo-console
```

Then open `http://localhost:3000`. It must be `localhost` — the File System
Access API needs a secure context, and `file://` is not one.

`?demo` loads a made-up card with every failure mode on it, so you can see what
the page does without a Rodeo attached. Nothing in demo mode can be written.

## Using it

1. Put the Rodeo in disk mode — hold encoders 1 and 4 for a second, or press
   `d` on its serial port. The card mounts as an ordinary drive.
2. **Open a card…** and pick that drive. The page reads it the way the firmware
   will.
3. Add, arrange, edit. Everything is written straight onto the card.
4. Fix anything under **Before you eject**, then eject. The Rodeo re-reads the
   card on the way back.

Note that disk mode closes the serial port — the two cannot be open at once.
That is the firmware's behaviour, not a limitation of this page.

## What it knows

The card's rules live in [`js/card.js`](js/card.js), and every one cites the
line of firmware that enforces it:

| | |
|---|---|
| `/samples/<name>.wav` | a pitched sound, its own pitch at C4 |
| `/kits/<name>/*.wav` | one sample per key from C2, **in name order** |
| `/presets/<name>.txt` | `key = value`, `#` or `;` comments |
| pool | 48 MiB of 16-bit samples |
| limits | 64 sounds, 512 WAVs, 31-character names |

The page converts anything the browser can decode — MP3, AIFF, FLAC, M4A, any
rate, any depth — into the plainest WAV the firmware accepts. That matters more
than it sounds: the firmware rejects `WAVE_FORMAT_EXTENSIBLE` outright, which is
what a lot of tools write for 24-bit and multichannel files, and a rejected file
is skipped in silence at power-up.

## Live mode

Rodeo already enumerates as USB-MIDI, so this needs nothing from the firmware.
Connect the port — it shows up as "Rodeo MIDI" — and the keyboard plays it.
Click or drag across the keys, or type on the `a w s e d f …` row; `z` and `x`
shift an octave, holding Shift is the sustain pedal.

The page sends **only** what `Midi()` acts on, and lists it on screen so the
absence is visible: notes on any channel, velocity, pitch bend over its fixed
±2 semitones, CC 64, and CC 120/123. There is no mod wheel and no CC for
cutoff; the panel's pots are the only way to shape the sound. Nothing comes
back, either — the firmware never writes MIDI, so the page can send and cannot
listen.

**The voice panel is a prediction, not a reading.** `js/midi.js` re-implements
`Allocate()`, so the page can say what Rodeo *should* have done with the notes
it just sent: the same key again takes its old voice back, then any finished
voice, then the oldest released one, then the oldest of all. Press nine notes
and it names the one that has to go. The only guess in it is how long a
released voice keeps sounding, which is the preset's release time and is an
input you can set.

Two things the panel will tell you that are easy to get wrong by ear:

- **Panic does not silence the board.** CC 123 calls `Release`, which drops the
  gate and lets the amp envelope run. On a preset with a 4 s release, that is
  four more seconds of sound.
- **In sampler layer, a key with no sample allocates nothing at all** — it
  cannot even steal a voice. Set the Layer control to match the Rodeo and the
  page applies that; it cannot read the layer itself.

The test patterns each answer one question: a kit walk from C2 for checking
which file landed on which key, a chromatic sweep, nine notes for the steal, a
bend sweep over the full travel, and a sustain test.

The page itself is kept bare — the explanations live here, not on screen, and
most controls carry a tooltip instead of a caption.

### The sequencer and the arp

Both run off one transport: play (or the space bar), a tempo, a step length
down to 1/32 and up to triplets, swing, and a gate length. `js/seq.js` holds the
arithmetic and is tested; unlike `card.js` and `midi.js` it is a copy of
nothing, because Rodeo has no sequencer and no arpeggiator and is not getting
one. This is the page playing the instrument.

**The sequencer** is 8 rows × 16 steps, chromatic from C2 so the rows are a
kit's first eight keys in order. Click a cell, or drag across to draw a line.
`−`/`+` move the whole pattern an octave.

**The arp** takes whatever is held on the keyboard: up, down, up-down, as
played, or random, over one to four octaves. Up-down plays its endpoints once,
so a triad gives four steps rather than six. **Latch** keeps it going after the
keys are let go, and the latched notes stay marked on the keyboard so it is
clear what it is chewing on; pressing a fresh key then starts a new chord. With
the arp running the keyboard feeds it instead of sounding directly.

Notes go out with Web MIDI timestamps from a lookahead scheduler rather than
straight off a `setTimeout`, which would jitter audibly. Swing shifts a step
without the grid drifting: sixteen steps later is still exactly a bar.

Stopping, switching to card mode, or hiding the tab lifts every note and sends
CC 123 — the page holds notes down on real hardware, and a dropped note-off
means the Rodeo drones until it is power-cycled.

## Tests

```sh
node --test tools/rodeo-console/test.mjs
```

These check that the console agrees with **the firmware**, not with itself.
`js/card.js`, `js/preset.js` and `js/model.js` are a second copy of what
`instruments/rodeo-demo/firmware/src/{samples,presets}.cpp` do, and a copy
drifts. When a test fails after a firmware change, the firmware is right.

Two of the cases exist because an earlier version of this file did not catch a
deliberately planted bug: the sort test needs a non-ASCII name to tell
`strcasecmp` from `localeCompare`, and the pool test has to state 25,165,824
outright rather than derive it from the constant it is checking.

### The chain

**Connect serial** opens the Rodeo's CDC port and sends `J`, and the firmware
streams its `#S` state line about twenty times a second while anything is
moving. The page draws the satellites in ring order with every slot live —
pots as meters, encoders as running detent totals, buttons and switches as
they are — plus the preset, the sound, the filter, the card and the scan
counts. A broken ring is drawn as a break, after the satellite it follows.

The buttons beside it are the serial commands: `s` for prose, `j` for one
line, `J` to toggle the stream, `d` for disk mode (which closes the port —
that is the firmware re-enumerating, not a fault), `n` to leave it, and `B`
to reboot into the bootloader.

`js/chain.js` parses the line and is a copy of two things, both authoritative:
the line's shape from the firmware's `state_line.cpp`, and the board kinds
from `rodeo-sdk/common/sat_frame.h`. **A line that does not parse is skipped,
never guessed at** — the firmware abandons a line if the computer stops
reading for 50 ms, so truncated lines are normal, and half a line of JSON must
not become half a screen of wrong numbers.

Two things change once the port is open. The **layer** and **release**
controls stop being inputs and become readouts, because the line carries both
and the page was previously asking you to tell it. And the voice count becomes
the Rodeo's own, rather than the model's prediction — the chips keep
predicting *which* notes are on which voice, since nothing reports that.

## Not here yet

Nothing in the plan. Phase 3 is the question of whether this goes to backers,
which is cross-browser fallbacks, hosting under `web/public/console/`, and the
honesty rules that apply to the rest of that domain.
