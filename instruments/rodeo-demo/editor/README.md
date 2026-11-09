# rodeo-demo editor

rodeo-demo's page: the console ([`tools/rodeo-console`](../../../tools/rodeo-console))
with what only rodeo-demo knows, and its SD card. Drop in audio of any format,
lay a kit out across the keys, edit presets as a form and hear them on the
Rodeo as you go, and find out what the firmware would quietly skip.

A bench tool: Chrome or Edge.

## Running it

```sh
cd instruments/rodeo-demo/editor
npm install
npm run dev
```

Then open the `http://localhost:…` address Vite prints. It must be `localhost`:
the File System Access API needs a secure context, and `file://` is not one.

`?demo` loads a made-up card with every failure mode on it, so you can see what
the page does without a Rodeo attached. Nothing in demo mode can be written.

## What it adds to the console

Everything the console does, with rodeo-demo's profile ([`src/profile.ts`](src/profile.ts)):

- **The parameters grouped as the panel is** (pot bank 1 the filter, pot bank 2
  the amp envelope, the encoder banks, and the two only a preset reaches), each
  with the control that drives it, and a **preset picker** that loads on the
  Rodeo.
- **The encoders' jobs** in the chain view: E3 next to the high-pass, with what
  its tap last set (the LFO), and so on for all eight; E1 picks the sound in
  sampler layer. A value the Rodeo does not send reads *not reported* rather
  than a blank, which would look like zero.
- **The voice panel**, below.
- **The selected kit** marked on the keyboard, and the keys outside it
  silent in sampler layer.
- **The card's controls** in the bar, and serial buttons to hand the card to
  the computer and take it back.

**The voice panel is a prediction, not a reading.** `src/voices.ts` re-implements
`Allocate()`, so the page can say what Rodeo *should* have done with the notes
it just sent: the same key again takes its old voice back, then any finished
voice, then the oldest released one, then the oldest of all. Press nine notes
and it names the one that has to go.
It takes the release time and the layer from the Rodeo's state, and the count
is the Rodeo's own; the chips still predict *which* notes are on which voice,
since nothing reports that.

Two things the panel will tell you that are easy to get wrong by ear:

- **Panic does not silence the board.** CC 123 calls `Release`, which drops the
  gate and lets the amp envelope run. On a preset with a 4 s release, that is
  four more seconds of sound.
- **In sampler layer, a key with no sample allocates nothing at all** — it
  cannot even steal a voice.

## The card

The card is a job you go and do rather than something to watch, so it opens in
a dialog over the keyboard instead of competing with it for the screen.

1. Put the Rodeo in disk mode — hold encoders 1 and 4 for a second, or press
   `d` on its serial port. The card mounts as an ordinary drive.
2. **Open card…** and pick that drive. The page reads it the way the firmware
   will.
3. Add, arrange, edit. Everything is written straight onto the card.
4. Fix anything under **Before you eject**, then eject. The Rodeo re-reads the
   card on the way back.

Note that disk mode closes the serial port — the two cannot be open at once.
That is the firmware's behaviour, not a limitation of this page.

### What it knows

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

### Presets, heard

The preset editor uses the console's MIDI connection two ways:

- **As you edit**, each change is played on the Rodeo, and **Send to Rodeo**
  plays the whole preset, before anything is saved, as loading it would: what
  it leaves out goes back to its default, except a pot's parameter, which stays
  on its pot. Disk mode takes the Rodeo's
  MIDI away (the card and MIDI cannot both be the computer's), so to hear edits
  live, edit the card in a reader rather than in disk mode.
- **Take the Rodeo's sound** fills the preset with what the Rodeo was last
  playing. The page keeps the last state it heard after the port goes, so the
  way round disk mode is: dial the sound in on the panel or the sound panel,
  enter disk mode, open the card, take the sound.

## Tests

```sh
npm test
npm run typecheck
```

These check that the editor agrees with **the firmware**, not with itself.
`src/card.ts`, `src/preset.ts` and `src/model.ts` are a second copy of what
`firmware/src/{samples,presets}.cpp` do, and a copy
drifts. When a test fails after a firmware change, the firmware is right.

Two of the cases exist because an earlier version of this file did not catch a
deliberately planted bug: the sort test needs a non-ASCII name to tell
`strcasecmp` from `localeCompare`, and the pool test has to state 25,165,824
outright rather than derive it from the constant it is checking.
