// The board catalogue — the single source for /ecosystem/ and every board page.
//
// RULES (planning/WEB_CLAIMS_AUDIT.md):
//   - Every `spec` and `caveat` carries `evidence`: a repo-relative path, or
//     path:line. If you cannot cite it, it does not go on the site.
//   - `status: 'shipped'` means a bring-up test has passed on that board.
//     `built` means it is on the bench and nothing has been proven on it yet;
//     `designed` means drawn and routed and never ordered. The first run
//     arrived 2026-10-28, so these three are now genuinely different things
//     and the gap between them is the point of this file.
//   - Every board carries `rev`, and every image carries its own. The Rev-B
//     boards exist now but have not been photographed, so they are still
//     shown as CAD renders — swap a `shot` the day there is a photograph.
//   - Only boards in the platform are listed. Cut boards and planned ones
//     that are not drawn yet (the ADC expander) stay off the site.
//
// REV-B, read this before editing. The campaign sells Rev-B, and Rev-B is not
// a revision of the Rev-A board — it is a different division of labour. The
// two on-board ADCs are gone; analog is the Seed's own converter across the
// general-purpose I/O, and controls grow onto a satellite chain instead of
// onto the motherboard. Anything here claiming 24 × 16-bit analog inputs is a
// fact about a board that is not for sale.

import type { ImageMetadata } from 'astro';
import type { Rev, Status } from './site';

// Photographs of Rev-A hardware. They belong to the prototype, not to the
// product — see `prototype` at the bottom of this file. The Rev-B boards are
// built but unphotographed, so they are shown as CAD renders (see `views`).
import motherboardImg from '../assets/hardware/rodeo-motherboard.webp';
import motherboardBackImg from '../assets/hardware/rodeo-motherboard-back.webp';
import motherboardDaisyImg from '../assets/hardware/rodeo-motherboard-daisy.webp';
import motherboardIoImg from '../assets/hardware/rodeo-motherboard-io.webp';
import encoderHubImg from '../assets/hardware/rodeo-encoder-hub.webp';
import encoderHubBackImg from '../assets/hardware/rodeo-encoder-hub-back.webp';
import potentiometerImg from '../assets/hardware/rodeo-potentiometer.png';

export interface Spec {
  label: string;
  value: string;
  evidence: string;
}

export interface Caveat {
  /**
   * Shown as the badge. 'not-planned' reads as a hard limit, not an omission;
   * 'shipped' is the one that says what a bring-up test actually proved, and
   * since 2026-10-28 most boards have one.
   */
  status: Extract<
    Status,
    'shipped' | 'not-planned' | 'in-progress' | 'built' | 'at-fab' | 'designed' | 'planned'
  >;
  text: string;
  evidence: string;
}

export interface Shot {
  src: ImageMetadata;
  /** Alt text. Required — BoardFigure throws without it. */
  alt: string;
  /** Photograph of real hardware, or a CAD render. Stated in the caption. */
  media: 'photo' | 'render';
  caption: string;
  /** What the image shows: a revision, or the earlier prototype. */
  rev: Rev | 'prototype';
}

export interface Board {
  slug: string;
  name: string;
  kind: 'host' | 'satellite' | 'daughterboard';
  status: Status;
  rev: Rev;
  /** One line for the catalogue card. */
  summary: string;
  /** A paragraph for the board page. */
  body: string;
  /**
   * Board outline in millimetres, measured from the KiCad Edge.Cuts layer.
   * Drives the placeholder that stands in until there are photographs, so
   * the shape and scale on the page are the board's real ones.
   */
  outline?: { w: number; h: number };
  /** The lead image used on the card and at the top of the board page. */
  shot?: Shot;
  /** Further views, shown on the board page only. */
  gallery?: Shot[];
  specs: Spec[];
  caveats?: Caveat[];
}

// Rev-B renders: placeholders until the boards come back from the fab and can
// be photographed. Every Rev-B stackup declares black soldermask, so the renders
// match what was ordered (decision 8 in WEB_ROADMAP.md (rodeo-internal) held
// them back until it did). tools/render/render.py makes them from the KiCad
// files and `npm run renders` re-encodes them into src/assets/hardware/rev-b/.
const revB = import.meta.glob<{ default: ImageMetadata }>('../assets/hardware/rev-b/*.webp', {
  eager: true,
});

function render(slug: string, view: 'iso' | 'front' | 'back'): ImageMetadata {
  const m = revB[`../assets/hardware/rev-b/${slug}-${view}.webp`];
  if (!m) throw new Error(`no render ${slug}-${view}.webp — run npm run renders`);
  return m.default;
}

/** A Rev-B board's renders: three-quarter view as the lead, top and bottom below. */
function views(slug: string, alt: { iso: string; front: string; back: string }) {
  const shot = (view: 'iso' | 'front' | 'back', caption: string): Shot => ({
    src: render(slug, view),
    alt: alt[view],
    media: 'render',
    caption,
    rev: 'B',
  });
  return { shot: shot('iso', 'three-quarter view'), gallery: [shot('front', 'top'), shot('back', 'bottom')] };
}

