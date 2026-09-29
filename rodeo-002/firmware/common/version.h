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
// preset selector rather than a second way to open the menu. Presets and
// gestures saved by v2 load unchanged; it is your hands that have to relearn
// something, which is exactly what MAJOR is for.

#pragma once

namespace rodeo002
{
constexpr int  kVersionMajor = 3;
constexpr int  kVersionMinor = 0;
constexpr int  kVersionPatch = 0;
constexpr char kVersionString[] = "3.0.0";

} // namespace rodeo002
