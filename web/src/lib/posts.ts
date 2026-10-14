// One place that knows how posts are filtered and ordered. Pages never query
// the collection directly, so drafts cannot leak into a page, the feed or the
// sitemap by someone forgetting the filter in one of the three.
import { getCollection, type CollectionEntry } from 'astro:content';

export type Post = CollectionEntry<'log'>;

/** `01-choosing-the-brain` → `choosing-the-brain`. */
export const slugOf = (id: string) => id.replace(/^\d+-/, '');

/** `01-choosing-the-brain` → 1. The running order. */
export const seqOf = (id: string) => Number.parseInt(id.match(/^(\d+)-/)?.[1] ?? '0', 10);

export const urlOf = (post: Post) => `/log/${slugOf(post.id)}/`;

/**
 * Published posts, newest first. Drafts are visible in `dev` and excluded from
 * every production surface.
 */
export async function getPosts(): Promise<Post[]> {
  const posts = await getCollection('log', ({ data }) => import.meta.env.DEV || !data.draft);
  return posts.sort((a, b) => seqOf(b.id) - seqOf(a.id));
}

export async function getTags(): Promise<{ tag: string; count: number }[]> {
  const posts = await getPosts();
  const counts = new Map<string, number>();
  for (const p of posts) for (const t of p.data.tags) counts.set(t, (counts.get(t) ?? 0) + 1);
  return [...counts.entries()]
    .map(([tag, count]) => ({ tag, count }))
    .sort((a, b) => b.count - a.count || a.tag.localeCompare(b.tag));
}

// `date: 2026-10-04` in frontmatter parses as UTC midnight. Formatting that in
// any negative-offset timezone renders the day before, so both formatters are
// pinned to UTC — otherwise a post's date changes depending on who builds it.
export const formatDate = (d: Date) =>
  d.toLocaleDateString('en-GB', {
    year: 'numeric',
    month: 'long',
    day: 'numeric',
    timeZone: 'UTC',
  });

export const isoDate = (d: Date) => d.toISOString().slice(0, 10);
