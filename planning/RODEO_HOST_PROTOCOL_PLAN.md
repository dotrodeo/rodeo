# The host protocol in rodeo-sdk — plan

Status: 2026-11-09. Steps 1 and 2 are built and run on a Rodeo: the SDK's `rodeo::Host` (`rodeo-sdk/common/host.h`, `examples/host`), and rodeo-demo on it with the console following. Steps 3–5 are to do.

## Why

rodeo-console grew up around rodeo-demo. Its "generic" parts (the chain view, the
live readings, the sound panel) only work because rodeo-demo's firmware happens to
speak a state line (`#S`, planning/RODEO_CONSOLE_STATE_LINE.md) and a SysEx protocol
(planning/RODEO_DEMO_MIDI_PROTOCOL.md). Both live in
`instruments/rodeo-demo/firmware/src/` (`state_line.cpp`, `remote.cpp`, `params.cpp`).
No other firmware has them.

Rodeo is a platform. "Plug it in and see your panel" should be true for any
firmware built on the SDK, not just the reference one. So:

1. The protocol moves into rodeo-sdk: any firmware gets the chain report, the
   parameter panel and flashing by declaring its parameters.
2. The console splits in two: a generic **rodeo-console** for any Rodeo, and a
   **rodeo-demo editor** for that firmware's SD card (samples, kits, presets).
3. One TypeScript client is shared by the console, the editor and the phone app.

## Cost, and why it's on by default

Measured in rodeo-demo (2026-11-09):

| | |
|---|---|
| Code | ~7 KB (`remote.o` 3.3 KB, `state_line.o` 1.4 KB, `params.o` 2.4 KB) of the 512 KB a BOOT_SRAM app runs in |
| RAM | an 8 KB send buffer in SDRAM, ~1 KB internal |
| CPU, idle | nothing: incoming SysEx is checked for the header and dropped |
| CPU, streaming | JSON built in the main loop (never the audio callback), ~20/s while something changes, silent 1 s after it stops; the stream ends 5 s after the host's last keep-alive |
| MIDI | SysEx only, under its own header. **No CCs, notes or program changes.** |

That is small enough to be **on by default, with a build-time off switch**
(`RODEO_HOST = 0` in the firmware's Makefile, compiling it out entirely). The
rule the SDK keeps: the protocol never claims a channel message. A firmware's
CC map stays the firmware's.

Opt-in pieces, off unless asked for:

- **A CC map from the parameter table** (`host.MapCc(74, "cutoff")`): the scaling
  rodeo-demo hand-writes in `remote::Cc`, derived from the table instead.
- **TRS MIDI as a transport.** At 31,250 baud a hello with 24 parameters takes most
  of a second and a stream would saturate the port, so USB MIDI is the only
  default transport.

## What the SDK provides

A `rodeo::Host` (name open) that the firmware gives three things:

- **A parameter table**: id, key, min, max, unit or names, the default, and the
  pot or encoder it's on, if any. The default is new: the console currently keeps
  a copy of rodeo-demo's defaults (`tools/rodeo-console/src/rodeo/audition.ts`
  `DEFAULTS`), which the table makes unnecessary.
