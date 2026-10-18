// The board catalogue — the single source for /ecosystem/ and every board page.
//
// RULES (web/docs/WEB_CLAIMS_AUDIT.md):
//   - Every `spec` and `caveat` carries `evidence`: a repo-relative path, or
//     path:line. If you cannot cite it, it does not go on the site.
//   - `status: 'shipped'` means proven on real hardware. `designed` means
//     drawn and routed and nothing more. Nothing here has been fabricated.
//   - Every board carries `rev`, and every image carries its own, because the
//     only photographs that exist are of Rev-A hardware.
//   - Retired boards stay listed, marked retired. A platform with an
//     inaccurate parts list loses trust instantly.
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
// product — see `prototype` at the bottom of this file. No Rev-B board has
// been fabricated, and its renders are held back until the soldermask
// stackups agree (WEB_ROADMAP.md (rodeo-internal), decision 8).
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
  /** Shown as the badge. 'not-planned' reads as a hard limit, not an omission. */
  status: Extract<Status, 'not-planned' | 'in-progress' | 'at-fab' | 'designed' | 'planned'>;
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
  /** The revision the image depicts, which is not always the board's own. */
  rev: Rev;
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
  /** Why it was cut. Retired boards only. */
  retiredReason?: string;
}

// Rev-B sources. rev-b.md is authoritative for anything Rev-B and says so
// itself; the older notes scattered through the Rev-A docs are history.
const R = 'hardware/rodeo-motherboard/docs/rev-b.md';
// Rev-A sources, for the prototype page only.
const MAP = 'rev-a/rodeo-motherboard/docs/hardware-map.md';
const HUB = 'rev-a/rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md';
const HUBFW = 'rev-a/rodeo-encoder-hub/firmware/README.md';
const CHAIN = 'hardware/rodeo-motherboard/docs/RODEO_SATELLITE_CHAIN_CONTRACT.md';
const SDK = 'rodeo-sdk/README.md';
// The first order: the motherboard and six daughterboards were uploaded to JLC
// on 2026-10-16, five of each, and are not ordered yet. One record covers all seven.
const FAB = 'hardware/rodeo-motherboard/docs/rev-b.md:3-6';

