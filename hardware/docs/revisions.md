# Board revisions

Each board's built revisions, and what's waiting for its next one. The rules are in
[`versioning.md`](versioning.md). A revision is tagged on the commit whose fab
outputs were ordered.

## Built

| Board | Revision | Tag | Ordered |
|---|---|---|---|
| Motherboard | B1 | `rodeo-motherboard/B1` | JLC, 2026-10-18 (prototype run) |
| 3.5 mm audio jacks | B1 | `rodeo-audio-3.5mm/B1` | JLC, 2026-10-18 |
| MIDI | B1 | `rodeo-midi/B1` | JLC, 2026-10-18 |
| USB-C power | B1 | `rodeo-power-usbc/B1` | JLC, 2026-10-18 |
| Barrel + USB-C power | B1 | `rodeo-power-barrel/B1` | JLC, 2026-10-18 |
| Power switch | B1 | `rodeo-power-switch/B1` | JLC, 2026-10-18 |
| Pot bank ×4 | B1 | `rodeo-pot-bank-4/B1` | JLC, 2026-10-18 |
| Encoder bank ×4 | B1 | `rodeo-encoder-bank-4/B1` | JLC, 2026-10-18 |
| Audio panel | B1 | `rodeo-audio-3.5mm-panel/B1` | JLC, 2026-10-18 |
| MIDI panel | B1 | `rodeo-midi-panel/B1` | JLC, 2026-10-18 |
| USB-C power panel | B1 | `rodeo-power-usbc-panel/B1` | JLC, 2026-10-18 |
| Barrel power panel | B1 | `rodeo-power-barrel-panel/B1` | JLC, 2026-10-18 |
| Power switch panel | B1 | `rodeo-power-switch-panel/B1` | JLC, 2026-10-18 |

All thirteen B1 tags point at `ebca252`, the commit that recorded the order; none of
these boards' files changed between their last edit (2026-10-16 to 2026-10-18) and
that commit.

## Next revision

Changes to make the next time each board is spun. Add to these as the B1 boards are
brought up.

### Every board but the motherboard

- **Copyright year.** Silkscreen says `© DOT RODEO LLC`; make it
  `© 2026 DOT RODEO LLC`, as on the motherboard and in `silkscreen.md`. (Found
  2026-10-19.)

### USB-C power (`rodeo-power-usbc`)

- **Add the copyright line**, which B1 lacks, with the year. A first pass (commit
  `400a522`, reverted in `e094a61` so B1's files stay as ordered) set the back
  silkscreen block (name, revision, licence, source, copyright) in 0.8 mm text to
  make room; start from it.

### Pot bank ×4 (`rodeo-pot-bank-4`)

- **Consider feeding the ID divider from 3V3**, as the encoder bank does, instead
  of from PC2. The satellite firmware handles both (it reads first without driving
  PC2), so this is tidiness, not a fix; it would free PC2.

### Motherboard, MIDI, audio, barrel, switch, encoder bank, panels

- Nothing yet beyond the copyright year.