// Rev-B sources. rev-b.md is authoritative for anything Rev-B and says so
// itself; the older notes scattered through the Rev-A docs are history.
const R = 'hardware/rodeo-motherboard/docs/rev-b.md';
// Rev-A sources, for the prototype page only.
const MAP = 'rev-a/rodeo-motherboard/docs/hardware-map.md';
const HUB = 'rev-a/rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md';
const HUBFW = 'rev-a/rodeo-encoder-hub/firmware/README.md';
const CHAIN = 'hardware/rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md';
// Rev-A facts cite the *frozen* Rev-A SDK. The live rodeo-sdk/ tree is being
// rewritten for Rev B, so its line numbers move — citing it for Rev-A claims
// drifted by four lines within a day of the freeze.
const SDK = 'rev-a/rodeo-sdk/README.md';
// Which boards have actually been made, and what each one wants next time it is
// spun. The Built table is the test for `built`: a board that is not in it has
// never been ordered, whatever its Gerbers look like. The Next-revision section
// is where bring-up's findings land, and two of them are motherboard caveats.
const REVS = 'hardware/docs/revisions.md';
// Milestone 1, the bring-up table: one example per board, each row carrying the
// date it passed on real Rev B hardware and what it measured. This is what
// moves a board from `built` to `shipped`.
const PLAN = 'planning/REV_B_PLAN.md';

