---
title: "Rodeo mobile — plan"
tags: [rodeo, rodeo-demo, mobile, ios, android, usb-midi, app]
status: planning
created: 2026-11-07
updated: 2026-11-07
---

# Rodeo mobile — plan

A phone or tablet app that is **the screen for rodeo-demo**: plug it into the
Rodeo's USB-C and it shows the panel live, plays the synth from an on-screen
keyboard, and runs a step sequencer and arpeggiator. It's there to show what
Rodeo can do with a phone attached. iPhone/iPad first, Android later.

## Decisions (Derek, 2026-11-07)

* **React Native with Expo, iOS first.** Android later, from the same code.
* **It lives in `instruments/rodeo-demo/app/`.**
* **Not for the App Store.** Installed on Derek's own devices with Expo's
  development tools: a development build, `npx expo run:ios --device`, signed with
  his Apple account. On a free account the install expires after 7 days; rebuild to
  renew it.
* **A native feel matters more than sharing code.** The UI starts from scratch:
  nothing carries over from rodeo-console's UI.
* **The sequencer clock runs on the phone.** Rodeo stays a sound module, as
  `tools/rodeo-console/src/seq.ts` already says: the phone sends notes. If the phone
  locks or the app goes to the background, the sequence stops; acceptable for a demo.

## The finding that shapes everything: MIDI is the only pipe

rodeo-demo is one composite USB device: UAC1 audio, a CDC serial port, USB-MIDI.
**An iOS app can't open a CDC serial port** (no access without MFi), and there's no
Web MIDI in Safari. Both iOS (CoreMIDI) and Android (`android.media.midi`) take a
class-compliant USB-MIDI device natively. So everything the app needs travels as
MIDI, which the firmware can't do yet: today it understands notes, pitch bend,
sustain (CC 64) and all-notes-off, and the state lives only on the serial `#S`
line. Phase 1 adds that, and it serves rodeo-console and outside controllers too.

## Approach: React Native + Expo, with a native MIDI module

* **Expo**, using a development build (`expo-dev-client`), not Expo Go, which can't
  load custom native code. Build and install locally with `npx expo run:ios`
  (Xcode), or with EAS later.
* **`rodeo-midi`, a local Expo module** (Expo Modules API, Swift) over CoreMIDI:
  * list sources and destinations, and find the Rodeo by name;
  * send with a timestamp (`MIDIPacketList` and host time), so the sequencer
    schedules ahead and CoreMIDI delivers on time, whatever the JS thread is doing;
  * receive (notes, CC, SysEx) as JS events;
  * report plug and unplug.
  An Android side (`android.media.midi`, Kotlin) goes in the same module later.
* **UI:**
  * React Native's own components;
  * `react-native-gesture-handler` and `react-native-reanimated` for the
    multi-touch keyboard and the knob drags, so they run on the UI thread;
  * `react-native-skia` if the knobs or visuals need custom drawing.
* **Logic:** `seq.ts`'s timing arithmetic (step lengths, swing, arpeggiator order)
  is plain JS with tests, and can be copied over or rewritten in TypeScript as
  needed. Nothing else from the console is assumed.
* **Testing without a device:** unverified whether the iOS Simulator sees a USB
  MIDI device plugged into the Mac. If it doesn't, the app will be tested on a real
  iPhone/iPad, or through a Network MIDI session from the Mac to the Simulator.

## What the app does

1. **Connect:** finds the Rodeo's MIDI ports, shows the preset name; a clear
   "plug in your Rodeo" state when it isn't there.
2. **Panel:** the 8 knobs with their parameter, value and the preset, live in both
   directions: turn a knob on Rodeo and the screen follows, drag on the screen and
   the synth follows.
3. **Keyboard:**
   * multi-touch, with velocity from where the key is struck;
   * octave shift, and an optional scale/key lock so it's hard to play a wrong note;
   * pitch-bend strip and sustain.
4. **Sequencer:** 16 steps (note, velocity, gate per step), tempo, swing, the
   arpeggiator; a drum-grid view for the sample kit (from note 36).
5. **Presets:** browse and load.
6. **Later:**
   * record Rodeo's audio straight into the phone over USB audio, and share the clip;
   * save presets from the phone.

## Risks to settle first (phase 0)

* **Power.** The USB-C board is a sink (CC 5.1 kΩ to ground), so the phone powers
  VBUS, and Rodeo's charger may try to charge its battery from the phone. The
  configuration asks for 100 mA. An iPhone may shut the port off, or brown out, if
  Rodeo draws much more.
* **Enumeration.** Does iOS take the composite device and ignore the CDC
  interface cleanly?
* **Timing** of USB-MIDI from a phone (expected 1–3 ms).

## Phases

0. **Bench test (Derek, ~15 min):**
   * plug an iPhone or iPad into the Rodeo;
   * play it from any MIDI app, e.g. GarageBand;
   * check it shows up as a MIDI device (and as audio);
   * for power, use a USB power meter, or run it once with the battery fully
     charged and once with it low. Unplugging the battery doesn't test the charging
     case, and running the whole Rodeo from the phone's 5 V is a heavier load.
1. **Firmware protocol:** specified in
   [RODEO_DEMO_MIDI_PROTOCOL.md](RODEO_DEMO_MIDI_PROTOCOL.md).
   * A CC map for every knob parameter and preset select.
   * The `#S` state as a SysEx message: sent on request, and streamed on change
     like `J`.
   * SysEx requests for the preset list and loading a preset.
   * Spec it in `planning/` first, like the state line; rodeo-console uses it too,
     so it's testable on the Mac.
2. **App skeleton:** the Expo project, the `rodeo-midi` module (iOS), connect,
   the keyboard.
3. **Panel mirror:** live two-way knobs, presets.
4. **Sequencer and arpeggiator:** the UI over `seq.ts`.
5. **Demo polish:** visuals, audio recording, a short demo script.
6. **Android:** the module's Kotlin side, then test on a phone.
