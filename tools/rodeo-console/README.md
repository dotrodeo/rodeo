# rodeo-console

A browser page for building a Rodeo's SD card: drop in audio of any format, lay
a kit out across the keys, edit presets as a form, and find out what the
firmware would quietly skip — before you walk back to the bench.

Phase 1 of [`planning/RODEO_CONSOLE_PLAN.md`](../../planning/RODEO_CONSOLE_PLAN.md).
A bench tool, not a product: Chrome or Edge, no build step, no dependencies.

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

## Tests

```sh
node --test tools/rodeo-console/
```

These check that the console agrees with **the firmware**, not with itself.
`js/card.js`, `js/preset.js` and `js/model.js` are a second copy of what
`instruments/rodeo-demo/firmware/src/{samples,presets}.cpp` do, and a copy
drifts. When a test fails after a firmware change, the firmware is right.

Two of the cases exist because an earlier version of this file did not catch a
deliberately planted bug: the sort test needs a non-ASCII name to tell
`strcasecmp` from `localeCompare`, and the pool test has to state 25,165,824
outright rather than derive it from the constant it is checking.

## Not here yet

Live mode — playing the Rodeo over Web MIDI and watching the satellite chain —
is phase 2, and the chain view wants a machine-readable state line from the
firmware first. The plan says what that would look like.
