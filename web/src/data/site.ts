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
    { href: '/docs/', label: 'docs', pending: true },
  ],
};

/** Board revisions. Everything fabbed so far is Rev-A. */
export type Rev = 'A' | 'B' | 'A+B';

/**
 * Claim status — see web/docs/WEB_CLAIMS_AUDIT.md.
 *
 * `designed` was added for Rev-B, because the existing words could not say
 * where those boards actually are. `shipped` means proven on hardware and
 * would be a lie; `planned` means specified-not-drawn and badly undersells a
 * board that is routed with fab outputs waiting. The gap between "we have
 * drawn this" and "we have held this" is the single most important thing a
 * crowdfunding page has to get right, so it gets its own word.
 */
export type Status =
  | 'shipped'
  | 'in-progress'
  | 'designed'
  | 'planned'
  | 'not-planned'
  | 'retired';

export const statusLabel: Record<Status, string> = {
  shipped: 'Verified',
  'in-progress': 'In progress',
  designed: 'Designed',
  planned: 'Planned',
  'not-planned': 'Not planned',
  retired: 'Retired',
};
