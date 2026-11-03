---
title: "rodeo-console — plan"
tags: [rodeo, tooling, web, usb, midi, serial, samples]
status: phases 1 and 2 built; phase 3 open
created: 2026-11-02
updated: 2026-11-02
---

# rodeo-console — plan

A browser page for working with a Rodeo on the bench: build the SD card, play the
instrument, and watch the satellite chain. **A tool for Derek first**, Chrome-only,
rough where rough is cheaper. Built so it can become the thing backers use later,
which means the expensive parts — decoding audio, the preset model, the card's
rules — are written once and do not care how the files get across.

Not in scope: mapping an external MIDI controller onto Rodeo. Rodeo has no CC map
to write to yet.

## The two findings that shape everything

**1. Disk mode and the serial port cannot both be open.** Entering disk mode hands
the card to the computer and closes CDC — `main.cpp` says so on the way out ("the
serial port will close"), and you get it back by ejecting the drive. So the console
cannot be one live dashboard. It is **two modes with a visible switch between
them**, and the UI should state which one the hardware is in rather than appear to
lose its connection.

```
   ┌─ CARD MODE ──────────────┐        ┌─ LIVE MODE ──────────────┐
   │ SD card is the computer's│  eject │ serial + USB-MIDI open   │
   │ · build the library      │ <────> │ · play it from the page  │
   │ · edit presets           │   'd'  │ · watch the chain        │
   │ serial CLOSED            │        │ card is Rodeo's          │
   └──────────────────────────┘        └──────────────────────────┘
```

**2. The serial port speaks English, not data.** `PrintState()` emits formatted
prose for a human terminal. A monitor that screen-scrapes it will break the first
time a sentence is reworded. See [Firmware asks](#firmware-asks) — one extra
command fixes this, and it is small.

## Where it lives

`tools/rodeo-console/` — a static page, no build step, ES modules and plain CSS.
Served from localhost (the three APIs below all need a secure context, and
`file://` is not one); a `.claude/launch.json` entry starts it.

Self-contained on purpose: if it ever ships to backers it moves into
`web/public/console/` as-is. It stays out of `web/src/` so it never enters the
marketing site's sitemap, link check, a11y gate or claims lockfile — it is a tool,
not a page about the product.

## Browser APIs, and what each costs

| API | Used for | Support | If missing |
|---|---|---|---|
| File System Access | writing onto the mounted card | Chrome/Edge | fall back to a .zip download |
| Web MIDI | playing Rodeo from the page | Chrome/Edge; Safari 17.4+ | hide the keyboard |
| Web Serial | state, chain monitor, `d`/`s`/`B` | Chrome/Edge only | hide live mode |
| WebAudio `decodeAudioData` | decoding whatever is dropped in | everywhere | — |

No framework and no bundler. One page, a handful of modules, no dependency that
needs updating in a year.

## Card mode

The card is mounted as an ordinary volume. The user picks it once with
`showDirectoryPicker()`; everything after that reads and writes in place, with no
copying step and no zip.

### The card's rules, all enforceable before you walk to the bench

From `samples.h`, `samples.cpp` and `presets.h`:

```
/samples/<name>.wav     pitched — one sample across the keys, its pitch at MIDI 60
/kits/<name>/*.wav      a kit   — one sample per key, NAME ORDER from MIDI 36 (C2)
/presets/<name>.txt     key = value, '#' or ';' comments
```

| Limit | Value | What the firmware does today |
|---|---|---|
| Sample pool | 48 MB (24 Mi × int16) | silently skips the sample that overflows |
| Sounds | 64 | — |
| WAV files | 512 | — |
| Sound name | 31 chars | truncated |
| Accepted WAV | 8/16/24-bit PCM, 32-bit float, mono/stereo, any rate | anything else skipped |

Every one of those is a thing the page can say *before* the card goes back. The
pool meter is the valuable one: samples are kept as 16-bit, so the page can compute
the exact bytes Rodeo will use — `frames × channels × 2` — and show 48 MB filling
up. It can also estimate the load wait, since the card reads at about 1.24 MB/s
(bring-up test 07).

### What the page does that the Finder cannot

1. **Decode anything.** MP3, AIFF, FLAC, M4A, 96 kHz, 32-channel — `decodeAudioData`
   takes it, the page writes a WAV the firmware accepts. Today an unreadable file is
   skipped in silence at power-up.
2. **Build a kit against a keyboard.** Kit files map to keys *in name order* from
   C2. That is an unpleasant thing to get right by renaming files in Finder. Show
   the keyboard, drop a file on a key, let the page write `01_kick.wav`,
   `02_snare.wav` and so on so the order comes out right.
3. **Presets as a form.** Twelve keys with fixed vocabularies and ranges
   (`shape = saw | square | pulse | triangle | saw + sub | square + sub`,
   `cutoff = 0..100`). A form cannot typo them. The firmware skips bad lines and
   reports them on a serial port you will not be looking at.
4. **Resolve `sound =` by name.** A preset naming a sound the card does not have
   fails at play time with `MissingSound()`. The page knows both sides and can say
   so while it is still fixable.
5. **Trim, normalise, downmix** — optional, and the reason to convert rather than
   copy: 16-bit mono halves the pool and the load wait.

### Screen

```
┌─────────────────────────────────────────────────────────────┐
│  RODEO/   (card mode)                      [ pool ████░ 31/48 MB ]
├──────────────────┬──────────────────────────────────────────┤
│ SOUNDS      12   │  kits/909                                │
│  ▸ samples    8  │  ┌────────────────────────────────────┐  │
│  ▾ kits       4  │  │ C2  01_kick.wav      441k → 48k    │  │
│      606         │  │ C#2 02_snare.wav     stereo → mono │  │
│    » 909         │  │ D2  (drop a file)                  │  │
│ PRESETS      6   │  └────────────────────────────────────┘  │
│                  │  [+ add]  [rename]  [delete]             │
├──────────────────┴──────────────────────────────────────────┤
│ ⚠ preset "bass" wants sound "sub2" — not on this card        │
└─────────────────────────────────────────────────────────────┘
```

## Live mode

Serial open, card is Rodeo's.

- **A sequencer and an arpeggiator**, both on one transport, in `js/seq.js`.
  Not a copy of anything in the firmware — Rodeo has neither — so the only
  firmware fact they respect is that the ninth note steals. Added because a
  bench tool you have to play by hand is a bench tool you stop using.

- **An on-screen keyboard and a test pattern** over Web MIDI. Rodeo already
  enumerates as USB-MIDI (bring-up 08), so this needs no firmware at all: notes,
  velocity, a held chord, a scale sweep for checking voice stealing across the
  eight voices. **Built**, in `js/midi.js` and `js/live.js`.

  Worth recording that the whole MIDI surface is five messages — note on, note
  off, pitch bend, CC 64, CC 120/123 — and that nothing comes back, because there
  is no `tud_midi_write` in the firmware. The keyboard lists them so the absences
  are visible rather than discovered. If a CC map ever lands, that list is the
  one place it changes.
- **The chain, drawn.** Four pots and four encoders moving as they are turned, with
  good/bad scan counts. This is the piece that wants the firmware ask below.
- **Buttons for `s`, `d`, `n`, `B`** — state, disk mode, leave disk mode, reboot to
  DFU — so the serial commands are discoverable instead of remembered.

## Firmware asks

Yours, not mine, and both are small. Phase 1 needs neither.

**1. A machine-readable state line.** Written up in full, with every field's
source call and the one way to implement it that would break the instrument, in
[`RODEO_CONSOLE_STATE_LINE.md`](RODEO_CONSOLE_STATE_LINE.md). A `j` command (or
`s` gaining a second line) emitting one line of compact state, so the monitor
parses instead of scrapes:

```
#S {"preset":3,"layer":"both","shape":"saw","sound":2,"cut":41,"res":12,
    "env":60,"atk":8,"rel":220,"voices":3,"cpu":34,"sats":2,
    "good":24581,"bad":0,"loaded":18,"samples":18,"card":1}
```

Prefixed so a human terminal can ignore it. Emitting it on change, or ~10 Hz while
anything moves, makes the chain view live without polling.

That sketch is short one thing the chain view needs and no current output carries:
**where each control is**. "2 satellites, 24581 good scans" cannot draw a pot
moving. `SatChain` knows (`Pot`, `Pressed`, `SwitchPosition`); nothing prints it.
The spec adds a per-satellite array for it.

**2. Later, for phase 2: a file-transfer command.** Only worth it once the card
workflow is in use and the eject dance is the annoying part. Write the SD card while
running, with CRC and progress. Until then disk mode is genuinely fine.

## Phases

**Phase 1 — card mode. Done.** The converter, the kit keyboard, the preset form,
the validator, the pool meter. No firmware change, no serial, no MIDI. This is
most of the value and all of the reusable code.

**Phase 2 — live mode. Done**, after the firmware landed `j` and `J` on
2026-11-02 (`627d631`).

The chain view reads the `#S` line over Web Serial: satellites in ring order,
every slot live, breaks drawn where they are. The line also retired two of the
page's own guesses — it was asking which layer the Rodeo was in, and assuming a
release time for the voice model, and now reads both.

The keyboard before it needed no firmware either: notes, velocity, bend, CC 64,
CC 123, test patterns, a sequencer and an arp, and a voice panel that
re-implements `Allocate()` so the page can say which note the ninth should have
stolen.

Three things the keyboard turned up, all in the firmware's favour and all now
said out loud on the page rather than left to surprise someone:

- **CC 120/123 release the voices, they do not mute them** (`Ev::AllOff` calls
  `Release`, synth.cpp:218). Panic on a 4-second release is four more seconds of
  sound. A test asserted the opposite until the firmware corrected it.
- **A voice keeps its note number after it stops sounding**, so `Allocate`'s first
  branch hands a re-pressed key its old voice back rather than a free one — but
  only until some other note claims that idle slot, which the free scan does from
  slot 0 upward.
- **In sampler layer a key with no sample allocates nothing**, so it cannot steal
  a voice either. The page had to be told the layer; the `#S` line has since
  made that one moot.

**Phase 3 — if it goes to backers.** Cross-browser fallbacks, a demo mode with no
hardware attached, hosting under `web/public/console/`, and the honesty rules that
apply to everything else on that domain.

## Risks

- **Chrome-only** is fine for a bench tool and a decision to revisit at phase 3, not
  now.
- **Writing onto a mounted FAT volume while the OS caches it.** Eject properly;
  Rodeo re-reads on `Remount()`. Worth testing early with a deliberately ugly case
  (write, do not eject, pull the cable) so the failure is known rather than
  discovered.
- **The page is a second definition of the card's format.** `samples.h` and
  `presets.h` stay authoritative; the page's copy of the rules cites them, and a
  mismatch shows up as a file Rodeo skips. If this bites twice, generate the
  vocabulary from the headers.
