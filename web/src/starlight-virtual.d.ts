// Starlight resolves `virtual:starlight/components/*` at build time but ships
// no type declarations for them, so `astro check` fails on a component override
// that composes Starlight's own pieces.
//
// Importing the concrete files (`@astrojs/starlight/components/Search.astro`)
// would type-check, but it bypasses the override chain: if Search is ever
// overridden in astro.config.mjs, the concrete import would silently keep
// rendering the stock one. Declaring the virtual modules keeps the documented
// behaviour and satisfies the checker.

declare module 'virtual:starlight/components/Search' {
  const Component: typeof import('@astrojs/starlight/components/Search.astro').default;
  export default Component;
}

declare module 'virtual:starlight/components/SocialIcons' {
  const Component: typeof import('@astrojs/starlight/components/SocialIcons.astro').default;
  export default Component;
}
