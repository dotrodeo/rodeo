// The board catalogue — the single source for /ecosystem/ and every board page.
//
// RULES (web/docs/WEB_CLAIMS_AUDIT.md):
//   - Every `spec` and `caveat` carries `evidence`: a repo-relative path, or
//     path:line. If you cannot cite it, it does not go on the site.
//   - `status: 'shipped'` means proven on real hardware, not merely designed.
//   - Every board carries `rev`. Everything fabbed so far is Rev-A, and the
//     board images are photographs/renders of Rev-A hardware.
//   - Retired boards stay listed, marked retired. A platform with an
//     inaccurate parts list loses trust instantly.

import type { ImageMetadata } from 'astro';
import type { Rev, Status } from './site';

import motherboardImg from '../assets/hardware/rodeo-motherboard.png';
import encoderHubImg from '../assets/hardware/rodeo-encoder-hub.png';
import potentiometerImg from '../assets/hardware/rodeo-potentiometer.png';
import encoderImg from '../assets/hardware/rodeo-encoder.png';
import buttonImg from '../assets/hardware/rodeo-button.png';

export interface Spec {
  label: string;
  value: string;
  evidence: string;
}

export interface Caveat {
  /** Shown as the badge. 'not-planned' reads as a hard limit, not an omission. */
  status: Extract<Status, 'not-planned' | 'in-progress' | 'planned'>;
  text: string;
  evidence: string;
}

export interface Board {
  slug: string;
  name: string;
  kind: 'host' | 'satellite';
  status: Status;
  rev: Rev;
  /** One line for the catalogue card. */
  summary: string;
  /** A paragraph for the board page. */
  body: string;
  image?: ImageMetadata;
  /** Alt text. Required — the figure component will not render without it. */
  imageAlt?: string;
  specs: Spec[];
  caveats?: Caveat[];
  /** Why it was cut. Retired boards only. */
  retiredReason?: string;
}

const MAP = 'rodeo-motherboard/docs/hardware-map.md';
const HUB = 'rodeo-motherboard/docs/RODEO_HUB_I2C_CONTRACT.md';
const FW = 'rodeo-motherboard/firmware/README.md';
const HUBFW = 'rodeo-encoder-hub/firmware/README.md';
const CMAP = 'rodeo-001/docs/RODEO_CONTROL_MAP.md';

