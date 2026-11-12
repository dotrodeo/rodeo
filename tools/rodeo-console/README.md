# rodeo-console

A browser page for any Rodeo on the bench: play it over USB-MIDI, see and set
its parameters, watch its satellite chain, and reboot it for flashing. It works
with any firmware built on the SDK, because everything it shows comes from the
SDK's host protocol
([`rodeo-sdk/docs/guides/host.mdx`](../../rodeo-sdk/docs/guides/host.mdx)): the
firmware's hello names its parameters, and the state and chain reports stream
from there.

A firmware's own page embeds this one and adds what only that firmware knows.
rodeo-demo's is the card editor in
[`instruments/rodeo-demo/editor`](../../instruments/rodeo-demo/editor), which
adds the SD card, its panel's grouping, its encoders' jobs, a preset picker and
a model of its voices, through a `Profile` ([`src/profile.ts`](src/profile.ts)).

A bench tool, not a product: Chrome or Edge. TypeScript built with Vite; the
only thing the page depends on is the SDK's client.

## Running it

```sh
cd tools/rodeo-console
npm install
npm run dev
```

Then open `http://localhost:5173`. `npm run build` type-checks and writes a
static copy to `dist/`, and `npm run preview` serves that.

### From another computer

`npm run dev:lan` (or `npm run preview:lan`) also listens on the network, so the
console can run on one machine and be opened in Chrome or Edge on another. The
Rodeo plugs into **the computer with the browser**: Web MIDI and Web Serial
reach only that machine's USB devices, never the server's. The rodeo-demo editor
works the same way on port 5174; both servers share
[`vite.serve.ts`](vite.serve.ts).

The page has to come over HTTPS. Web MIDI, Web Serial and the File System Access
API work only in a secure context, and `http://localhost` is one but
`http://<server>.local` is not: over plain HTTP from another machine the console
finds no MIDI at all. Give the servers a certificate the other computers trust:

```sh
export RODEO_TLS_CERT=~/.config/rodeo-tls/cert.pem RODEO_TLS_KEY=~/.config/rodeo-tls/key.pem
npm run dev:lan        # https://<server>.local:5173
```

To make that certificate, once, on the server: a small certificate authority
of your own, and a certificate it signs for the server's names and address.
Keep `ca.key` on the server.

```sh
mkdir -p ~/.config/rodeo-tls && cd ~/.config/rodeo-tls && chmod 700 .
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 3650 \
  -keyout ca.key -out ca.crt -subj "/CN=$(hostname) local dev CA" \
  -addext "basicConstraints=critical,CA:TRUE,pathlen:0" -addext "keyUsage=critical,keyCertSign,cRLSign"
openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout key.pem -out server.csr -subj "/CN=$(hostname).lan"
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -days 825 -out cert.pem \
  -extfile <(printf 'subjectAltName=DNS:%s.lan,DNS:%s.local,IP:%s\nextendedKeyUsage=serverAuth\n' \
             "$(hostname)" "$(hostname)" "<the server's LAN address>")
rm server.csr; chmod 600 ca.key key.pem
```

