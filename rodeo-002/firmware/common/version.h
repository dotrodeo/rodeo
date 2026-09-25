// rodeo-002 — the firmware's version, in one place.
//
// Semantic versioning (Derek, 2026-09-25). For an instrument that means:
//
//   MAJOR  the panel or a stored format changes in a way a player or a saved
//          preset would notice -- a control moving, a bank that has to be
//          re-saved, a gesture that no longer loads.
//   MINOR  something new that takes nothing away: an engine, an effect type, a
//          destination, a page.
//   PATCH  a fix. The instrument does what it already said it did.
//
// v1.0.0 is the first build that is a finished instrument rather than a
// bring-up: MIDI, eight voices, the texture chain, the panel and OLED, sixteen
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
