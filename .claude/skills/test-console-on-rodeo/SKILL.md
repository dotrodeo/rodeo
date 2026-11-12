---
name: test-console-on-rodeo
description: Run the generic rodeo-console or the rodeo-demo editor against the real Rodeo in Chrome and verify a change end to end (Web MIDI with SysEx, notes actually sent, no console errors). Use after changing tools/rodeo-console, instruments/rodeo-demo/editor or rodeo-sdk/host-js.
---

# Test the console on the Rodeo

Two web apps, one code path: `tools/rodeo-console` (generic, any SDK firmware) and
`instruments/rodeo-demo/editor` (embeds the console with a rodeo-demo Profile, takes it
by `file:` dependency, so a console change shows up in the editor without a rebuild).

## 1. Tests, types, builds first

```bash
cd tools/rodeo-console && npx tsc --noEmit -p . && node --test test.ts && npx vite build
cd instruments/rodeo-demo/editor && npx tsc --noEmit -p . && node --test test.ts src/*.test.ts && npx vite build
```

Changed `rodeo-sdk/host-js`? Its tests too: `node --test` there.

## 2. Servers: only ports 5173 and 5174

Chrome grants Web MIDI SysEx per origin, and Derek has allowed `localhost:5173` and
`localhost:5174`. Any other port stops at a permission prompt and `requestMIDIAccess`
hangs. Convention: console on 5173, editor on 5174.

```bash
cd tools/rodeo-console && npx vite --port 5173 --strictPort                 # background
cd instruments/rodeo-demo/editor && npx vite --port 5174 --strictPort       # background
```

Run them in the background and leave them up if Derek is going to test; say which
URL. Stop them when he asks.

## 3. Drive it in Claude in Chrome

Use Derek's Chrome (it holds the MIDI permission), in a new tab you close afterwards.
Connect in the page and check the port, in one `javascript_tool` call:

```js
document.getElementById('live-connect').click();
await new Promise(r => setTimeout(r, 1500));
document.getElementById('live-port').selectedOptions[0]?.textContent   // "Rodeo Demo"
```

**See what is actually sent:** wrap `MIDIOutput.prototype.send`, then restore it:

```js
const log = []; const orig = MIDIOutput.prototype.send;
MIDIOutput.prototype.send = function (b, t) { if ((b[0] & 0xf0) === 0x90) log.push(b[1]); return orig.call(this, b, t); };
// ... act ...
MIDIOutput.prototype.send = orig; log
```

**Fake the computer keyboard on `document.body`, never `window`:** the handlers call
`e.target.matches(...)`, which `window` lacks, so events on `window` throw and play
nothing.

```js
const k = (type, key) => document.body.dispatchEvent(new KeyboardEvent(type, { key, bubbles: true }));
```

Controls have ids (`live-play`, `live-bpm`, `live-bars`, `live-arp`, `live-arp-rate`,
`live-latch`, `live-panic`, ...); set a `<select>`'s value then dispatch `change`. For
the canvas piano roll use real pointer drags with the `computer` tool and `zoom` to look.

## 4. Before reporting

- `read_console_messages` with `onlyErrors`; tell apart errors your own synthetic events
  caused from real ones.
- Put the page back: arp off, transport stopped, **panic** (the page holds notes on real
  hardware; a lost note-off drones until power-cycle).
- Screenshot what changed; close your tab.
- If the Rodeo is not seen at all, it is a board problem, not a page problem: use the
  `flash-rodeo` skill's "When the board does not show up".