export const boards: Board[] = [
  {
    slug: 'rodeo-motherboard',
    name: 'rodeo-motherboard',
    kind: 'host',
    status: 'shipped',
    rev: 'B',
    outline: { w: 90, h: 112 },
    ...views('rodeo-motherboard', {
      iso: "Render of the Rev-B rodeo-motherboard at an angle: a square black board with the empty Daisy Seed footprint across the middle, the boxed audio-interface ribbon header and a microSD slot along the top edge, and rows of small white connectors down the sides and along the bottom.",
      front: "Render of the top of the Rev-B rodeo-motherboard. The audio-interface ribbon header and microSD slot run along the top; connectors labelled MIDI, SPI, CHAIN and SWITCH sit down the right edge; four I2C TAPS and ten GPIO TAPS fill the bottom left beside MAIN POWER and a 1S Li-ion BATT connector. The Daisy Seed footprint is two empty rows of plated holes.",
      back: "Render of the back of the Rev-B rodeo-motherboard: black soldermask with a QR code under the words first rodeo? get started here!, the licence block, and a large RODEO MOTHERBOARD wordmark running off the bottom edge.",
    }),
    summary:
      'The host board. A Daisy Seed, two codecs, ten general-purpose I/O connectors and the satellite chain.',
    body: 'The motherboard has one job: carry the Daisy, the audio, and the connectors everything else plugs into. There are no converters on it: those pins drive a second codec instead, and high-resolution analog moved to an optional expander board. Controls no longer compete for motherboard channels, because the satellite chain grows by adding satellites. It also carries the battery, its charger and a fuel gauge, so the instrument charges over USB-C and runs off the desk.',
    specs: [
      { label: 'audio', value: '4 in / 4 out, both codecs in one callback', evidence: `${PLAN}:316` },
      { label: 'general-purpose I/O', value: '10 connectors, 8 analog-capable', evidence: `${R}:90` },
      { label: 'control growth', value: 'satellite chain on USART3', evidence: `${R}:140` },
      { label: 'I²C', value: '4 taps, pull-ups on board', evidence: `${R}:255` },
      { label: 'storage', value: 'internal SD — 1.24 MB/s read, measured', evidence: `${PLAN}:321` },
      { label: 'USB', value: 'audio, MIDI, serial and disk mode, all on the USB-C', evidence: `${PLAN}:322-324` },
      { label: 'MIDI', value: 'on a daughterboard', evidence: `${R}:443` },
      { label: 'power', value: '1S Li-ion, charger and fuel gauge', evidence: `${R}:478-487` },
      { label: 'expansion', value: 'SPI header, looped byte-exact to 6.2 Mbit/s', evidence: `${PLAN}:325` },
    ],
    caveats: [
      {
        status: 'shipped',
        text: 'It boots. The Seed came up on this board on 2026-10-28, and nine of the eleven bring-up tests have passed on it since: both codecs, MIDI, the I/O connectors, the satellite chain, the SD card, USB-MIDI, disk mode, USB audio and the SPI header. Only the OLED test has not been run.',
        evidence: `${PLAN}:315-325`,
      },
      {
        status: 'planned',
        text: 'The battery gauge on the board we have is dead, and that is our fault: J1’s pin order is the reverse of every cell in the building, the first one went in backwards and the board got very hot. The MAX17048 has answered on I²C with 0 V ever since. The next revision swaps J1 and adds the reverse protection B1 went without.',
        evidence: `${REVS}:55-71`,
      },
      {
        status: 'planned',
        text: 'The second codec’s ADC takes corrupted samples whenever its own DAC is playing — a layout fault, SDOUT running 0.15 mm alongside SDIN and SCLK for about 50 mm. Sending the DAC 16-bit words cuts it roughly a hundredfold, which is why USB audio defaults to 16-bit. The next revision moves the trace and adds series resistors.',
        evidence: `${REVS}:84-96`,
      },
      {
        status: 'not-planned',
        text: 'No on-board ADCs. The two ADS114S08s, their 24 pot connectors and the pull-downs are removed; that bank becomes an optional expander on the SPI header.',
        evidence: `${R}:619-623`,
      },
    ],
  },

  {
    slug: 'rodeo-pot-bank-4',
    name: 'rodeo-pot-bank-4',
    kind: 'satellite',
    status: 'shipped',
    rev: 'B',
    outline: { w: 20, h: 100 },
    ...views('rodeo-pot-bank-4', {
      iso: "Render of the Rev-B rodeo-pot-bank-4 at an angle: a narrow black strip with four potentiometers standing in a row on threaded bushings, the RODEO wordmark between them, and a four-pin header at one end.",
      front: "Render of the top of the Rev-B rodeo-pot-bank-4: four potentiometers evenly spaced down a 20 by 100 mm black strip, numbered 1 to 4, with RODEO in white silkscreen between each pair.",
      back: "Render of the underside of the Rev-B rodeo-pot-bank-4, where every other part is mounted: the CH32V003 microcontroller, the chain input and output connectors, and small passives, with the board name and licence in silkscreen.",
    }),
    summary: 'Four 9 mm pots on 25 mm centres, read by one CH32V003 on the chain.',
    body: 'The first Rev B satellite. Four pots in a row, digitised on the board by a cent-scale microcontroller and passed along the chain as a frame — so the cost of another four controls is another satellite, not another motherboard. The pots mount through the panel and the board hangs off their bushings, with every other part surface-mounted on the underside so assembly stays single-sided.',
    specs: [
      { label: 'controls', value: '4 × 9 mm pot, 25 mm centres', evidence: 'hardware/rodeo-pot-bank-4/README.md:3' },
      { label: 'outline', value: '20 × 100 mm', evidence: 'hardware/rodeo-pot-bank-4/README.md:19' },
      { label: 'brain', value: 'CH32V003F4P6', evidence: `${CHAIN}:103` },
      { label: 'chain link', value: 'JST SH 5-pin, in and out', evidence: `${CHAIN}:32-33` },
      { label: 'chain rate', value: '1 Mbaud', evidence: `${CHAIN}:51` },
    ],
    caveats: [
      {
        status: 'shipped',
        text: 'It works. Discovered by kind, all four pots read, its LED driven from the Seed, and it recovered from being unplugged mid-scan. In a ring with the encoder bank: about 24,600 scans in two minutes with none bad.',
        evidence: `${PLAN}:319`,
      },
    ],
  },

  {
    slug: 'rodeo-encoder-bank-4',
    name: 'rodeo-encoder-bank-4',
    kind: 'satellite',
    status: 'shipped',
    rev: 'B',
    outline: { w: 20, h: 100 },
    ...views('rodeo-encoder-bank-4', {
      iso: "Render of the Rev-B rodeo-encoder-bank-4 at an angle: a narrow black strip with four rotary encoders standing in a row, their shafts upright.",
      front: "Render of the top of the Rev-B rodeo-encoder-bank-4: four rotary encoders evenly spaced down a 20 by 100 mm black strip, numbered 1 to 4.",
      back: "Render of the underside of the Rev-B rodeo-encoder-bank-4: the CH32V003 microcontroller, the chain input and output connectors and small passives, with the board name and licence in silkscreen.",
    }),
    summary: 'Four 12 mm encoders with push switches, built to sit beside the pot bank.',
    body: 'The second satellite, deliberately the same shape as the first so the two line up on one panel with their knobs at the same height. The four push switches share a single analog pin through a resistor ladder, because the microcontroller runs out of pins before it runs out of capability — the kind of trade that keeps a satellite under the cost that makes the chain worth having.',
    specs: [
      { label: 'controls', value: '4 × 12 mm encoder with switch', evidence: 'hardware/rodeo-encoder-bank-4/README.md:3' },
      { label: 'outline', value: '20 × 100 mm, matching the pot bank', evidence: 'hardware/rodeo-encoder-bank-4/README.md:20' },
      { label: 'brain', value: 'CH32V003F4P6', evidence: `${CHAIN}:103` },
      { label: 'chain slots', value: 'board kind 6, four encoder slots', evidence: `${CHAIN}:142` },
    ],
    caveats: [
      {
        status: 'shipped',
        text: 'It works. All four encoders turned both ways and pressed, the resistor ladder reading every switch, in a two-satellite ring with the pot bank at 250 scans a second.',
        evidence: `${PLAN}:319`,
      },
    ],
  },


  {
    slug: 'rodeo-button-bank-4',
    name: 'rodeo-button-bank-4',
    kind: 'satellite',
    status: 'designed',
    rev: 'B',
    outline: { w: 20, h: 75 },
    ...views('rodeo-button-bank-4', {
      iso: 'Render of the Rev-B rodeo-button-bank-4 at an angle: a narrow black strip with four square tact switches in a row and the RODEO wordmark repeated three times running off one end.',
      front: 'Render of the top of the Rev-B rodeo-button-bank-4: four tact switches numbered 1 to 4 down a 20 by 75 mm black strip, a tiny RGB LED beside each one, and three overlapping RODEO wordmarks at the top.',
      back: 'Render of the underside of the Rev-B rodeo-button-bank-4: the CH32V003 microcontroller at the top, the chain input and output connectors labelled CHAIN IN and CHAIN OUT, and two white two-pin connectors labelled AUX 5V at the bottom.',
    }),
    summary: 'Four buttons on 12.5 mm centres, each with an addressable RGB LED beside it.',
    body: 'The third satellite, and the first that can answer back. Lit tact switches this small are either eight dollars each or barely documented, so the buttons are plain and an RGB LED sits beside each one, meant to show through a light pipe in the panel. The LEDs run from the chain’s own 3.3 V with a firmware brightness cap, or at full brightness from an auxiliary 5 V feed, switched over automatically by a MOSFET and a Schottky so there is nothing to set.',
    specs: [
      {
        label: 'controls',
        value: '4 × tact switch, 12.5 mm centres',
        evidence: 'hardware/rodeo-button-bank-4/README.md:3',
      },
      {
        label: 'indicators',
        value: '4 × WS2812B addressable RGB',
        evidence: 'hardware/rodeo-button-bank-4/README.md:70',
      },
      {
        label: 'outline',
        value: '20 × 75 mm',
        evidence: 'hardware/rodeo-button-bank-4/README.md:33',
      },
      { label: 'brain', value: 'CH32V003F4P6', evidence: 'hardware/rodeo-button-bank-4/README.md:129' },
      {
        label: 'chain slots',
        value: 'board kind 7, four button slots',
        evidence: 'hardware/rodeo-button-bank-4/README.md:165-166',
      },
      {
        label: 'LED supply',
        value: 'chain 3V3, or aux 5 V when it is plugged in',
        evidence: 'hardware/rodeo-button-bank-4/README.md:22-25',
      },
    ],
    caveats: [
      {
        status: 'designed',
        text: 'Drawn and routed, with fab outputs generated — and not ordered. The first Rev B run went to JLC on 2026-10-18 and covered thirteen boards; this is not one of them.',
        evidence: `${REVS}:9-27`,
      },
      {
        status: 'planned',
        text: 'Full brightness needs a 5 V feed, and the Rev B motherboard does not bring one to a connector — the only 5 V it exposes is the codec’s analog supply, which LED current has no business on. An aux output is on the list for the motherboard’s next revision.',
        evidence: `${REVS}:72-82`,
      },
      {
        status: 'in-progress',
        text: 'The satellite firmware for kind 7 is written — the RGB output slot, the four switches, and the WS2812 driver with its brightness cap on chain power — and tested on a computer, but it has not run on a board.',
        evidence: 'hardware/rodeo-button-bank-4/README.md:184-186',
      },
    ],
  },

  {
    slug: 'rodeo-pot-1',
    name: 'rodeo-pot-1',
    kind: 'satellite',
    status: 'designed',
    rev: 'B',
    outline: { w: 25, h: 25 },
    ...views('rodeo-pot-1', {
      iso: 'Render of the Rev-B rodeo-pot-1 at an angle: a small square black board with one potentiometer standing on a threaded bushing, its flatted shaft upright.',
      front: 'Render of the top of the Rev-B rodeo-pot-1: a single potentiometer in the middle of a 25 mm square black board, marked RV1, with a mounting hole and a row of programming pads beside it.',
      back: 'Render of the underside of the Rev-B rodeo-pot-1: the CH32V003 microcontroller, the status LED, the chain input and output connectors labelled CHAIN IN and CHAIN OUT, and the words RODEO POTENTIOMETER, Kind 1, Rev B1 in white silkscreen.',
    }),
    summary: 'One 9 mm pot on the chain, for a knob that does not belong in a row of four.',
    body: 'The pot bank cut down to one. Same pot, same microcontroller, same connectors, same firmware — so a lone knob and a bank of four read identically to the Seed and sit at the same height on a panel. It hangs from the pot’s own bushing with one M2 standoff beside it, which is there to stop the board swinging round when the nut is tightened or someone forces the knob past its stop.',
    specs: [
      { label: 'controls', value: '1 × 9 mm pot, panel mount', evidence: 'hardware/rodeo-pot-1/README.md:3' },
      { label: 'outline', value: '25 × 25 mm', evidence: 'hardware/rodeo-pot-1/README.md:34' },
      { label: 'brain', value: 'CH32V003F4P6', evidence: 'hardware/rodeo-pot-1/README.md:79' },
      {
        label: 'chain slot',
        value: 'board kind 1, one pot slot',
        evidence: 'hardware/rodeo-pot-1/README.md:118-119',
      },
      {
        label: 'firmware',
        value: 'kind 1 runs on the one satellite image',
        evidence: 'hardware/rodeo-pot-1/README.md:142-145',
      },
    ],
    caveats: [
      {
        status: 'designed',
        text: 'Drawn and routed, with fab outputs generated — and not ordered. The first Rev B run went to JLC on 2026-10-18 and covered thirteen boards; this is not one of them.',
        evidence: `${REVS}:9-27`,
      },
    ],
  },

  {
    slug: 'rodeo-encoder-1',
    name: 'rodeo-encoder-1',
    kind: 'satellite',
    status: 'designed',
    rev: 'B',
    outline: { w: 25, h: 25 },
    ...views('rodeo-encoder-1', {
      iso: 'Render of the Rev-B rodeo-encoder-1 at an angle: a small square black board with one rotary encoder standing on a threaded bushing, its knurled shaft upright.',
      front: 'Render of the top of the Rev-B rodeo-encoder-1: a single rotary encoder in the middle of a 25 mm square black board, marked SW1, with a mounting hole and a row of programming pads beside it.',
      back: 'Render of the underside of the Rev-B rodeo-encoder-1: the CH32V003 microcontroller, the status LED, the chain input and output connectors labelled CHAIN IN and CHAIN OUT, and the words RODEO ENCODER, Kind 3, Rev B1 in white silkscreen.',
    }),
    summary: 'One 12 mm encoder with a push switch, on the single pot’s footprint.',
    body: 'The encoder bank cut down to one, drawn on the same 25 mm square as the single pot: same mounting hole, same standoff, the LED moved a millimetre further out to clear the encoder’s switch pins. The push switch keeps the bank’s resistor ladder with one rung on it, so the firmware that already debounces four switches handles this one without a new line of code.',
    specs: [
      {
        label: 'controls',
        value: '1 × 12 mm encoder with push switch',
        evidence: 'hardware/rodeo-encoder-1/README.md:3-4',
      },
      {
        label: 'outline',
        value: '25 × 25 mm, the single pot’s',
        evidence: 'hardware/rodeo-encoder-1/README.md:41',
      },
      { label: 'brain', value: 'CH32V003F4P6', evidence: 'hardware/rodeo-encoder-1/README.md:86' },
      {
        label: 'chain slot',
        value: 'board kind 3, one encoder slot',
        evidence: 'hardware/rodeo-encoder-1/README.md:117-119',
      },
      {
        label: 'firmware',
        value: 'kind 3 runs on the one satellite image',
        evidence: 'hardware/rodeo-encoder-1/README.md:134-140',
      },
    ],
    caveats: [
      {
        status: 'designed',
        text: 'Drawn and routed, with fab outputs generated — and not ordered. The first Rev B run went to JLC on 2026-10-18 and covered thirteen boards; this is not one of them.',
        evidence: `${REVS}:9-27`,
      },
      {
        status: 'planned',
        text: 'The panel hole is Ø7.5 mm for an M7 × 0.75 bushing, which still has to be checked against Bourns’ datasheet before anyone drills a panel for it.',
        evidence: 'hardware/rodeo-encoder-1/README.md:57',
      },
    ],
  },

  {
    slug: 'rodeo-audio-3.5mm',
    name: 'rodeo-audio-3.5mm',
    kind: 'daughterboard',
    status: 'shipped',
    rev: 'B',
    outline: { w: 60, h: 30 },
    ...views('rodeo-audio-3.5mm', {
      iso: "Render of the Rev-B rodeo-audio-3.5mm at an angle: a black board with four 3.5 mm jacks along its front edge and a boxed 2×10 ribbon header behind them.",
      front: "Render of the top of the Rev-B rodeo-audio-3.5mm: four 3.5 mm jacks along the top edge labelled CODEC IN, SEED IN, CODEC OUT and SEED OUT, protection parts beneath them, and the 2×10 ribbon header along the bottom.",
      back: "Render of the back of the Rev-B rodeo-audio-3.5mm: the board name, revision and licence in white silkscreen above a large RODEO wordmark that runs off the bottom edge.",
    }),
    summary: 'Four stereo 3.5 mm jacks carrying the motherboard’s eight audio signals.',
    body: 'Eight audio signals leave the motherboard on one ribbon and land here, on a board that bolts to the rear rail. It is passive apart from protection: a series resistor and a clamp on every input, an ESD diode on every jack contact. A quarter-inch variant will share the circuit with different jacks.',
    specs: [
      { label: 'jacks', value: '4 × stereo 3.5 mm TRS', evidence: 'hardware/rodeo-audio-3.5mm/README.md:3' },
      { label: 'carries', value: 'the 8 audio signals on the 2×10 ribbon', evidence: 'hardware/rodeo-audio-3.5mm/README.md:3-4' },
      { label: 'protection', value: 'series R and clamp per input, ESD per contact', evidence: 'hardware/rodeo-audio-3.5mm/README.md:4-5' },
    ],
    caveats: [
      {
        status: 'shipped',
        text: 'All eight channels passed through it on 2026-10-31 — a different tone out of each output, each input naming whose tone it heard.',
        evidence: `${PLAN}:316`,
      },
    ],
  },

  {
    slug: 'rodeo-midi',
    name: 'rodeo-midi',
    kind: 'daughterboard',
    status: 'shipped',
    rev: 'B',
    outline: { w: 40, h: 25 },
    ...views('rodeo-midi', {
      iso: "Render of the Rev-B rodeo-midi at an angle: a small black board with two 3.5 mm TRS jacks along its front edge and a white five-pin connector at the back.",
      front: "Render of the top of the Rev-B rodeo-midi: two 3.5 mm jacks labelled MIDI IN and MIDI OUT, the words TRS TYPE A, and a five-pin connector labelled TO MOTHERBOARD MIDI.",
      back: "Render of the back of the Rev-B rodeo-midi: the board name, revision and licence in white silkscreen above a large RODEO wordmark that runs off the bottom edge.",
    }),
    summary: 'MIDI in and out on TRS Type A, on the back panel.',
    body: 'MIDI leaves the motherboard in Rev B and becomes two jacks on the rear rail. The active parts stay behind — the optocoupler, its resistor and diode, and the output resistors all remain on the motherboard — so this board carries the jacks, one connector and ESD protection on the output, and nothing else.',
    specs: [
      { label: 'jacks', value: '2 × 3.5 mm TRS Type A, in and out', evidence: 'hardware/rodeo-midi/README.md:3' },
      { label: 'to the motherboard', value: 'JST SH 5-pin, pin for pin with J6', evidence: 'hardware/rodeo-midi/README.md:13-16' },
      { label: 'active parts', value: 'none — optocoupler stays on the motherboard', evidence: 'hardware/rodeo-midi/README.md:4-7' },
    ],
    caveats: [
      {
        status: 'shipped',
        text: 'Notes, control changes and SysEx all came back in order through a TRS loop on 2026-10-29.',
        evidence: `${PLAN}:317`,
      },
    ],
  },

  {
    slug: 'rodeo-power-usbc',
    name: 'rodeo-power-usbc',
    kind: 'daughterboard',
    status: 'shipped',
    rev: 'B',
    outline: { w: 40, h: 20 },
    ...views('rodeo-power-usbc', {
      iso: "Render of the Rev-B rodeo-power-usbc at an angle: a small black board with a USB-C receptacle at its front edge, a status LED either side, and a white eight-pin connector behind.",
      front: "Render of the top of the Rev-B rodeo-power-usbc: the USB-C receptacle on the top edge between two status LEDs, and the eight-pin connector to the motherboard along the bottom.",
      back: "Render of the back of the Rev-B rodeo-power-usbc: a large RODEO wordmark running off the top edge above the words USB-C POWER SUPPLY, the revision and the licence.",
    }),
    summary: 'The default power input, and the board that carries USB data to the Seed.',
    body: 'A USB-C jack behind the panel, cabled to the motherboard. It is why there is USB at all: the Seed’s data lines run through this board instead of stopping inside the case, which is the single thing Rev A got wrong. USB-MIDI, USB audio, disk mode and DFU flashing all reach the panel port through it, and all of them have now been done on hardware.',
    specs: [
      { label: 'input', value: 'USB-C, 5 V sink, no USB-PD', evidence: 'hardware/rodeo-power-usbc/README.md:22' },
      { label: 'also carries', value: 'USB data for the Seed, and status LEDs', evidence: 'hardware/rodeo-power-usbc/README.md:4-5' },
      {
        label: 'proven',
        value: 'ran an iPhone’s USB-C on the phone’s power alone',
        evidence: `${PLAN}:324`,
      },
      { label: 'outline', value: '40 × 20 mm on the rear rail', evidence: 'hardware/rodeo-power-usbc/README.md:8-10' },
    ],
    caveats: [
      {
        status: 'shipped',
        text: 'Every hour of bring-up since 2026-10-28 has run through this board, and the USB data lines it exists for work: the Rodeo enumerates on the panel USB-C, and MIDI, serial, disk mode and 4×4 audio all go through it. On 2026-11-02 it did all that off an iPhone — no battery fitted, the phone powering the Seed, both codecs, two satellites and the SD card, with GarageBand opening all four channels each way.',
        evidence: `${PLAN}:322-327`,
      },
    ],
  },

  {
    slug: 'rodeo-power-barrel',
    name: 'rodeo-power-barrel',
    kind: 'daughterboard',
    status: 'built',
    rev: 'B',
    outline: { w: 60, h: 40 },
    ...views('rodeo-power-barrel', {
      iso: "Render of the Rev-B rodeo-power-barrel at an angle: a black board with a barrel jack and a USB-C receptacle along its front edge, two power inductors, an electrolytic capacitor, a white eight-pin connector and a two-pin auxiliary output.",
      front: "Render of the top of the Rev-B rodeo-power-barrel: USB-C and the barrel jack (labelled DC IN 7–20 V, centre positive) along the top edge, the regulators and inductors in the middle, and the eight-pin motherboard connector and AUX 5V 3A MAX output along the bottom.",
      back: "Render of the back of the Rev-B rodeo-power-barrel: the words USB-C + BARREL POWER SUPPLY, the revision and licence, and a large RODEO wordmark running off the bottom edge.",
    }),
    summary: 'The USB-C board plus a barrel jack and a separate 5 V output for LED strips.',
    body: 'For builds that need more current than a USB port will give. The barrel jack takes 7–20 V — a 9 V or 12 V adapter in practice — and wins automatically when it is plugged in, while USB stays connected for data and takes over again if the adapter is pulled. A second regulator feeds an auxiliary 5 V output for LED strips, kept off the motherboard’s supply on purpose.',
    specs: [
      { label: 'adapter', value: '5.5 × 2.1 mm, centre positive, 7–20 V', evidence: 'hardware/rodeo-power-barrel/README.md:21' },
      { label: 'switchover', value: 'barrel takes priority, USB is the fallback', evidence: 'hardware/rodeo-power-barrel/README.md:4-6' },
      { label: 'aux output', value: 'separate 5 V rail for LED strips', evidence: 'hardware/rodeo-power-barrel/README.md:3-4' },
    ],
    caveats: [
      {
        status: 'built',
        text: 'It came back from JLC with the rest of the run and is on the bench. Bring-up has been done with the USB-C board, so nothing on this one — the barrel jack, the switchover, the auxiliary 5 V — has been powered up or measured yet.',
        evidence: `${PLAN}:327`,
      },
    ],
  },

  {
    slug: 'rodeo-power-switch',
    name: 'rodeo-power-switch',
    kind: 'daughterboard',
    status: 'built',
    rev: 'B',
    outline: { w: 30, h: 20 },
    ...views('rodeo-power-switch', {
      iso: "Render of the Rev-B rodeo-power-switch at an angle: a small black board with a latching push button whose plunger overhangs the front edge, and a white two-pin connector at the back.",
      front: "Render of the top of the Rev-B rodeo-power-switch: the push button SW1 on the top edge and a two-pin connector labelled TO MOTHERBOARD SWITCH below it.",
      back: "Render of the back of the Rev-B rodeo-power-switch: the board name, revision and licence in white silkscreen above a large RODEO wordmark that runs off the bottom edge.",
    }),
    summary: 'A latching button on the back panel that turns the synth off without stopping the charge.',
    body: 'One button and one connector, and nothing else — the resistors and the filter live on the motherboard. Pressed in, it enables the buck-boost converter; out, the synth is off and the battery carries on charging. The plunger comes through a slot in the panel and works bare, so the printed cap is optional.',
    specs: [
      { label: 'switch', value: 'latching push button, panel mount', evidence: 'hardware/rodeo-power-switch/README.md:3' },
      { label: 'to the motherboard', value: 'JST SH 2-pin on J3', evidence: `${R}:528` },
      { label: 'outline', value: '30 × 20 mm', evidence: 'hardware/rodeo-power-switch/README.md:10-11' },
    ],
    caveats: [
      {
        status: 'built',
        text: 'Made in the same run and on the bench. Its test is part of the power bring-up — that switching off cuts everything whether or not something is plugged in — and that has not been run on it yet.',
        evidence: `${PLAN}:352`,
      },
    ],
  },

];

