# rodeo-console

A browser page for working with a Rodeo on the bench: play it over USB-MIDI,
watch the satellite chain, and build its SD card — drop in audio of any
format, lay a kit out across the keys, edit presets as a form, and find out
what the firmware would quietly skip.

Phases 1 and 2 of
[`planning/RODEO_CONSOLE_PLAN.md`](../../planning/RODEO_CONSOLE_PLAN.md).
A bench tool, not a product: Chrome or Edge. It is TypeScript built with Vite,
and everything it installs is a dev dependency — the page itself ships with
none.

**One screen, three connections.** The toolbar carries MIDI, serial and the
card, each with a status dot: grey not connected, yellow connected, cyan
carrying traffic, red open but wrong. The dot's tooltip says which — a colour
can report that something is off, not what.

The Rodeo still cannot have the card and the serial port open at once:
entering disk mode hands the card to the computer and closes CDC, and
USB-MIDI goes with it. That is a fact about the hardware, so the dots show it
going dark rather than the page pretending to be two places.

The card is a job you go and do rather than something to watch, so it opens in
a dialog over the keyboard instead of competing with it for the screen.

## Running it

```sh
cd tools/rodeo-console
npm install
npm run dev
```

Then open the `http://localhost:…` address Vite prints. It must be
`localhost` — the File System Access API needs a secure context, and `file://`
is not one.

`npm run build` type-checks and writes a static copy to `dist/`, and
`npm run preview` serves that.

`?demo` loads a made-up card with every failure mode on it, so you can see what
the page does without a Rodeo attached. Nothing in demo mode can be written.

## Using it

1. Put the Rodeo in disk mode — hold encoders 1 and 4 for a second, or press
   `d` on its serial port. The card mounts as an ordinary drive.
2. **Open card…** and pick that drive. The page reads it the way the firmware
   will.
3. Add, arrange, edit. Everything is written straight onto the card.
4. Fix anything under **Before you eject**, then eject. The Rodeo re-reads the
   card on the way back.

Note that disk mode closes the serial port — the two cannot be open at once.
That is the firmware's behaviour, not a limitation of this page.

## What it knows

The card's rules live in [`src/card.ts`](src/card.ts), and every one cites the
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

## Playing it

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

**The voice panel is a prediction, not a reading.** `src/midi.ts` re-implements
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

The page itself is kept bare — the explanations live here, not on screen, and
most controls carry a tooltip instead of a caption.

### The sequencer and the arp

Both run off one transport: play (or the space bar), a tempo, a step length
down to 1/32 and up to triplets, swing, and a gate length. `src/seq.ts` holds the
arithmetic and is tested; unlike `card.ts` and `midi.ts` it is a copy of
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

Stopping or hiding the tab lifts every note and sends CC 123 — the page holds notes down on real hardware, and a dropped note-off
means the Rodeo drones until it is power-cycled.

## Tests

```sh
cd tools/rodeo-console
npm test
```

`npm run typecheck` checks the types without building.

These check that the console agrees with **the firmware**, not with itself.
`src/card.ts`, `src/preset.ts` and `src/model.ts` are a second copy of what
`instruments/rodeo-demo/firmware/src/{samples,presets}.cpp` do, and a copy
drifts. When a test fails after a firmware change, the firmware is right.

Two of the cases exist because an earlier version of this file did not catch a
deliberately planted bug: the sort test needs a non-ASCII name to tell
`strcasecmp` from `localeCompare`, and the pool test has to state 25,165,824
outright rather than derive it from the constant it is checking.

### The chain

**Connect serial** opens the Rodeo's CDC port and sends `J`, and the firmware
streams its `#S` state line about twenty times a second while anything is
moving. System figures sit at the top; below them the satellites in ring
order, with a break drawn after the satellite it follows. Each card is one
grid, so its columns line up down the card.

**The panel's banks get a paired view**, which is the useful part: each
knob and encoder sits beside the number it is driving — pot 1 next to cutoff,
pot 8 next to release, E3 next to the high-pass — from the table in `panel.h`.
The panel uses the first two pot banks and the first two encoder banks on the
chain, bank 1 first; any others are drawn as plain slots.

That pairing exists to show one thing. Loading a preset sets the parameters,
and each pot keeps its hands off until it is moved about 2% from where it was
(`panel.cpp`'s takeover). So a knob can sit at 70% while cutoff is 41%: the
panel is not broken, the preset still has it. The row goes red and says
**preset holds this**, and nothing on the Rodeo itself can tell you that.

An encoder has no position to compare, so its row pairs the parameter with its
value and shows **the turn that just happened**. The line carries a detent
total counted from power-up, which on its own says nothing — "+14" is not a
state — so the totals are differenced between lines. It clears itself: the
firmware keeps streaming for a second after everything stops, and those last
lines carry a delta of zero.

Each encoder row also shows what its tap or hold last set: E1's layer, E2's
octave, E3's LFO, E4's chorus/reverb/both, E8's mono. Those were missing from `#S` until
the firmware added `det`, `oct`, `lfo`, `fx` and `mix`; a value the line does
not carry still reads *not reported* rather than a blank, which would look
exactly like a parameter set to zero.

The buttons say what they do rather than which letter they send (the letter is
in the tooltip): stream readings, read once, log full state, hand the card to
this computer, take the card back, reboot for flashing. Handing over the card
closes the port — that is the firmware re-enumerating, not a fault.

Anything the Rodeo says in prose — *no SD card to hand over*, the preset lines
it skipped — appears on the line under those buttons. Before, this page was
the only thing reading the port, so those messages went nowhere.

`src/chain.ts` parses the line and is a copy of two things, both authoritative:
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
