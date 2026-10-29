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

### Motherboard (`rodeo-motherboard`)

- **Battery polarity: swap J1 and add reverse protection.** Found at bring-up
  (2026-10-28): every cell Derek has is wired opposite to J1 (pin 1 `BATT_N`,
  pin 2 `BATT_P`), so the first one went in reversed, and the board got very
  hot. Afterwards the MAX17048 (U4) answers on I²C but VCELL, SOC and CRATE
  read 0 with 4.1 V on the cell; it was probably damaged through its VDD/CELL
  pins. Two changes:
  * Swap J1's pins to match the packs we actually buy, and keep the + mark.
  * Add reverse protection after all. B1 relied on the connector and the
    silkscreen because a high-side PMOS with its gate on board ground doesn't
    work with the DW01A in the negative lead (board ground is behind Q1). To
    check: a PMOS in the positive lead with its gate on `BATT_N` (the cell's
    own negative, ahead of Q1) through a resistor, drain to J1 and source to
    `BATT_P`. It turns on at Vgs = −Vcell (pick one specified at −2.5 V for an
    empty cell) and conducts both ways, so the charger still charges through
    it. R8, the DW01A's supply, would move to the load side so a reversed cell
    can't drive U3's VCC below its VSS. Work out where the current went in B1
    (U2's and U4's substrate diodes, and Q1) before settling on it.
- **Add a 5 V aux output.** The power budget leaves ≈1 A at 5 V for LEDs, but
  nothing brings `+5V_SYS` to a connector: it reaches only regulators, ferrites
  and capacitors. The only 5 V on a connector is `+5V_A` (audio ribbon pin 10),
  the codec's filtered analog supply, which LED current would put into the
  audio. Add a 2-pin JST PH on `+5V_SYS` through a ≈1 A PTC, like the barrel
  board's LED-strip output (J4). It is the only 5 V still there on battery
  (the power boards' outputs need an input plugged in), so it suits RGB or
  bright satellites, which take their LED power from it and their logic from
  the chain (see the chain contract, §1.4). On B1, a pigtail on a `+5V_SYS`
  output capacitor (C17, C19 or C22) or FB1's input side stands in. (Found
  2026-10-19.)

### MIDI, audio, barrel, switch, encoder bank, panels

- Nothing yet beyond the copyright year.
