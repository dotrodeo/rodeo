// Site-wide constants. The public brand is "Rodeo"; rodeooh.rodeo is a pun on
// "Romeo, oh Romeo" and does no brand work — see web/docs/WEB_BRAND.md.

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
    'A host board and a family of drag-drop control satellites. Build an instrument’s control surface without designing a PCB.',
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
 * Claim status — see web/docs/WEB_CLAIMS_AUDIT.md.
 *
 * `designed` and `at-fab` exist because the original words could not say where
 * the Rev-B boards actually are. `shipped` means proven on hardware and would
 * be a lie; `planned` means specified-not-drawn and badly undersells a board
 * whose Gerbers are with a manufacturer.
 *
 * `at-fab` is the stronger of the two and is earned when an order is placed,
 * not when Gerbers are uploaded: on 2026-10-16 all seven Rev-B boards were
 * uploaded to JLC but not yet ordered, so they are `designed`. It still is not
 * `shipped`: the line between "we have had this made" and "we have held this
 * and measured it" is the single most important thing a crowdfunding page has
 * to get right.
 */
export type Status =
  | 'shipped'
  | 'in-progress'
  | 'at-fab'
  | 'designed'
  | 'planned'
  | 'not-planned'
  | 'retired';

export const statusLabel: Record<Status, string> = {
  shipped: 'Verified',
  'in-progress': 'In progress',
  'at-fab': 'At the fab',
  designed: 'Designed',
  planned: 'Planned',
  'not-planned': 'Not planned',
  retired: 'Retired',
};
