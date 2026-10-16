# Rev B silkscreen checklist and audit

What every Rev B board should carry on its silkscreen, split into **needed for the
Kickstarter run** (the boards backers receive) and **fine to add after**. Then an
audit of each board as committed on 2026-10-15.

The silkscreen is for the person assembling, wiring and repairing the synth, which
for Rodeo's backers is the backer. Labels a player needs (MIDI IN, knob names) belong
on the panel.

## Checklist

### Every board

| Item | Text | Kickstarter? | Why |
|---|---|---|---|
| Board name | `RODEO <BOARD NAME>` | **Needed** | Builders sort ~23 boards per kit; support starts with "which board is this?" |
| Revision and date | `Rev B1 · 2026-10` | **Needed** | Rev A and Rev B boards will coexist; firmware and docs are per revision |
| Licence and source location | `CERN-OHL-W-2.0` and `github.com/dotrodeo/rodeo` or `rodeooh.rodeo/source` (or a QR code to it) | **Needed** | The licence asks for a notice of the source location on distributed products; the silkscreen is the reliable place |
| Pin 1 / key on every connector | footprint markers (check they survived) | **Needed** | Builders plug in every cable |
| Connector function names | e.g. `CHAIN IN`, `TO MOTHERBOARD MIDI` | **Needed** | The reference designator (J2) means nothing to a builder |
| Polarity marks | `+` on electrolytics and power connectors, diode/LED cathode bars | **Needed** | Usually from the footprint; check |
| JLC order-number marker | `JLCJLCJLCJLC` on the back, in an out-of-the-way spot, with "specify a location" ticked when ordering | **Needed** | Otherwise JLC prints its order number where it likes, possibly on the visible face |
| Copyright | `© 2026 DOT RODEO LLC` | After | The licence notice covers most of it; nice for provenance |
| QR code to that board's docs page | | After | The motherboard already has one; one per board is a nicety |
| Serial / QC box | a small empty rectangle to mark "tested" or a serial | After (but useful for the run) | ~1,150 boards to test for 50 kits; a tick box makes it obvious which are done |
| OSHWA certification ID and logo | `US00xxxx` | After | Free, fits the licence and the platform pitch; needs the certification first |

### By board type

| Board | Item | Kickstarter? |
|---|---|---|
| Satellites | `CHAIN IN` / `CHAIN OUT` with an arrow showing direction | **Needed**: a swapped pair leaves the ring open, and a 30-satellite chain makes it likely |
| Satellites | Board kind (`KIND 5`), so a discovery report can be matched to a board | **Needed** |
| Satellites | Programming pad names `SWIO 3V3 GND NRST` | **Needed**: you program every satellite at assembly, through a jig |
| Satellites | Knob numbers `1 2 3 4`, matching slot order | **Needed** |
| Power boards | Barrel input rating `7–20 V ⎓ 2.5 A min` with the centre-positive symbol | **Needed** |
| Power boards | Aux output `5 V ⎓ 3 A max` and `+` at J4 | **Needed** |
| Power boards | J2 pin names (`VIN GND D− D+ GND VIN PG CHG`) | **Needed**: lets a wrong cable be found with a meter |
| Power boards | LED meanings `PWR` / `CHG` | After (the panel can say it) |
| Jack boards | Jack functions (`SEED OUT`, `CODEC IN`…) on the back, readable from inside the case | **Needed**: inside the case the panel's labels can't be seen, and left/right is mirrored |
| Jack boards | Ribbon key and `GND ROW` mark on J1 | **Needed** |
| MIDI board | `TRS TYPE A` | **Needed**: the MIDI Association asks TRS MIDI gear to state the type |
| Motherboard | Connector group names | Done |
| Motherboard | Battery polarity and cell type at the battery connector | **Needed** |

## Regulatory, for the Kickstarter run

Not legal advice; this is what to settle before launch, because it decides what
else goes on the boards and in the manual.

* **Decide the shipping region first** (`kickstarter/docs/OPEN_QUESTIONS.md` still
  has it open). US-only is far simpler than adding the EU and UK.
* **US (FCC Part 15).** Rodeo is a digital device (480 MHz Seed, 350–400 kHz bucks).
  Assembled boards sold to builders as parts of a kit may be treated as
  subassemblies or as a kit that must comply as a finished device; which applies
  turns on how they're marketed. **Needed before launch:** a short consult with an
  EMC test lab, and if Class B testing is required, the Supplier's Declaration of
  Conformity and the Part 15 statement in the manual (not necessarily on the board).
* **EU / UK (if shipping there).** CE / UKCA under the EMC and RoHS directives,
  WEEE producer registration, and the crossed-out bin symbol on the product or
  packaging. The Low Voltage Directive doesn't apply (everything is under 50 V AC /
  75 V DC). **Don't print CE, UKCA or FCC marks unless the conformity work is done.**
* **Mains adapter:** if a kit includes one, buy a certified adapter for each market;
  never your own design.
* **Battery:** if kits ship with a lithium cell, UN38.3-tested cells and carrier
  paperwork are needed; shipping without a cell and specifying one is far simpler.