Then, on each computer that opens the page, trust `ca.crt` once (copy it over;
it isn't secret):

* **macOS:** open it in Keychain Access, in the System keychain; double-click
  it, and under Trust set "When using this certificate" to Always Trust.
* **Windows:** double-click it, Install Certificate, Local Machine, and place
  it in "Trusted Root Certification Authorities".
* **Linux:** Chrome reads its own store: Settings, Privacy and security,
  Security, Manage certificates, Authorities, Import.

The certificate lasts 825 days, the most macOS accepts. To renew it, rerun the
last three commands; the authority (`ca.crt`) stays, so nothing changes on the
other computers.

Use the server's `.local` name: every computer resolves it on the local network
by itself. A `.lan` name resolves only on computers that use the router for DNS,
which a custom DNS server, a VPN or Chrome's secure DNS setting quietly prevents.
Vite answers only host names it knows: `*.lan`, `*.local`, `*.home.arpa`, the
server's own name and its IP addresses. `RODEO_ALLOWED_HOSTS` adds more,
comma-separated.

## Using it

**Connect MIDI** asks Chrome for MIDI with SysEx ("control and reprogram MIDI
devices"), picks the first port with "Rodeo" in its name, and says hello. Allow
SysEx and the dot turns cyan once the Rodeo answers; refuse it and the keyboard
still plays, but nothing comes back. A Rodeo that plays notes but never answers
was built with `RODEO_HOST=0`, or before the protocol. Once Chrome has allowed
it, the page connects by itself when it opens and the button goes away; it
never prompts on its own, so a first visit still starts with the button.

The dots: grey not connected, yellow connected, cyan carrying traffic, red
open but wrong. The dot's tooltip says which, since a colour can say that
something is off but not what.

### Playing it

Click or drag across the keys, or type on the `a w s e d f …` row; `z` and `x`
shift an octave, holding Shift is the sustain pedal. The keyboard sends notes,
velocity, pitch bend (scaled to rodeo-demo's ±2 semitones), CC 64, and CC 123
for panic. What a firmware does with them is its own business.

### The arp and the piano roll

`src/seq.ts` holds the arithmetic for both and is tested; it is a copy of
nothing: the Rodeo just hears notes. This is the page playing the instrument.

**The arp** belongs to the keyboard, and its controls sit beside the keyboard's.
Whatever is held is played up, down, up-down, as played, or random, over one to
four octaves, at a rate from 1/4 down to 1/32 (triplets too), with its own gate.
Up-down plays its endpoints once, so a triad gives four steps rather than six.
It has its own clock: it starts with the first key and stops with the last,
whether the roll is playing or not. The only thing it takes from the roll is the
tempo, and when the roll is running the arp's first note waits for the roll's
next boundary at the arp's rate, so the two stay in time. **Latch** keeps it
going after the keys are let go, and the latched notes stay marked on the
keyboard so it is clear what it is chewing on; pressing a fresh key then starts
a new chord. With the arp on, the keyboard feeds it instead of sounding directly.

**The piano roll** covers a piano's 88 keys (A0 to C8), 2 to 8 bars of sixteenths, and opens on C2
to C4 (drag its corner to see more). Click an empty cell to add a note as long
as the last one drawn, and drag to set its length; drag a note to move it in
time and pitch, drag its end to change its length, and click it (or right-click)
to remove it. Clicking a key at the side plays that note. Two notes of one pitch
never overlap: a new one shortens or replaces the one under it. Shortening the
roll keeps the notes past the new end; they just don't play until it's long
enough again. Play (or the space bar), the tempo and swing are the roll's
transport. `src/roll.ts` draws it on a canvas.

Notes go out with Web MIDI timestamps from a lookahead scheduler rather than
straight off a `setTimeout`, which would jitter audibly. Swing shifts a step
without the grid drifting: sixteen steps later is still exactly a bar. Each
note off lands a few ms before its next step, so a note followed by its own
pitch retriggers. A big chord goes out 32 notes a millisecond, since the
firmware's queue drops past 64 unread events and a full column is 88 notes. The
arp looks ahead only 60 ms, against the roll's 140, because it reads the held
keys as it schedules: a chord played one key at a time would otherwise repeat
its first key.

Stopping or hiding the tab lifts every note and sends CC 123 — the page holds notes down on real hardware, and a dropped note-off
means the Rodeo drones until it is power-cycled.

### The parameters

The parameter panel is drawn as the instrument is: a column per bank, a knob
per control. A pot's knob shows where its parameter is; an encoder's has its
tap and its held turn written under its name, and under the knob the values
those set. Drag a knob up or down (Alt for fine), scroll it, or use the arrow
keys; click a pressable encoder for its tap, and Shift-drag it (or Shift-scroll)
for its held turn. Times and frequencies over a wide range turn in ratio, as
the firmware's pots do. A value turns red while a preset or the page holds its
pot (the state's `hold` bit).

What the banks are comes from the profile (`panel` in `src/profile.ts`). Without
one the pots make the columns, four to a bank by pot number. Any parameter no
control turns goes in a row underneath: a switch for an off/on, a menu for a
list, a − + stepper for a short range, a knob for the rest. Everything follows
the Rodeo live through the state stream, and setting a value is the protocol's
`set`.

### The chain

The chain report streams over MIDI with the state: the satellites in ring order,
a break drawn after the one it follows, and the scan health. Pot *k* is the
*k*-th pot slot on the chain in ring order, which is what a parameter's `pot`
in the hello means, so **each knob is drawn beside the parameter it drives**.

That pairing exists to show one thing. Loading a preset sets the parameters,
and each pot keeps its hands off until it is moved about 2% from where it was
(rodeo-demo's `panel.cpp` takeover). So a knob can sit at 70% while cutoff is 41%: the
panel is not broken, the preset still has it. The firmware sets the pot's `hold`
bit, the row goes red and says **preset holds this**, and nothing on the Rodeo
itself can tell you that.

An encoder has no position to compare, so its row pairs the parameter with its
value and shows **the turn that just happened**. The report carries a detent
total counted from power-up, which on its own says nothing — "+14" is not a
state — so the totals are differenced between reports. A profile can say what each
encoder turns; without one it shows the total.


**Reboot for flashing** is the protocol's `dfu`: the Rodeo answers, then
reboots into its bootloader and leaves USB.

### Serial

**Connect serial** is the firmware's log: whatever it prints, the last line
shown under the chain buttons (lines starting `#` are for machines and are
skipped). A profile can add serial commands as buttons there; rodeo-demo's
hand the card over and take it back. What the Rodeo says over MIDI (the
protocol's `said`) lands on the same line.

## Embedding it

```ts
import 'rodeo-console/console.css';
import { mount } from 'rodeo-console/console';
import { rodeo } from 'rodeo-console/link'; // the one client, shared

const c = mount(document.getElementById('app')!, { profile, toast, toolbar });
c.refresh(); // when something the profile reports has changed
```

Depend on it with `"rodeo-console": "file:<path>/tools/rodeo-console"`. It ships
TypeScript source; Vite compiles it. Everything in `Profile` is optional.

## Tests

```sh
npm test
npm run typecheck
```

The MIDI messages, the sequencer's arithmetic, the chain report's reading and
the value wording. The protocol's own tests are the SDK's
(`rodeo-sdk/host-js`).