export const boards: Board[] = [
  {
    slug: 'rodeo-motherboard',
    name: 'rodeo-motherboard',
    kind: 'host',
    status: 'designed',
    rev: 'B',
    outline: { w: 90, h: 112 },
    summary:
      'The host board. A Daisy Seed, two codecs, ten general-purpose I/O connectors and the satellite chain.',
    body: 'Rev B gives the motherboard one job: carry the Daisy, the audio, and the connectors everything else plugs into. The two on-board converters that defined Rev A are gone — their pins went to a second codec, and the analog bank they provided became an optional expander board. Controls no longer compete for motherboard channels, because the satellite chain grows by adding satellites.',
    specs: [
      { label: 'audio', value: '4 in / 4 out, two codecs', evidence: `${R}:328` },
      { label: 'general-purpose I/O', value: '10 connectors, 8 analog-capable', evidence: `${R}:81` },
      { label: 'control growth', value: 'satellite chain on USART3', evidence: `${R}:131` },
      { label: 'I²C', value: '4 taps, pull-ups on board', evidence: `${R}:242` },
      { label: 'storage', value: 'internal SD, 1-bit SDMMC', evidence: `${R}:260` },
      { label: 'USB', value: 'data via the power board', evidence: `${R}:304` },
      { label: 'MIDI', value: 'on a daughterboard', evidence: `${R}:430` },
      { label: 'expansion', value: 'SPI header with chip-select', evidence: `${R}:100` },
    ],
    caveats: [
      {
        status: 'designed',
        text: 'Designed and ready to order: the schematic, layout and routing are done and the board is DRC-clean. Uploaded to JLC on 2026-10-16, not yet ordered; no Rev-B motherboard has been built or bench-tested.',
        evidence: FAB,
      },
      {
        status: 'not-planned',
        text: 'No on-board ADCs. The two ADS114S08s, their 24 pot connectors and the pull-downs are removed; that bank becomes an optional expander on the SPI header.',
        evidence: `${R}:606-610`,
      },
    ],
  },

  {
    slug: 'rodeo-pot-bank-4',
    name: 'rodeo-pot-bank-4',
    kind: 'satellite',
    status: 'designed',
    rev: 'B',
    outline: { w: 20, h: 100 },
    summary: 'Four 9 mm pots on 25 mm centres, read by one CH32V003 on the chain.',
    body: 'The first Rev B satellite. Four pots in a row, digitised on the board by a cent-scale microcontroller and passed along the chain as a frame — so the cost of another four controls is another satellite, not another motherboard. The pots mount through the panel and the board hangs off their bushings, with every other part surface-mounted on the underside so assembly stays single-sided.',
    specs: [
      { label: 'controls', value: '4 × 9 mm pot, 25 mm centres', evidence: 'hardware/rodeo-pot-bank-4/README.md:3' },
      { label: 'outline', value: '20 × 100 mm', evidence: 'hardware/rodeo-pot-bank-4/README.md:19' },
      { label: 'brain', value: 'CH32V003F4P6', evidence: `${CHAIN}:89` },
      { label: 'chain link', value: 'JST SH 5-pin, in and out', evidence: `${CHAIN}:32-33` },
      { label: 'chain rate', value: '1 Mbaud', evidence: `${CHAIN}:51` },
    ],
    caveats: [
      {
        status: 'designed',
        text: 'Drawn and routed on 2026-10-14; uploaded to JLC on 2026-10-16, not yet ordered. Nothing here has been measured on hardware.',
        evidence: FAB,
      },
    ],
  },

  {
    slug: 'rodeo-encoder-bank-4',
    name: 'rodeo-encoder-bank-4',
    kind: 'satellite',
    status: 'designed',
    rev: 'B',
    outline: { w: 20, h: 100 },
    summary: 'Four 12 mm encoders with push switches, built to sit beside the pot bank.',
    body: 'The second satellite, deliberately the same shape as the first so the two line up on one panel with their knobs at the same height. The four push switches share a single analog pin through a resistor ladder, because the microcontroller runs out of pins before it runs out of capability — the kind of trade that keeps a satellite under the cost that makes the chain worth having.',
    specs: [
      { label: 'controls', value: '4 × 12 mm encoder with switch', evidence: 'hardware/rodeo-encoder-bank-4/README.md:3' },
      { label: 'outline', value: '20 × 100 mm, matching the pot bank', evidence: 'hardware/rodeo-encoder-bank-4/README.md:20' },
      { label: 'brain', value: 'CH32V003F4P6', evidence: `${CHAIN}:89` },
      { label: 'chain slots', value: 'board kind 6, four encoder slots', evidence: `${CHAIN}:128` },
    ],
    caveats: [
      {
        status: 'designed',
        text: 'Specified and drawn on 2026-10-14; uploaded to JLC on 2026-10-16, not yet ordered. Not yet built or tested.',
        evidence: FAB,
      },
    ],
  },

  {
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
      { label: 'connector', value: '3-pin JST SH: GND, wiper, supply', evidence: `${R}:574-578` },
      { label: 'Rev B fit', value: 'plugs into any analog-capable I/O connector', evidence: `${R}:574-578` },
      { label: 'electronics', value: 'passive — one RC filter', evidence: `${SDK}:125-127` },
    ],
  },

  {
    slug: 'rodeo-audio-3.5mm',
    name: 'rodeo-audio-3.5mm',
    kind: 'daughterboard',
    status: 'designed',
    rev: 'B',
    outline: { w: 60, h: 30 },
    summary: 'Four stereo 3.5 mm jacks carrying the motherboard’s eight audio signals.',
    body: 'Rev B has twice the audio of Rev A, and the jacks move off the motherboard onto a board that bolts to the rear rail. It is passive apart from protection: a series resistor and a clamp on every input, an ESD diode on every jack contact. A quarter-inch variant will share the circuit with different jacks.',
    specs: [
      { label: 'jacks', value: '4 × stereo 3.5 mm TRS', evidence: 'hardware/rodeo-audio-3.5mm/README.md:3' },
      { label: 'carries', value: 'the 8 audio signals on the 2×10 ribbon', evidence: 'hardware/rodeo-audio-3.5mm/README.md:3-4' },
      { label: 'protection', value: 'series R and clamp per input, ESD per contact', evidence: 'hardware/rodeo-audio-3.5mm/README.md:4-5' },
    ],
    caveats: [
      {
        status: 'designed',
        text: 'Drawn and laid out on 2026-10-14; uploaded to JLC on 2026-10-16, not yet ordered. Not yet built or tested.',
        evidence: FAB,
      },
    ],
  },

  {
    slug: 'rodeo-midi',
    name: 'rodeo-midi',
    kind: 'daughterboard',
    status: 'designed',
    rev: 'B',
    outline: { w: 40, h: 25 },
    summary: 'MIDI in and out on TRS Type A, on the back panel.',
    body: 'MIDI leaves the motherboard in Rev B and becomes two jacks on the rear rail. The active parts stay behind — the optocoupler, its resistor and diode, and the output resistors all remain on the motherboard — so this board carries the jacks, one connector and ESD protection on the output, and nothing else.',
    specs: [
      { label: 'jacks', value: '2 × 3.5 mm TRS Type A, in and out', evidence: 'hardware/rodeo-midi/README.md:3' },
      { label: 'to the motherboard', value: 'JST SH 5-pin, pin for pin with J6', evidence: 'hardware/rodeo-midi/README.md:13-16' },
      { label: 'active parts', value: 'none — optocoupler stays on the motherboard', evidence: 'hardware/rodeo-midi/README.md:4-7' },
    ],
    caveats: [
      {
        status: 'designed',
        text: 'Drawn and routed on 2026-10-15; uploaded to JLC on 2026-10-16, not yet ordered. Not yet built or tested.',
        evidence: FAB,
      },
    ],
  },

  {
    slug: 'rodeo-power-usbc',
    name: 'rodeo-power-usbc',
    kind: 'daughterboard',
    status: 'designed',
    rev: 'B',
    outline: { w: 40, h: 20 },
    summary: 'The default power input, and the board that carries USB data to the Seed.',
    body: 'A USB-C jack behind the panel, cabled to the motherboard. It is why Rev B has USB at all: the Seed’s data lines run through this board rather than dead-ending inside the case as they did on Rev A. That is what should bring USB-MIDI, USB audio and DFU flashing to the panel port — should, because no Rev B board has been powered on yet.',
    specs: [
      { label: 'input', value: 'USB-C, 5 V sink, no USB-PD', evidence: 'hardware/rodeo-power-usbc/README.md:22' },
      { label: 'also carries', value: 'USB data for the Seed, and status LEDs', evidence: 'hardware/rodeo-power-usbc/README.md:4-5' },
      { label: 'outline', value: '40 × 20 mm on the rear rail', evidence: 'hardware/rodeo-power-usbc/README.md:8-10' },
    ],
    caveats: [
      {
        status: 'designed',
        text: 'Drawn and routed on 2026-10-12; uploaded to JLC on 2026-10-16, not yet ordered. Not yet built or tested.',
        evidence: FAB,
      },
    ],
  },

  {
    slug: 'rodeo-power-barrel',
    name: 'rodeo-power-barrel',
    kind: 'daughterboard',
    status: 'designed',
    rev: 'B',
    outline: { w: 60, h: 40 },
    summary: 'The USB-C board plus a barrel jack and a separate 5 V output for LED strips.',
    body: 'For builds that need more current than a USB port will give. The barrel jack takes 7–20 V — a 9 V or 12 V adapter in practice — and wins automatically when it is plugged in, while USB stays connected for data and takes over again if the adapter is pulled. A second regulator feeds an auxiliary 5 V output for LED strips, kept off the motherboard’s supply on purpose.',
    specs: [
      { label: 'adapter', value: '5.5 × 2.1 mm, centre positive, 7–20 V', evidence: 'hardware/rodeo-power-barrel/README.md:21' },
      { label: 'switchover', value: 'barrel takes priority, USB is the fallback', evidence: 'hardware/rodeo-power-barrel/README.md:4-6' },
      { label: 'aux output', value: 'separate 5 V rail for LED strips', evidence: 'hardware/rodeo-power-barrel/README.md:3-4' },
    ],
    caveats: [
      {
        status: 'designed',
        text: 'Drawn and routed on 2026-10-12; uploaded to JLC on 2026-10-16, not yet ordered. Not yet built or tested.',
        evidence: FAB,
      },
    ],
  },

  {
    slug: 'rodeo-adc-expander',
    name: 'rodeo-adc-expander',
    kind: 'daughterboard',
    status: 'planned',
    rev: 'B',
    summary: 'Where Rev A’s 24 high-resolution analog inputs went: an optional board on the SPI header.',
    body: 'Rev A put two 16-bit converters on every motherboard whether a build needed them or not, at around nineteen dollars a board. The satellite chain covers ordinary knobs, so the converters become optional: a board on the SPI expansion header, for builds that genuinely need ratiometric 16-bit inputs rather than the Seed’s own ADC.',
    specs: [
      { label: 'converter', value: 'one ADS114S08 on the SPI header', evidence: `${R}:218` },
      { label: 'why optional', value: 'satellites cover ordinary controls; ~$19 a board saved', evidence: `${R}:218` },
    ],
    caveats: [
      {
        status: 'planned',
        text: 'Specified, not drawn. The part is chosen and the circuit is carried over from the Rev-A review, but no schematic or layout exists.',
        evidence: `${R}:218`,
      },
    ],
  },
];