* **RoHS:** lead-free finish (HASL lead-free or ENIG) on every JLC order; all parts
  chosen so far are listed RoHS.
* **Trademarks:** `USB-C` and `MIDI` as text are fine; the USB logos need a USB-IF
  licence, so leave them off.

## Audit (2026-10-15)

| Board | Has | Needed for Kickstarter | After |
|---|---|---|---|
| **Motherboard** | `RODEO`, `MOTHERBOARD`, connector group names (`MAIN POWER`, `BATT`, `AUDIO INTERFACE`, `I2C TAPS`, `GPIO TAPS`, `SPI`, `SATT`, `MIDI`, `SWITCH`), QR code to the getting-started page | Revision and date; licence and source location; `SATT` → `CHAIN OUT` with an arrow; battery polarity and cell type at `BATT`; JLC marker | Copyright; QC box; OSHWA |
| **USB-C power** | `RODEO`, `USB-C POWER` (back) | Revision and date; licence and source; J2 pin names; JLC marker | `PWR`/`CHG` LED labels; copyright; QR; QC box |
| **Barrel power** | `RODEO`, `BARREL POWER` (back) | Revision and date; licence and source; barrel rating `7–20 V ⎓ 2.5 A min` with the centre-positive symbol; aux `5 V ⎓ 3 A max` and `+` at J4; J2 pin names; JLC marker | LED labels; copyright; QR; QC box |
| **3.5 mm jack board** | Reference designators only | Name; revision and date; licence and source; jack functions `SEED OUT · CODEC OUT · SEED IN · CODEC IN` on the back; J1 key and `GND ROW` mark; JLC marker | Copyright; QR; QC box |
| **MIDI board** | Reference designators only | Name; revision and date; licence and source; `MIDI IN` / `MIDI OUT` at the jacks; `TRS TYPE A`; `TO MOTHERBOARD MIDI` at J1; JLC marker | Copyright; QR; QC box |
| **Pot bank ×4** | `SATTELITE INPUT`, `SATTELITE OUTPUT` (back) | **Fix the spelling: `SATELLITE`**, better `CHAIN IN →` / `→ CHAIN OUT`; name `POT BANK ×4`; `KIND 5`; revision and date; licence and source; knob numbers 1–4; `SWIO 3V3 GND NRST` at J3; JLC marker | Copyright; QR; QC box |
| **Encoder bank ×4** | `SATTELITE INPUT`, `SATTELITE OUTPUT` (back) | Same as the pot bank, with `ENCODER BANK ×4` and `KIND 6` | Copyright; QR; QC box |

Notes from the audit:

* No board has a revision, a licence notice or a JLC marker yet.
* Satellites are 20 mm wide: use 0.8–1 mm text on the back, and put the licence on
  one line (`CERN-OHL-W-2.0 rodeooh.rodeo/source`) or use a QR code.
* The two power boards' back text currently runs off the board edge and over pads
  (DRC flagged it as silkscreen warnings); if it gets reworked for the additions,
  keep it inside the outline so the fab doesn't clip it.

## Status after the silkscreen pass (2026-10-16)

All seven boards now carry their name, `Rev B1 · 2026-10`, `CERN-OHL-W-2.0`, a
source link and `© DOT RODEO LLC` (the motherboard on the back, beside its QR code).
Connector function labels, pin-1 marks and the items below are in.

| Board | Added | Still open |
|---|---|---|
| Motherboard | Revision (front); licence, source, copyright (back); `CHAIN`; battery `−`/`+` and `1S Li-ion` | — |
| USB-C power | Back block; J2 pin letters `C P V G + − G V` above the connector | — |
| Barrel power | Back block with the board name; `DC IN 7-20 V 2.5 A MIN CENTER +`; `+ AUX 5V 3A MAX`; J2 pin letters | — |
| 3.5 mm jacks | Jack functions beside each jack (front); back block; J1 `1` | — |
| MIDI | `MIDI IN` / `MIDI OUT`; `TRS TYPE A`; `TO MOTHERBOARD MIDI`; J1 `1`; back block | — |
| Pot bank ×4 | `CHAIN INPUT` / `CHAIN OUTPUT` (spelling fixed); name, revision, licence, source, copyright; knob numbers front and back; J3 letters `S V G N` and pin 1; `Kind 5` | — |
| Encoder bank ×4 | Same as the pot bank, with `Kind 6` (knob numbers on the front only) | Knob numbers on the back (optional) |

Decided and deferred:

* **No JLC order-number marker** for now (Derek, 2026-10-15): JLC places its order
  number where it chooses.
* **Source links** print the future GitHub organisation (`github.com/dotrodeo/rodeo`
  on the motherboard) and `rodeooh.rodeo/source` on the daughterboards. Before the
  first Rev B production order: rename the organisation, set up the
  `rodeooh.rodeo/source` redirect, and update `LICENSE` / `LICENSES/MIT.txt` to
  DOT RODEO LLC and the new source location once the LLC is formed.

