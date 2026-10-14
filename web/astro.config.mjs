// @ts-check
import { defineConfig } from 'astro/config';
import sitemap from '@astrojs/sitemap';
import mdx from '@astrojs/mdx';
import starlight from '@astrojs/starlight';
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
  integrations: [
    // Starlight owns /sdk/ only. The marketing pages, the board catalogue and
    // the build log keep their own layout; this is a docs section inside the
    // site rather than a second site bolted on.
    starlight({
      title: 'Rodeo',
      description:
        'Driver libraries and bring-up examples for the Rodeo motherboard — the ADC surface, the encoder hubs, and the pure-logic headers.',
      customCss: ['./src/styles/starlight.css'],
      expressiveCode: {
        // Starlight's default syntax theme is a Material-ish purple/orange set
        // that has nothing to do with this palette, and its code surface sits
        // 1.11:1 from the page so blocks never detach. Everything below is
        // legibility and separation, not decoration.
        themes: ['github-dark-default'],
        styleOverrides: {
          borderRadius: '0',
          borderColor: '#3a3a3a',
          codeBackground: '#191919',
          frames: {
            editorActiveTabBackground: '#191919',
            editorTabBarBackground: '#141414',
            terminalBackground: '#191919',
            terminalTitlebarBackground: '#141414',
          },
        },
      },
      // The site has its own 404; Starlight must not claim that route.
      disable404Route: true,
      // Dark only; the toggle is overridden with an empty component.
      components: {
        ThemeSelect: './src/components/starlight/ThemeSelect.astro',
        Header: './src/components/starlight/Header.astro',
        MobileMenuFooter: './src/components/starlight/MobileMenuFooter.astro',
      },
      social: [
        { icon: 'github', label: 'GitHub', href: 'https://github.com/derekdotrodeo/rodeo' },
      ],
      sidebar: [
        {
          label: 'Start here',
          items: [
            { label: 'Overview', slug: 'sdk' },
            { label: 'Install and build', slug: 'sdk/install' },
            { label: 'Your first app', slug: 'sdk/first-app' },
          ],
        },
        {
          label: 'Drivers',
          items: [
            { label: 'Analog inputs', slug: 'sdk/drivers/adc' },
            { label: 'Encoders', slug: 'sdk/drivers/encoders' },
          ],
        },
        {
          label: 'Reference',
          items: [
            { label: 'Pure-logic headers', slug: 'sdk/reference/pure-logic' },
            { label: 'Bring-up examples', slug: 'sdk/reference/examples' },
            { label: 'Versioning', slug: 'sdk/reference/versioning' },
          ],
        },
      ],
    }),
    mdx(),
    sitemap(),
  ],
  vite: { plugins: vitePlugins },
});