- **get and set callbacks** for a parameter's value.
- **Optionally, its own commands** (rodeo-demo's presets, load and sounds), under
  a range of command numbers the SDK leaves to the firmware.

The SDK answers the rest itself:

| Command | What | Where today |
|---|---|---|
| hello | firmware name, build, protocol version, the parameter table | remote.cpp |
| state / stream | every parameter's value, held bits, CPU | remote.cpp |
| set | one parameter | remote.cpp |
| chain | satellites, kinds, firmware versions, raw readings, good/bad scans | `#S` over serial (state_line.cpp) |
| dfu | reboot into the bootloader for flashing | `B` over serial |

The chain report belongs in the SDK because the SDK owns the chain
(`common/sat_chain.*`). Moving it from the serial state line into the protocol
means the console needs only MIDI. Serial stays as the human-readable log.

Panel-held bits (a preset holds a pot until it's moved) are rodeo-demo's idea,
but any firmware with presets will have them, so the state carries an optional
bit per control.

## Open questions

- **The SysEx ID.** `0x7D` is the MIDI spec's non-commercial ID. It's fine for
  development; a shipped platform should probably register a manufacturer ID
  with the MMA. Changing it later is a protocol version bump, so decide before
  builders depend on it.
- **USB descriptors.** rodeo-demo builds its own TinyUSB configuration (audio,
  MIDI, CDC, mass storage). The SDK's `usb_port` needs a MIDI interface for the
  host to ride on, in every configuration a firmware might choose.
- **The name**: `rodeo::Host` (`common/host.h`), chosen in step 1.

## Steps

1. **SDK: the host module** (built 2026-11-09), from remote.cpp and
   state_line.cpp, with the parameter table and defaults: `common/host.h`,
   `host.cpp`, `host_usb.h`; `examples/host`; the guide `docs/guides/host.mdx`
   (the wire format) and the generated reference. 12 host tests. About 6 KB of
   code on ARM; `RODEO_HOST=0` compiles it out. Differences from rodeo-demo's
   protocol 1: hello carries each parameter's `def`, the firmware's `sdk`
   version and whether it has a `chain`; stream takes bits (1 state, 2 chain);
   the chain report (`08`, reply `48`) replaces the serial `#S` line's chain
   half; `09` reboots for flashing; `20`–`3E` are the firmware's own commands,
   so rodeo-demo's presets, load and sounds (`05`–`07` today) move there in
   step 2. Ran on Rev B with four satellites the same day: every command, a
   stream through full pot sweeps, and dfu, which needs `HostUsbFinish()`
   before the reboot or its reply is lost in TinyUSB's buffer.
2. **rodeo-demo moves onto it** (done 2026-11-09), keeping its own commands.
   Protocol version 2. This proves the module: if rodeo-demo can't be expressed
   with it, the API is wrong. It could, unchanged: `remote.cpp` is now the
   `HostApp` (get and set through `params.cpp` and `panel::Set()`, the hold
   bits, `voices` and the state's card fields), its CC map, and presets, load
   and sounds at `0x20`–`0x22`. Each parameter's default is computed at start-up
   from a default `synth::Params`. The console moved with it: protocol 2, the
   defaults from hello (its copy is gone), and rodeo-demo's lists asked for only
   when `fw` is `rodeo-demo`. `midi_protocol_test.py` passed on the Rodeo, dfu
   included. The serial `#S` line still carries the chain for the console's
   chain view until step 4 moves that to MIDI. The phone app's copy of the
   client (branch `rodeo-mobile-app`) still speaks protocol 1: step 5.
3. **The shared client** (done 2026-11-09): `rodeo-sdk/host-js`, the package
   `@dotrodeo/rodeo-host`. TypeScript source with no build step (Node, Vite and
   Metro all take it): the parser, the protocol, and a client that knows no
   firmware: a firmware's commands go out with `request()` and their replies
   are kept by command, and `whenReady()` runs after each hello. rodeo-demo's
   part is its own module, `@dotrodeo/rodeo-host/rodeo-demo` (lists, load, the
   state's extra fields). The console depends on it with `file:` and its copies
   are gone; 13 tests in the package, the console's 78 still pass. The phone
   app takes it in step 5 (Metro will need the folder in `watchFolders`).
4. **The split.** `tools/rodeo-console` keeps the chain view, readings, keyboard,
   sequencer, the parameter panel and flashing, for any Rodeo. The card editor
   (`card.ts`, `preset.ts`, `wav.ts`, `fs.ts`, `model.ts`, `demo.ts`,
   `audition.ts`, the card half of `ui.ts`) moves to
   `instruments/rodeo-demo/editor/`, next to the firmware it edits, and embeds
   the generic panel so a player still sees the chain and the sound.
5. **The phone app** (branch `rodeo-mobile-app`) moves onto the shared client
   when it's picked up again.

Each step is usable on its own: after step 2 the current console still works
against rodeo-demo, on protocol 2.
