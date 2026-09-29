// @ts-check
import { defineConfig } from 'astro/config';
import sitemap from '@astrojs/sitemap';
import tailwindcssPlugin from '@tailwindcss/vite';

// Astro and @tailwindcss/vite currently resolve different copies of Vite's
// types, so the plugin's `this` context does not structurally match. The
// plugin is correct at runtime; only the declarations skew, so the array is
// widened rather than the behaviour changed.
/** @type {any[]} */
const vitePlugins = [tailwindcssPlugin()];

// `site` is load-bearing: canonical URLs, OG cards, the sitemap and (later)
// the RSS feed Buttondown consumes all derive from it.
export default defineConfig({
  site: 'https://rodeooh.rodeo',
  output: 'static',
  trailingSlash: 'always',
  build: { format: 'directory' },
  integrations: [sitemap()],
  vite: { plugins: vitePlugins },
});
