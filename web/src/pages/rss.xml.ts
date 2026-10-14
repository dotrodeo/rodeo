// The feed is load-bearing beyond RSS readers: Buttondown's RSS-to-email will
// consume it, so a malformed feed or a wrong `site` means broken newsletters,
// not just a broken feed. `site` comes from astro.config.mjs.
import rss from '@astrojs/rss';
import type { APIContext } from 'astro';
import { getPosts, urlOf } from '../lib/posts';
import { site } from '../data/site';

export async function GET(context: APIContext) {
  const posts = await getPosts();

  return rss({
    title: `${site.name} — build log`,
    description:
      'Designing an open-source synthesizer platform in the open, including the parts that went wrong.',
    site: context.site ?? site.url,
    items: posts.map((post) => ({
      title: post.data.title,
      pubDate: post.data.date,
      description: post.data.summary,
      link: urlOf(post),
      categories: [...post.data.tags],
    })),
    customData: '<language>en-gb</language>',
  });
}
