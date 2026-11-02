---
title: "rodeo-demo — Concept (brainstorm)"
tags: [rodeo, rodeo-demo, instrument, concept, rev-b, synth, sampler, usb-audio, usb-midi, sd]
status: brainstorm
created: 2026-11-02
updated: 2026-11-02
---

# rodeo-demo — Concept (brainstorm)

**What it is:** the first instrument written for **Rev B**, and the first to use the
satellite chain. It exists to make noise with the hardware that bring-up has proven, and
to show a builder what a few satellites and the motherboard's built-in I/O add up to.
Like rodeo-001 and rodeo-002 it is a reference build; the platform is the product.

Decisions so far are in the table below; anything marked **Open** still needs Derek's call.

## Fixed (Derek, 2026-11-02)

| | |
|---|---|
| Controls | **1 pot bank (4 pots) + 1 encoder bank (4 encoders, each with a press)**, on the satellite chain |
| Played by | **USB-MIDI** (and TRS MIDI comes free: test 03 passed) |
| Audio | Line outs, and **USB audio** to the computer |
| Sound | A relatively basic **synth**; **sample playback from the SD card** if it fits |

## Decided (Derek, 2026-11-02)

| | |
|---|---|
| Panel | **C: a fixed map now, pages later** once the OLED is fitted |
| Sampler | **Both**: each sound is either a pitched sample across the keys or a kit (one sample per note) |
| USB audio | **Demo to USB in 1/2, computer playback to OUT 3/4** |
| Code | **Fresh and small**, on plain DaisySP, readable end to end by a builder. rodeo-002 is not touched |
| Presets | **Read-only on the Rodeo**: written on the computer as a configuration file on the SD card, chosen on the Rodeo, never saved from it (revised 2026-11-02) |
| Voices | **8, shared** between synth and sampler |
| Sample rates | **Converted to 48 kHz**, by playback speed as the voice plays (speed = file rate / 48 kHz) |
| Fixed map | **Section A as written** |

## What the hardware has already proven

Every piece this build needs has passed on Rev B, separately:

| Piece | Bring-up | What it gives the demo |
|---|---|---|
| Satellite chain | 05, 2026-11-02 | Pot bank + encoder bank as a ring, 0 bad scans at 250 scans/s; each bank has one LED the Seed drives |
| USB audio + MIDI + serial | 10 and 08 | One composite device on the USB-C: UAC1 4×4 at 16-bit (24-bit on a direct port), USB-MIDI, and a serial port for 'B' (reflash) and status |
| Both codecs | 02 | Four outs, four ins, in one callback (`audio_4x4`) |
| SD card | 07, 09 | FatFS in 1-bit mode, 1.24 MB/s read on a 2 GB card; disk mode over USB works |
| TRS MIDI | 03 | In and out |

So the work is putting them together, not proving them. The one combination nobody has
run yet is **USB audio streaming while FatFS reads the SD card**. They are separate
peripherals (OTG_HS and SDMMC1) with separate DMA, so no conflict is expected; it gets
checked first.

## Proposed shape

```
USB-MIDI ─┐
TRS MIDI ─┴► voice alloc ─► 8 × [ synth voice | sample voice ] ─► stereo mix ─► FX ─┬─► OUT 1/2
                                                                                  └─► USB in 1/2 (record it)
computer playback (USB out 1/2) ────────────────────────────────────────────────────► OUT 3/4
pot bank, encoder bank ─► chain (250 Hz) ─► params; LEDs ◄─ state
```

* **Synth voice**: two oscillators (saw / pulse with width / triangle, PolyBLEP from
  DaisySP), detune, a sub-oscillator, a state-variable low-pass, an amp ADSR and a filter
  ADSR, one LFO. Eight voices, which rodeo-002 showed is comfortable on the H750.
* **Sample voice**: plays a WAV from SDRAM, pitched by the note (one sample across the
  keys) or one sample per note (a kit). Same amp envelope and filter as the synth, so the
  panel means the same thing for both.
* **FX**: a chorus and a reverb on the mix. Small and well-known, nothing rodeo-002 doesn't
  already do better.
* **USB audio**: Rodeo shows as an audio interface. The demo's output is recorded on USB
  inputs 1/2, and whatever the computer plays comes out of 3/4. One cable for MIDI in and
  audio out.

## SD card: how samples get in

The SD card is too slow to stream many voices from (1.24 MB/s is about six stereo voices
at 48 kHz, before any seeking), but the Seed has **64 MB of SDRAM**. So samples are
**loaded into SDRAM** from the card, and voices play from there:

* At boot, or on a kit change, load a folder of WAVs (16- or 24-bit, mono or stereo, 48 kHz;
  other rates converted to 48 kHz on load). 10 MB loads in about 8 s on the
  card tested; a modern SDHC card should be faster.
* **Getting files onto the card**: either take the card out, or **disk mode** (built
  2026-11-02, `src/disk.h`), which reuses `usb_storage`'s. Hold E1 and E4 together for a
  second (or `d` on the serial port): the Rodeo unmounts the card and re-enumerates as
  "Rodeo Demo (disk mode)", serial + USB storage, and the computer mounts the card.
  Eject the drive to hand it back; the Rodeo re-enumerates as itself and reloads the
  library. If the computer goes away instead, it comes back by itself after 3 s. USB
  audio and MIDI are gone while in disk mode; the synth keeps playing from the TRS jack
  with the samples it had. The pot bank's LED blinks slowly throughout.
  Tested on macOS: in about 3 s, out about 1 s after the eject; a 24-bit 44.1 kHz WAV
  copied from the Mac loaded, and USB audio and MIDI worked again afterwards.
* Proposed layout: `/samples/<name>.wav` for pitched samples, `/kits/<name>/` for a kit
  (files sorted by name, mapped from MIDI note 36 up).

"Squeezing it in" is about this decision, not memory: the app runs from 512 KB of SRAM
(BOOT_SRAM), and FatFS plus a WAV loader is a few KB.

## The panel: 4 pots, 4 encoders, 2 LEDs, no screen

There is **no screen** yet (test 06 waits on the OLED from rodeo-002), so the panel has to
make sense without one. **Decided: C.**

**A. Fixed map, no pages.** Every control always does one thing. Pots are the four things
you play: cutoff, resonance, filter envelope amount, release. Encoders choose things:
E1 sound (oscillator shape, or which sample), E2 detune / octave, E3 attack, E4 FX mix.
Presses: E1 synth / sampler / both layered; E2 reset to default; E3 LFO on/off; E4 tap
to cycle chorus / reverb / both. Easiest to learn with no screen; the fewest parameters.

**B. Pages on E1.** E1 picks a page (Oscillators, Filter, Envelopes, LFO, Sampler, FX),
and the pots and E2–E4 edit that page's parameters. Far more reachable; but with no
screen the page shows only as an LED blink count, and pots need pickup (a pot does
nothing until it passes the stored value).

**C. A then B.** Ship A; add pages behind a press-and-hold once the OLED is fitted.

## Open questions

1. Presets are read-only on the Rodeo (decided): a configuration file on the SD card,
   written on the computer and copied over in disk mode. Still open: the file's format,
   how a preset is chosen with the fixed map full, and what the pots do after one loads
   (pickup, or presets leave the pots' parameters alone).
