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
// rodeo-002 v1.0.0 is the first build of THIS instrument that is finished
// rather than a bring-up: MIDI, eight voices, the texture chain, the panel and OLED, sixteen
// factory presets with naming, the gesture recorder with per-slot persistence,
// settings pages, and CC in and out.

#pragma once

namespace rodeo002
{
constexpr int  kVersionMajor = 1;
constexpr int  kVersionMinor = 0;
constexpr int  kVersionPatch = 0;
constexpr char kVersionString[] = "1.0.0";

} // namespace rodeo002
