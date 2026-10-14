// Astro 5 puts this at src/content.config.ts — NOT src/content/config.ts — and
// collections need an explicit loader. Both are easy to get wrong from older docs.
import { defineCollection, z } from 'astro:content';
import { glob } from 'astro/loaders';
import { docsLoader } from '@astrojs/starlight/loaders';
import { docsSchema } from '@astrojs/starlight/schema';

/**
 * Closed tag list. The highest-leverage line in this file: tag routes can be
 * generated without scanning, and `Firmware` / `fw` / `firmware` sprawl fails
 * the build instead of quietly producing three tag pages.
 */
export const LOG_TAGS = [
  'process',
  'pcb',
  'firmware',
  'dsp',
  'panel',
  'enclosure',
  'bring-up',
  'audio',
] as const;

export type LogTag = (typeof LOG_TAGS)[number];

const log = defineCollection({
  // Filenames are `NN-slug.mdx`. The numeric prefix is the running order and
  // is stripped from the URL — it exists so two posts written on the same day
  // still have a defined sequence without inventing publication dates.
  loader: glob({ pattern: '**/[0-9]*.{md,mdx}', base: './src/content/log' }),
  schema: ({ image }) =>
    z
      .object({
        title: z.string().min(10).max(70),
        date: z.coerce.date(),
        updated: z.coerce.date().optional(),
        /** Does triple duty: meta description, OG description, RSS description. */
        summary: z.string().min(40).max(200),
        tags: z.array(z.enum(LOG_TAGS)).min(1),
        hero: image().optional(),
        heroAlt: z.string().optional(),
        draft: z.boolean().default(false),
        /** Spec ids this post touches, cross-checked by check-claims.mjs later. */
        relatedSpecs: z.array(z.string()).optional(),
      })
      .refine((d) => !d.hero || (d.heroAlt && d.heroAlt.trim().length > 0), {
        message: 'heroAlt is required when hero is set',
        path: ['heroAlt'],
      }),
});

// Starlight owns /sdk/ and brings its own collection. It sits alongside the
// log collection rather than replacing the site's own routing.
const docs = defineCollection({ loader: docsLoader(), schema: docsSchema() });

export const collections = { log, docs };