/**
 * Rev-A hardware: the prototype that proved the platform and is not for sale.
 *
 * Kept separate from `boards` on purpose. These are the only photographs that
 * exist, and putting them beside Rev-B specifications would show a reader a
 * board with two converters on it while the text describes one without. They
 * belong to the prototype story.
 */
export interface Change {
  area: string;
  revA: string;
  revB: string;
  why: string;
  evidence: string;
}

export const prototype = {
  rev: 'prototype' as const,

  /** The prototype's satellite, kept here with the board it was built for. */
  satellite: {
    slug: 'rodeo-potentiometer',
    name: 'rodeo-potentiometer',
    kind: 'satellite',
    status: 'shipped',
    rev: 'A+B',
    summary: 'One pot, three pins, entirely passive — and it still plugs into Rev B.',
    body: 'The simplest board in the family, and the only one that survives the Rev B redesign unchanged. The general-purpose I/O connectors deliberately use the pinout this board already has, so a single pot drops onto any analog-capable connector without an adapter. Use it where one control does not justify a satellite.',
    shot: {
      src: potentiometerImg,
      alt: 'Render of the rodeo-potentiometer board: a small square PCB with a single potentiometer footprint, two gold mounting pads, a resistor, a capacitor and a three-pin JST-SH connector at the bottom edge.',
      media: 'render',
      caption: 'rodeo-potentiometer',
      rev: 'A',
    },
    specs: [
      { label: 'connector', value: '3-pin JST SH: GND, wiper, supply', evidence: `${R}:587-591` },
      { label: 'Rev B fit', value: 'plugs into any analog-capable I/O connector', evidence: `${R}:587-591` },
      { label: 'electronics', value: 'passive — one RC filter', evidence: `${SDK}:133-134` },
    ],
  },

  /** Proven on the fabricated Rev-A boards. Past tense on purpose. */
  proven: [
    { label: 'analog in', value: '24 channels, 16-bit', evidence: `${MAP}:76` },
    {
      label: 'conversion path',
      value: 'bench-confirmed on silicon, 2026-08-14',
      evidence: `${SDK}:133-134`,
    },
    { label: 'digital in', value: '12 raw Daisy GPIO', evidence: `${MAP}:114` },
    { label: 'audio', value: 'line in and out, 3.5 mm', evidence: `${MAP}:143` },
    { label: 'MIDI', value: 'in and out, TRS', evidence: `${MAP}:165-166` },
    { label: 'control bus', value: 'I²C', evidence: `${MAP}:173` },
    { label: 'encoder hub', value: '9 encoders on one I²C target', evidence: `${HUB}:39` },
    {
      label: 'hub firmware',
      value: 'running on a fabbed board, 2026-09-12',
      evidence: `${HUBFW}:177-191`,
    },
    {
      label: 'register contract',
      value: 'silicon-verified, 2026-08-20',
      evidence: `${SDK}:137-138`,
    },
    { label: 'host', value: 'Daisy Seed3, 40-pin footprint', evidence: `${MAP}:15` },
  ] satisfies Spec[],

  /** The limits Rev-A shipped with. Named, not buried. */
  limits: [
    {
      status: 'not-planned' as const,
      text: 'The panel USB-C is power only. The Seed’s USB data lines dead-end at pins 37/36, so there is no USB-MIDI, no USB audio and no flashing over the panel connector.',
      evidence: `${MAP}:213-221`,
    },
    {
      status: 'not-planned' as const,
      text: 'The encoder hub carries the only I²C pull-ups on the bus, so the host needs a hub present for I²C to work at all. A blank display with no hub attached is a missing-pull-up symptom, not firmware.',
      evidence: `${HUB}:37`,
    },
  ] satisfies Caveat[],

  /** What Rev-B does differently, and why it is different. */
  changes: [
    {
      area: 'Analog inputs',
      revA: '24 channels at 16 bits, from two converters soldered to every board',
      revB: 'the Seed’s own converter across eight I/O connectors; the 16-bit bank becomes an optional board',
      why: 'Every build paid about nineteen dollars for converters whether it needed them or not. Satellites cover ordinary knobs, so the precision bank became opt-in.',
      evidence: `${R}:231`,
    },
    {
      area: 'Audio',
      revA: 'one codec, in and out',
      revB: 'a second codec on the pins the converters vacated — four in, four out',
      why: 'The freed pins were worth more as audio than as analog inputs that satellites could supply instead.',
      evidence: `${R}:341`,
    },
    {
      area: 'Growing a panel',
      revA: 'one cable per control, back to a motherboard channel or an I²C hub',
      revB: 'satellites chained together, each reading its own controls',
      why: 'Controls stopped being bounded by the motherboard. Position in the chain is the address, so there is nothing to configure and no respin when a panel grows.',
      evidence: `${R}:140`,
    },
    {
      area: 'USB',
      revA: 'power only — the data lines went nowhere',
      revB: 'data through the power board: USB-MIDI, USB audio and firmware flashing',
      why: 'The most-checked spec on the Rev-A board, and the one most worth fixing.',
      evidence: `${R}:317`,
    },
    {
      area: 'Power',
      revA: 'USB-C in, straight to a regulator — tethered to a wall for as long as it runs',
      revB: 'a 1S Li-ion battery with a charger, power path and fuel gauge on the motherboard',
      why: 'Rev A had no battery of any kind. Rev B charges over USB-C and keeps charging with the switch off, which is the difference between a thing on a bench and an instrument you can pick up.',
      evidence: `${R}:478-487`,
    },
    {
      area: 'Jacks and MIDI',
      revA: 'on the motherboard',
      revB: 'daughterboards on a rear rail',
      why: 'A smaller host board, and connectors a build does not need are connectors it does not pay for.',
      evidence: `${R}:443`,
    },
  ] satisfies Change[],

  shots: [
    {
      src: motherboardImg,
      alt: 'Photograph of the assembled prototype Rodeo motherboard: a black PCB with the Daisy Seed footprint, two banks of analog connectors, a row of digital connectors and the panel I/O along one edge.',
      media: 'photo' as const,
      caption: 'rodeo motherboard, assembled',
      rev: 'prototype' as const,
    },
    {
      src: motherboardBackImg,
      alt: 'Photograph of the back of the prototype motherboard, showing the ground pour, the board name in silkscreen and the mounting holes.',
      media: 'photo' as const,
      caption: 'the same board, reverse',
      rev: 'prototype' as const,
    },
    {
      src: motherboardIoImg,
      alt: 'Close-up photograph of the prototype motherboard’s panel I/O: the MIDI DIN connector, the 3.5 mm audio jacks and the USB-C connector along the board edge.',
      media: 'photo' as const,
      caption: 'panel I/O — the USB-C here is power only',
      rev: 'prototype' as const,
    },
    {
      src: motherboardDaisyImg,
      alt: 'Close-up photograph of the prototype motherboard’s Daisy Seed footprint: two rows of plated through-holes with the UART, I2C and SPI headers above them and the indicator LEDs beside.',
      media: 'photo' as const,
      caption: 'the Daisy Seed footprint and bus headers',
      rev: 'prototype' as const,
    },
    {
      src: encoderHubImg,
      alt: 'Photograph of the rodeo-encoder-hub board: a small square PCB with nine three-pin connectors around its edge and an STM32 microcontroller in the centre.',
      media: 'photo' as const,
      caption: 'rodeo-encoder-hub — retired in Rev-B',
      rev: 'prototype' as const,
    },
    {
      src: encoderHubBackImg,
      alt: 'Photograph of the back of the rodeo-encoder-hub board, showing the I²C address jumpers and the ground pour.',
      media: 'photo' as const,
      caption: 'the hub’s reverse, with its address jumpers',
      rev: 'prototype' as const,
    },
  ] satisfies Shot[],
};

export const liveBoards = boards.filter((b) => b.status !== 'retired');
export const host = boards.find((b) => b.kind === 'host')!;
export const satellites = boards.filter((b) => b.kind === 'satellite');
export const daughterboards = boards.filter((b) => b.kind === 'daughterboard');
