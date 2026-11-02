// Site-wide constants. The public brand is "Rodeo"; rodeooh.rodeo is a pun on
// "Romeo, oh Romeo" and does no brand work — see planning/WEB_BRAND.md.

export interface NavItem {
  href: string;
  label: string;
  /** Shown but not linked — the section is not built yet. */
  pending?: boolean;
}

export const site: {
  name: string;
  url: string;
  tagline: string;
  description: string;
  nav: NavItem[];
} = {
  name: 'Rodeo',
  url: 'https://rodeooh.rodeo',
  tagline: 'Open-source synth platform',
  description:
    'A carrier board for the Electrosmith Daisy and a chain of control satellites. Build an instrument’s control surface without drawing a PCB.',
  // Lowercase, mono, per the mockup. `pending` renders unlinked.
  nav: [
    { href: '/', label: 'home' },
    { href: '/ecosystem/', label: 'boards' },
    { href: '/sdk/', label: 'rodeo-sdk' },
    { href: '/log/', label: 'build log' },
    { href: '/prototype/', label: 'prototype' },
    { href: '/docs/', label: 'docs' },
  ],
};

/** Board revisions. Everything fabbed so far is Rev-A. */
export type Rev = 'A' | 'B' | 'A+B';

/**
 * Claim status — see planning/WEB_CLAIMS_AUDIT.md.
 *
 * `designed` and `at-fab` exist because the original words could not say where
 * the Rev-B boards actually are. `shipped` means proven on hardware and would
 * be a lie; `planned` means specified-not-drawn and badly undersells a board
 * whose Gerbers are with a manufacturer.
 *
 * `at-fab` is the stronger of the two and is earned when an order is placed,
 * not when Gerbers are uploaded: the Rev-B boards were uploaded on 2026-10-16 and
 * ordered on 2026-10-18, and only the order moved them to `at-fab`. It still is not
 * `shipped`: the line between "we have had this made" and "we have held this
 * and measured it" is the single most important thing a crowdfunding page has
 * to get right.
 *
 * `built` is that line. The first run arrived on 2026-10-28 and bring-up found
 * real faults on the first day, so "it exists" and "it works" had to stop being
 * the same word. A board is `built` when it is on the bench and `shipped` only
 * once a bring-up test has passed on it.
 */
export type Status =
  | 'shipped'
  | 'in-progress'
  | 'built'
  | 'at-fab'
  | 'designed'
  | 'planned'
  | 'not-planned'
  | 'retired';

export const statusLabel: Record<Status, string> = {
  shipped: 'Verified',
  'in-progress': 'In progress',
  built: 'Built, untested',
  'at-fab': 'At the fab',
  designed: 'Designed',
  planned: 'Planned',
  'not-planned': 'Not planned',
  retired: 'Retired',
};