export const boards: Board[] = [
  {
    slug: 'rodeo-motherboard',
    name: 'rodeo-motherboard',
    kind: 'host',
    status: 'shipped',
    rev: 'A',
    summary:
      'The host. A Daisy Seed carrier that turns the Seed’s pins into a wired control surface.',
    body: 'Two 12-channel 16-bit delta-sigma converters on SPI give twenty-four analog inputs, with twelve raw digital inputs alongside them and an I²C bus for smart modules. Controls are not board-mounted: every input is a JST connector, so the panel is a tiling of satellite boards rather than one co-designed PCB.',
    image: motherboardImg,
    imageAlt:
      'The Rodeo motherboard seen from above: a black four-layer board with two banks of twelve analog JST connectors, a bank of twelve digital connectors, three bus headers, and a Daisy Seed footprint with gold plated through-holes.',
    specs: [
      { label: 'analog in', value: '24 × 16-bit', evidence: `${MAP}:76` },
      { label: 'converters', value: '2 × ADS114S08 on SPI1', evidence: `${MAP}:76` },
      { label: 'digital in', value: '12 × GPIO', evidence: `${MAP}:114` },
      { label: 'buses', value: 'I²C · SPI · UART', evidence: `${MAP}:173` },
      { label: 'audio', value: 'line in + out, 3.5 mm', evidence: `${MAP}:143` },
      { label: 'MIDI', value: 'TRS in + out', evidence: `${MAP}:165-166` },
      { label: 'host', value: 'Daisy Seed3, 40-pin', evidence: `${MAP}:15` },
      { label: 'mounting', value: '95 × 75 mm, 4 × M2', evidence: 'rodeo-motherboard/kicad/rodeo-motherboard.kicad_pcb' },
    ],
    caveats: [
      {
        status: 'not-planned',
        text: 'The panel USB-C is power only. The Seed’s USB data pins terminate at pins 37/36 and are not routed to the connector, so there is no USB-MIDI and no USB audio through the panel, and no DFU flashing over it. Flash through the Seed’s own port or SWD.',
        evidence: `${MAP}:213-221`,
      },
      {
        status: 'not-planned',
        text: 'MIDI and the J44 UART header share USART1, so only one of the two can be used at a time.',
        evidence: `${MAP}:182`,
      },
      {
        status: 'in-progress',
        text: 'The ADC conversion path is bench-proven, but absolute-voltage accuracy is still unverified pending a jumper/pot test.',
        evidence: `${FW}:90-92`,
      },
    ],
  },
  {
    slug: 'rodeo-encoder-hub',
    name: 'rodeo-encoder-hub',
    kind: 'satellite',
    status: 'shipped',
    rev: 'A',
    summary:
      'Reads nine encoders and answers the host as a single I²C target. Four addresses per bus.',
    body: 'Encoders are expensive in pins and cheap in bandwidth, so they do not go to the host directly. An STM32C031 polls nine of them at roughly 1 kHz, does the quadrature and the debouncing, and serves a RAM shadow over I²C. Two cuttable jumpers set the address, so four hubs — thirty-six encoders — share two wires.',
    image: encoderHubImg,
    imageAlt:
      'The Rodeo encoder hub seen from above: a square black board with nine numbered encoder slots ringing a central microcontroller, and JST connectors along all four edges.',
    specs: [
      { label: 'MCU', value: 'STM32C031C6', evidence: `${HUBFW}:177-191` },
      { label: 'slots', value: '9', evidence: `${HUB}:39` },
      { label: 'address', value: '0x30–0x33', evidence: `${HUB}:35` },
      { label: 'bus speed', value: '100 kHz at boot', evidence: `${HUB}:36` },
      { label: 'host link', value: 'Qwiic / STEMMA QT', evidence: `${HUB}:34` },
      { label: 'slot link', value: 'JST-SH 4-pin', evidence: `${CMAP}:103` },
      { label: 'poll rate', value: '~1 kHz, RAM shadow', evidence: `${HUB}:38` },
      { label: 'pull-ups', value: '2 × 2.2 kΩ on-board', evidence: `${HUB}:37` },
    ],
    caveats: [
      {
        status: 'in-progress',
        text: 'On Rev-A the hub is the bus anchor — it carries the only pull-ups, so the I²C bus needs at least one hub present. Rev-B moves them to the motherboard.',
        evidence: `${HUB}:37`,
      },
      {
        status: 'planned',
        text: 'The bus runs at 100 kHz and is proven there. The 400 kHz promotion is not done: the timing value has not been generated and setup-hold has never been checked on a logic analyser.',
        evidence: `${HUBFW}:179-183`,
      },
    ],
  },
  {
    slug: 'rodeo-potentiometer',
    name: 'rodeo-potentiometer',
    kind: 'satellite',
    status: 'shipped',
    rev: 'A',
    summary: 'One pot, a supply filter, three pins to an ADC channel. Entirely passive.',
    body: 'An Alps RK09K with a 1 kΩ + 100 nF supply filter on the top of the track. Nothing to configure and nothing to drive — it presents a voltage and the host converts it.',
    image: potentiometerImg,
    imageAlt:
      'The Rodeo potentiometer satellite seen from above: a small black board carrying one Alps potentiometer and a 3-pin JST connector.',
    specs: [
      { label: 'control', value: 'Alps RK09K', evidence: 'rodeo-potentiometer/jlcpcb/production_files/BOM-rodeo-potentiometer.csv' },
      { label: 'outline', value: '25 × 30 mm', evidence: 'rodeo-potentiometer/rodeo-potentiometer.kicad_pcb' },
      { label: 'link', value: 'JST-SH 3-pin', evidence: `${CMAP}:76` },
      { label: 'cable', value: 'reversed — see the harness table', evidence: `${CMAP}:100` },
    ],
    caveats: [
      {
        status: 'not-planned',
        text: 'The analog link needs a reversed cable: the satellite mirrors the host’s pin order. Built straight, nothing is damaged — everything on the board is passive — but every pot reads backwards, which looks exactly like a firmware sign bug.',
        evidence: `${CMAP}:100,109-111`,
      },
    ],
  },
  {
    slug: 'rodeo-encoder',
    name: 'rodeo-encoder',
    kind: 'satellite',
    status: 'shipped',
    rev: 'A',
    summary: 'A detented EC11 with a push switch, wired to one hub slot.',
    body: 'Deliberately dumb: no microcontroller, no quadrature decoding, four pins to a hub slot. All the intelligence lives in the hub, which is what keeps the satellite cheap enough to put twenty of them on a panel.',
    image: encoderImg,
    imageAlt:
      'The Rodeo encoder satellite seen from above: a small square black board carrying one EC11 rotary encoder and a 4-pin JST connector.',
    specs: [
      { label: 'control', value: 'EC11, detented, with switch', evidence: `${CMAP}:78` },
      { label: 'outline', value: '25 × 25 mm', evidence: 'rodeo-encoder/rodeo-encoder.kicad_pcb' },
      { label: 'link', value: 'JST-SH 4-pin (A/B/S/GND)', evidence: `${CMAP}:78` },
      { label: 'goes to', value: 'one rodeo-encoder-hub slot', evidence: `${HUB}:39` },
    ],
  },
  {
    slug: 'rodeo-button',
    name: 'rodeo-button',
    kind: 'satellite',
    status: 'in-progress',
    rev: 'A',
    summary: 'A momentary switch straight onto a digital input.',
    body: 'The simplest satellite in the set: a switch, a pull-up and two pins. The interface is verified against the host; the switch footprint itself is still being settled, which is why this one is not marked verified.',
    image: buttonImg,
    imageAlt:
      'The Rodeo button satellite seen from above: a small square black board carrying one momentary switch and a 2-pin JST connector.',
    specs: [
      { label: 'control', value: 'momentary switch', evidence: `${CMAP}:77` },
      { label: 'outline', value: '25 × 25 mm', evidence: 'rodeo-button/rodeo-button.kicad_pcb' },
      { label: 'link', value: 'JST-SH 2-pin, straight cable', evidence: `${CMAP}:101` },
      { label: 'goes to', value: 'one motherboard digital input', evidence: `${MAP}:114` },
    ],
    caveats: [
      {
        status: 'in-progress',
        text: 'The switch footprint is not yet assigned, so the bushing diameter — and therefore the panel cutout — is not final.',
        evidence: 'rodeo-001/docs/RODEO_ENCLOSURE_SPEC.md',
      },
    ],
  },
];

export const retired: Pick<Board, 'slug' | 'name' | 'retiredReason'>[] = [
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
      'A four-gang RP2040 encoder strip. Replaced by the hub-and-satellite split, which scales better across a panel.',
  },
];

export const liveBoards = boards.filter((b) => b.status !== 'retired');
export const host = boards.find((b) => b.kind === 'host')!;
export const satellites = boards.filter((b) => b.kind === 'satellite');
