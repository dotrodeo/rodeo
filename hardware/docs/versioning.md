# Hardware versioning

Every Rodeo board carries a revision like **`Rev B1`**: a letter and a number.
They mean different things, the way semver's major and minor do, but suited to
something that can't be patched once it's made.

## The letter: the platform generation

A, B, C… A letter is a generation of the platform. **Every board of one letter
works with every other board of that letter.** What makes that true is a set of
interfaces, and changing any of them makes a new letter:

- the motherboard's connectors to its daughterboards: pinouts, connector types,
  what each pin carries (`hardware/rodeo-motherboard/docs/rev-b.md`);
- the satellite chain's wire contract
  (`hardware/rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md`), as far as
  its connector and electrical side go. Its protocol has its own version, carried
  in discovery; see "Firmware" below;
- the I/O connectors' pinout;
- the mounting system: hole grid, panel heights, rail positions
  (`hardware/mounting/README.md`);
- the Seed's pin map (`rodeo-sdk/common/board.h`).

Rev A to Rev B changed nearly all of these. A new letter is a new platform: its
boards start again at 1 (C1), including any that didn't change.

## The number: the board's revision

1, 2, 3… within a letter, and **counted per board**. The USB-C power board can be
at B3 while the motherboard is still B1: they share a letter, so they still work
together.

**Any change to what is fabricated is a new number**: copper, drills, outline,
solder mask, silkscreen, or a part in the BOM. That way a physical board can always
be traced to the exact files it was made from. Things that don't change a board
don't change its number:

- documentation, READMEs, renders;
- panelisation (`panel.json`, `jlcpcb-panel/`): the panel is how boards are
  delivered, not the board. A panel's fab files are regenerated from the board's.

## When a number is fixed

A revision is **fixed when it is ordered**, and marked with a git tag on the commit
whose fab outputs went to the fab:

```
rodeo-motherboard/B1
rodeo-power-usbc/B1
rodeo-midi-panel/B1
```

Component first, like `rodeo-sdk/v0.2.0`. The tag is annotated with what was ordered
and when. Before a board's first order its files can change freely; after it, the
tagged files are history.

Changes wanted for a board that's already been ordered go on its **next-revision
list** in [`revisions.md`](revisions.md) first. When the board is next spun, they
are made together, the silkscreen moves to the new number and month
(`Rev B2 · 2026-11`), and the order is tagged `…/B2`. Main may carry an unbuilt
revision in progress; only the tags say what has been built.

## Firmware

- **The SDK targets the letter.** `board.h` describes Rev B. A new number must not
  change the Seed's pin map or a connector's pinout; if it would have to, the
  change is a new letter.
- **Satellites tell the firmware what they are with their ID resistor**, not their
  revision (contract §1.6). A satellite revision that moves an MCU pin needs
  firmware that handles both revisions, or a new board kind. The pot bank and the
  encoder bank feed their ID dividers differently, and the satellite firmware reads
  both safely: that's the pattern.
- **The SDK, the satellite firmware and the chain protocol have their own
  versions** (semver, `rodeo-sdk/vX.Y.Z`, `rodeo-satellite/vX.Y.Z`, and the major
  number satellites report in discovery). They are not tied to board revisions.

## Panels

The rear panels are boards too (FR4, fabricated), and follow the same scheme with
their own tags. They carry no revision on their silkscreen; the tag and
`revisions.md` say which is which.
