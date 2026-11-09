# @dotrodeo/rodeo-host

The computer's side of the host protocol (`common/host.h`, wire format in
[`docs/guides/host.mdx`](../docs/guides/host.mdx)) in TypeScript: a MIDI byte
parser, the message builders, and `RodeoClient`, which says hello, keeps the
state and chain streams alive and hands out snapshots. It knows nothing about
Web MIDI or any particular firmware: give `attach()` a sender and feed
`receive()` the bytes that arrive.

```ts
import { RodeoClient } from '@dotrodeo/rodeo-host';

const rodeo = new RodeoClient();
rodeo.subscribe((s) => console.log(s.status, s.hello?.fw, s.state?.p));
rodeo.attach({ send: (bytes) => (out.send(bytes), true), now: () => performance.now() });
input.onmidimessage = (e) => rodeo.receive(e.data);
rodeo.set('cutoff', 90);
```

A firmware's own commands (`0x20`–`0x3E`) go out with `request(cmd, payload)`;
their replies land in `snapshot.replies[cmd + 0x40]`. `whenReady()` runs after
every hello, which is the moment to ask for a firmware's lists.

`@dotrodeo/rodeo-host/rodeo-demo` is rodeo-demo's part, built that way:
`followRodeoDemo(client)` asks for its preset and sound lists, and `presets()`,
`sounds()`, `demoState()` and `loadPreset()` read and act on them. It is the
pattern for another firmware's module.

The package ships TypeScript source (`.ts` imports, erasable syntax only), so
Node runs it directly and Vite and Metro compile it; there is no build step.
Used by `tools/rodeo-console` through a `file:` dependency.

```sh
npm install
npm test          # node --test, no browser
npm run typecheck
```