export const retired: Pick<Board, 'slug' | 'name' | 'retiredReason'>[] = [
  {
    slug: 'rodeo-encoder-hub',
    name: 'rodeo-encoder-hub',
    retiredReason:
      'An STM32 that read nine encoders and served them over I²C. Silicon-verified and genuinely good, and replaced anyway: the satellite chain does the same job without a central hop, and a satellite’s position in the chain is its address, so nothing has to be configured.',
  },
  {
    slug: 'rodeo-encoder',
    name: 'rodeo-encoder',
    retiredReason:
      'A single encoder on a hub port. It went with the hub — four encoders now arrive together on rodeo-encoder-bank-4 for less than four of these cost.',
  },
  {
    slug: 'rodeo-button',
    name: 'rodeo-button',
    retiredReason:
      'A two-pin button board. Rev B’s I/O connectors are three-pin, so these no longer plug in directly, and buttons move onto the satellite chain.',
  },
  {
    slug: 'rodeo-rotary-switch',
    name: 'rodeo-rotary-switch',
    retiredReason:
      'A resistor-ladder selector reading as one analog voltage. Dropped once encoders covered the same job with fewer parts and no channel cost.',
  },
  {
    slug: 'rodeo-encoder-bank',
    name: 'rodeo-encoder-bank',
    retiredReason:
      'A four-gang RP2040 encoder strip. An early attempt at what rodeo-encoder-bank-4 now does on a cent-scale part.',
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
  rev: 'A' as Rev,

  /** Proven on the fabricated Rev-A boards. Past tense on purpose. */
  proven: [
    { label: 'analog in', value: '24 channels, 16-bit', evidence: `${MAP}:76` },
    {
      label: 'conversion path',
      value: 'bench-confirmed on silicon, 2026-08-14',
      evidence: `${SDK}:125-127`,
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
      evidence: `${SDK}:129-130`,
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
      evidence: `${R}:218`,
    },
    {
      area: 'Audio',
      revA: 'one codec, in and out',
      revB: 'a second codec on the pins the converters vacated — four in, four out',
      why: 'The freed pins were worth more as audio than as analog inputs that satellites could supply instead.',
      evidence: `${R}:328`,
    },
    {
      area: 'Growing a panel',
      revA: 'one cable per control, back to a motherboard channel or an I²C hub',
      revB: 'satellites chained together, each reading its own controls',
      why: 'Controls stopped being bounded by the motherboard. Position in the chain is the address, so there is nothing to configure and no respin when a panel grows.',
      evidence: `${R}:131`,
    },
    {
      area: 'USB',
      revA: 'power only — the data lines went nowhere',
      revB: 'data through the power board: USB-MIDI, USB audio and firmware flashing',
      why: 'The most-checked spec on the Rev-A board, and the one most worth fixing.',
      evidence: `${R}:304`,
    },
    {
      area: 'Jacks and MIDI',
      revA: 'on the motherboard',
      revB: 'daughterboards on a rear rail',
      why: 'A smaller host board, and connectors a build does not need are connectors it does not pay for.',
      evidence: `${R}:430`,
    },
  ] satisfies Change[],

  shots: [
    {
      src: motherboardImg,
      alt: 'Photograph of the assembled Rev-A Rodeo motherboard: a black PCB with the Daisy Seed footprint, two banks of analog connectors, a row of digital connectors and the panel I/O along one edge.',
      media: 'photo' as const,
      caption: 'rodeo motherboard, assembled',
      rev: 'A' as Rev,
    },
    {
      src: motherboardBackImg,
      alt: 'Photograph of the back of the Rev-A motherboard, showing the ground pour, the board name in silkscreen and the mounting holes.',
      media: 'photo' as const,
      caption: 'the same board, reverse',
      rev: 'A' as Rev,
    },
    {
      src: motherboardIoImg,
      alt: 'Close-up photograph of the Rev-A motherboard’s panel I/O: the MIDI DIN connector, the 3.5 mm audio jacks and the USB-C connector along the board edge.',
      media: 'photo' as const,
      caption: 'panel I/O — the USB-C here is power only',
      rev: 'A' as Rev,
    },
    {
      src: motherboardDaisyImg,
      alt: 'Close-up photograph of the Rev-A motherboard’s Daisy Seed footprint: two rows of plated through-holes with the UART, I2C and SPI headers above them and the indicator LEDs beside.',
      media: 'photo' as const,
      caption: 'the Daisy Seed footprint and bus headers',
      rev: 'A' as Rev,
    },
    {
      src: encoderHubImg,
      alt: 'Photograph of the rodeo-encoder-hub board: a small square PCB with nine three-pin connectors around its edge and an STM32 microcontroller in the centre.',
      media: 'photo' as const,
      caption: 'rodeo-encoder-hub — retired in Rev-B',
      rev: 'A' as Rev,
    },
    {
      src: encoderHubBackImg,
      alt: 'Photograph of the back of the rodeo-encoder-hub board, showing the I²C address jumpers and the ground pour.',
      media: 'photo' as const,
      caption: 'the hub’s reverse, with its address jumpers',
      rev: 'A' as Rev,
    },
  ] satisfies Shot[],
};

export const liveBoards = boards.filter((b) => b.status !== 'retired');
export const host = boards.find((b) => b.kind === 'host')!;
export const satellites = boards.filter((b) => b.kind === 'satellite');
export const daughterboards = boards.filter((b) => b.kind === 'daughterboard');
