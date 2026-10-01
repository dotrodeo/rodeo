// rodeo-002 — this INSTRUMENT's firmware version, in one place.
//
// Scoped to rodeo-002 and nothing else. The repository is the whole platform --
// the motherboard, the satellites, rodeo-001 -- and each of those versions
// itself; a bare version number here would claim the lot. Tags are written
// `rodeo-002/v1.0.0` for the same reason -- component first, so a listing groups
// by component rather than by version (Derek, 2026-09-25).
//
// Semantic versioning. For an instrument that means:
//
//   MAJOR  the panel or a stored format changes in a way a player or a saved
//          preset would notice -- a control moving, a bank that has to be
//          re-saved, a gesture that no longer loads.
//   MINOR  something new that takes nothing away: an engine, an effect type, a
//          destination, a page.
//   PATCH  a fix. The instrument does what it already said it did.
//
// rodeo-002 v1.0.0 was the first build of THIS instrument that was finished
// rather than a bring-up: MIDI, eight voices, the texture chain, the panel and
// OLED, sixteen factory presets with naming, the gesture recorder with per-slot
// persistence, settings pages, and CC in and out.
//
// v2.0.0 adds eight empty user slots, a factory reset and a boot splash. MAJOR
// rather than MINOR because the bank grew from 16 slots to 24, which changes
// the stored layout: a v1 bank is rejected and the factory sixteen come back,
// so anything saved over a factory slot is lost. That is exactly the case the
// rule above exists for -- "a bank that has to be re-saved" -- and the version
// number is the only warning a player gets.
//
// v3.0.0 is the panel again, and nothing stored: a SIXTH encoder (NAV, hub slot
// 6 / J8) alongside MAIN, and one meaning per gesture. NAV turns to move and
// taps to enter; MAIN turns to change the highlighted value and taps for the
// gesture recorder; a hold on NAV goes back one level. The menu no longer times
// out, and -- the last piece -- turning NAV on the normal screen is now the
// preset selector rather than a second way to open the menu. An empty user slot
// is part of that scroll and so it had to become playable: it plays Init while
// still being named `-- empty --`. Presets and gestures saved by v2 load
// unchanged -- Init is substituted when an unsaved slot is loaded, not written
// into the bank -- so it is your hands that have to relearn something, which is
// exactly what MAJOR is for.
//
// The header follows the selector: `NN-Preset name`, zero-padded, where the
// voice count used to be. Where you are in the bank is now worth a glance
// several times a minute; how many voices are sounding had not been worth one
// since bring-up. Load left the menu for the same reason -- NAV's turn does it
// in one detent -- and the menu now has precedence over the panel: nothing you
// touch closes it, so it cannot go away mid-thought.

// v4.0.0 drops Grainlet, Particle, String and Karplus (Derek, 2026-09-29,
// after living with the instrument): four engines nobody reached for, two of
// which could not be played polyphonically. MAJOR because a preset that names
// one no longer loads intact -- it falls back to virtual-analog and says so.
// Their stored ids 8..11 are retired, never reused.
//
// Two things fell out of it. Every engine left is polyphonic, so the voice-mode
// override (Mono forced on the physical models) is gone: Poly is Poly on every
// engine. And `StringVoice` alone was 5368 bytes of the per-voice engine union
// against 148 for everything remaining, so DTCM fell from 85.5% to 53.8%.
//
// It also adds the analog behaviour that room paid for, at no panel cost: every
// voice is tuned slightly wrong and wanders slowly, and every voice has a place
// in the stereo field. SPREAD NOW MEANS STEREO WIDTH in every polyphonic mode,
// where it used to do nothing outside Unison -- at 0 the output is the mono sum
// it always was, sample for sample. Unison's own detune was +/-6 cents under a
// comment claiming a semitone; it is +/-25 cents now. Presets load unchanged
// but SOUND different: wider, and actually stacked in Unison rather than eight
// phase-locked copies of one oscillator.
//
// And a SUB-OSCILLATOR: a plain waveform under or over the note, on the two
// shift knobs Env->Timbre and Env->Morph gave up (both moved to the patch page,
// losing nothing but the travel). Not a second macro engine -- that would have
// halved the polyphony on the dearer engines to buy beating, which Unison
// already gives. Level 0 is off and is charged nothing. It fits the last two
// spare bytes of StoredPreset, so no version bump and no preset lost; those
// bytes have always been zero, which decodes as off, -12, sine.
//
// The FACTORY BANK is rebuilt from nothing for all of the above: sixteen sounds
// covering all seven engines, all three voice modes, all three filter types, all
// five Space types, and the sub above the note, below it and off. Factory Reset
// is what loads it -- the stored bank is otherwise kept, since the format did
// not change.

// v5.0.0 gives every preset its own GAIN, because the factory bank spanned 19 dB
// of peak level and switching presets meant riding the master volume (Derek,
// 2026-10-01). It is a trim after the chain, so turning a preset down does not
// also turn its drive down, and it is on the patch settings page in dB.
//
// MAJOR because the stored format moved: PresetBank::kVersion 2 -> 3. NOTHING
// SAVED IS LOST -- v3 only reinterprets the four bytes `release` used to hold,
// which had been dead since the envelope became ADS, so the layout is identical
// and a v2 bank is migrated in place by putting unity in that field. The
// factory bank's own levels all changed, though, which every preset notices.
//
// Also: the sub's triangle is DaisySP's naive one rather than its polyblep one.
// WAVE_POLYBLEP_TRI is the only waveform built on a leaky integrator and
// Oscillator::Init never initialises its state, so on the host two factory
// presets produced NaN from the first sample. BSS zeroing hid it on the board.

#pragma once

namespace rodeo002
{
constexpr int  kVersionMajor = 5;
constexpr int  kVersionMinor = 0;
constexpr int  kVersionPatch = 0;
constexpr char kVersionString[] = "5.0.0";

} // namespace rodeo002
