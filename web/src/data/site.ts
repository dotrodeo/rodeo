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
    { href: '/sdk/', label: 'rodeo-sdk', pending: true },
    { href: '/log/', label: 'build log', pending: true },
    { href: '/docs/', label: 'docs', pending: true },
  ],
};

/** Board revisions. Everything fabbed so far is Rev-A. */
export type Rev = 'A' | 'B' | 'A+B';

/** Claim status — see web/docs/WEB_CLAIMS_AUDIT.md. */
export type Status = 'shipped' | 'in-progress' | 'planned' | 'not-planned' | 'retired';

export const statusLabel: Record<Status, string> = {
  shipped: 'Verified',
  'in-progress': 'In bring-up',
  planned: 'Planned',
  'not-planned': 'Not planned',
  retired: 'Retired',
};
