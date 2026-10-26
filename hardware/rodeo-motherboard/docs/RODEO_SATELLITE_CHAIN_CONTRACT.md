---
title: "Rodeo Satellite Chain — Wire Contract"
tags: [rodeo, firmware, uart, satellites, contract]
status: draft v1 — frame format in code (rodeo-sdk/common/sat_frame.h); not yet on hardware
created: 2026-10-08
updated: 2026-10-26
reviewers: [derek]
---

# Rodeo Satellite Chain — Wire Contract

**This is the contract both firmwares code against:** the Daisy Seed on the Rev B
motherboard (the chain's only master) and every satellite. Change a byte layout here and
you break both sides, so this doc is the single source of truth for the electrical
interface, the frame format, discovery and fault handling. The motherboard side of the
hardware is in [`rev-b.md`, "Satellite chain on USART3"](rev-b.md#satellite-chain-on-usart3-hybrid-analog).

**The design in one paragraph.** Satellites sit on a daisy-chain that loops back to the
Seed. The Seed sends a frame with one fixed-size slot per satellite. Each satellite takes
the first slot (its outputs, such as LED state), forwards the rest, and appends a slot of
its own (its inputs) at the end. The frame comes back the same length, with every
satellite's inputs in chain order. A satellite's address is its position in the chain.
One firmware image runs on every satellite; an ID resistor tells it what kind of board it
is on.

---

## 1. Electrical

### 1.1 Connector

Every satellite has an **IN** and an **OUT** connector, both **JST SH 5-pin**. The
motherboard has one chain connector, J18, which is wired like a satellite's OUT.

| Pin | Signal | Satellite IN | Satellite OUT | Motherboard J18 |
|---|---|---|---|---|
| 1 | GND | GND | GND | GND |
| 2 | 3V3 | supply in | supply out (straight through) | `+3V3_SAT`, through a 0.75 A PTC |
| 3 | `DATA_FWD` | MCU RX | MCU TX (normal routing) | Seed TX (`SAT_TX`), 33 Ω |
| 4 | `DATA_RET` | trace straight through to OUT pin 4, MCU **return tap** on it | ← | Seed RX (`SAT_RX`), 10 kΩ pull-up |
| 5 | `PRESENT` | tied to GND | MCU input with pull-up | not connected |

`DATA_FWD` is point-to-point: one transmitter, one receiver per cable. `DATA_RET` is one
long wire from the end of the chain back to the Seed, passing through every satellite.

### 1.2 Line

| Item | Value |
|---|---|
| Format | UART, 8 data bits, no parity, 1 stop bit, LSB first, idle high |
| Rate | **1 Mbaud** (exact from a 48 MHz or 24 MHz clock: divisor 48 or 24) |
| Levels | 3.3 V CMOS. Satellites run directly from the chain's 3V3; no regulator. |
| Seed side | USART3: TX PC10 (Seed pin 3), RX PC11 (pin 2) |

### 1.3 Closing the ring

* At boot, and every 1 ms after, each satellite reads its OUT `PRESENT` pin. **Low:**
  something is plugged in downstream, so the satellite transmits on `DATA_FWD`.
  **High:** it is the last satellite, so it moves its UART TX to its `DATA_RET` tap. Its
  frames then travel back along the return wire to the Seed.
* **A satellite drives its `DATA_RET` tap only while it is transmitting a frame.** The
  rest of the time the tap is an input (high-impedance), and the motherboard's 10 kΩ
  pull-up holds the idle line high. This matters when the ring is closed early by a
  command (§5.2): the satellite past the break never receives a frame, so it never
  transmits, and two drivers can never fight over the return wire.
* Set the tap to push-pull just before the first byte of a frame and back to input after
  the UART's transmit-complete flag for the last byte.

### 1.4 Power budget

**What limits the chain is voltage drop, not current.** The chain is fed from one end
at 3.3 V and every satellite must run down to 2.7 V. Each link (150 mm of 28 AWG plus
two connector matings, out and back) is ≈0.14 Ω and carries the current of every
satellite past it.

* **Baseline: ≈6 mA average per satellite** (MCU and an indicator LED), 30 satellites
  on one feed. The last sits ≈67 Ω × (current per satellite) below the motherboard:
  0.4 V at 6 mA, leaving ≈2.9 V and ≈0.2 V of headroom.
* **Spending the headroom:** a satellite that draws ΔI more than the baseline at
  position *p* (1 = next to the motherboard) lowers the end of the chain by
  ≈0.14 Ω × *p* × ΔI. Keep **Σ (*p* × ΔI) ≤ ≈1.4 A** over the whole chain. Four
  single-colour lit buttons at ≈5 mA each (ΔI ≈ 20 mA) cost 85 mV even at position
  30, and under 30 mV in a chain of ten; a short chain has far more room than this,
  since the baseline drop falls with the square of its length.
* **Anything whose LEDs need more than 3.3 V or tens of mA** (addressable RGB, which
  wants ≥3.5 V; meters) takes its LED power from a separate 5 V input and keeps its
  MCU on the chain's 3.3 V, so logic levels don't change. Ground is shared through
  both cables. The 5 V comes from the motherboard's `+5V_SYS`, which Rev B doesn't
  bring to a connector yet (see `hardware/docs/revisions.md`).

### 1.5 Satellite MCU requirements

The contract doesn't fix the MCU. Any part that meets these can be a satellite:

| Requirement | Why |
|---|---|
| UART TX remappable at runtime to a second pin (the `DATA_RET` tap) | closing the ring (§1.3); copying TX in software can't keep up at 1 Mbaud |
| 1 Mbaud UART, with a way to handle a byte every 10 µs (DMA or a fast interrupt) | §1.2 |
| ADC of 10 bits or more, referenced to VDD | ID resistor (§1.6) and pots, ratiometric to the satellite's own supply |
| Runs from 2.7 V to 3.6 V | §1.4 |
| Programmable through pads | §8 |

**Chosen for the first satellite (2026-10-14): CH32V003F4P6** (TSSOP-20, 10-bit ADC).
USART1 has four pin mappings, and TX and RX always move together: 0 = TX PD5/RX PD6,
1 = PD0/PD1, 2 = PD6/PD5, 3 = PC0/PC1. A satellite uses mapping 0 normally (TX PD5 to
OUT `DATA_FWD`, RX PD6 from IN) and mapping 3 when it is last (TX PC0 on the `DATA_RET`
tap), with IN `DATA_FWD` wired to both PD6 and PC1 so RX follows. Mapping 1 is avoided
because PD1 is SWIO. Alternative MCU: **PY32F002A** (12-bit ADC).

### 1.6 Board-kind ID resistor

One firmware image runs on every satellite. At boot it reads a divider on an ADC pin:
10 kΩ (1 %) from a GPIO driven high during the read (or from 3V3, which costs 0.33 mA) to
the ADC pin, and **R_ID** (1 %, E96) from the ADC pin to GND.

`kind = round(15 × ADC / ADC_full_scale)`

| Kind | R_ID | | Kind | R_ID |
|---|---|---|---|---|
| 0 | 0 Ω (short) | | 8 | 11.5 kΩ |
| 1 | 715 Ω | | 9 | 15.0 kΩ |
| 2 | 1.54 kΩ | | 10 | 20.0 kΩ |
| 3 | 2.49 kΩ | | 11 | 27.4 kΩ |
| 4 | 3.65 kΩ | | 12 | 40.2 kΩ |
| 5 | 4.99 kΩ | | 13 | 64.9 kΩ |
| 6 | 6.65 kΩ | | 14 | 140 kΩ |
| 7 | 8.66 kΩ | | 15 | not fitted |

With both resistors at 1 %, every value lands within ±0.1 of its kind number. A
reading further than ±0.3 from every kind is fault `0x01` (§7), not a guess.

**Assigned kinds (v1):**

| Kind | Board | Slots | Input slot | Output slot |
|---|---|---|---|---|
| 0 | reserved (test fixture) | 1 | — | — |
| 1 | potentiometer | 1 | `POT` | `LED` (optional LED) |
| 2 | button (`rodeo-button-1`, specified) | 1 | `BUTTON` | `LED` |
| 3 | encoder with push switch | 1 | `ENCODER` | `LED` |
| 4 | rotary switch, up to 8 positions (`rodeo-rotary-switch-1`: 4 positions, specified) | 1 | `SWITCH` | `LED` |
| 5 | pot bank ×4 (`rodeo-pot-bank-4`) | 4 | `POT` ×4, pots 1–4 in order | slot 1 `LED`, slots 2–4 `NONE` |
| 6 | encoder bank ×4 (`rodeo-encoder-bank-4`) | 4 | `ENCODER` ×4, encoders 1–4 in order | slot 1 `LED`, slots 2–4 `NONE` |
| 7 | button bank ×4 (`rodeo-button-bank-4`) | 4 | `BUTTON` ×4, buttons 1–4 in order | `RGB` ×4, the LED beside each button |
| 8–13 | unassigned: kept for future Rodeo satellites | | | |
| 14 | **custom board**, running its own firmware (the satellite dev board, and boards built from it) | 1–4, as its `DISCOVER` reply says | any input kind, per slot | any output kind, per slot |
| 15 | **no ID resistor fitted:** sends `EMPTY` | 1 | `EMPTY` | ignored |

A new board type needs a new resistor value and a firmware update, not a separate
firmware image.

**Kind 14 is the exception:** it's for boards Rodeo doesn't make, so the stock image
doesn't run them; it reports fault `0x02` (kind not supported) on one. A custom
board runs its own firmware, which speaks this contract and declares its slots in
its `DISCOVER` reply (§4). The Seed takes a satellite's slot count from that reply,
never from the kind, and reads each slot by the input kind it carries, so a custom
board needs nothing new on the Seed. Kinds 8–13 stay for Rodeo's own future
satellites, which the stock image will learn.

---

## 2. Frames

### 2.1 Layout

Every frame is a 4-byte header followed by N slots of 4 bytes.

**Header**

| Byte | Field | Meaning |
|---|---|---|
| 0 | `SOF` | `0xA5` for protocol v1. Any other value: not a v1 frame. |
| 1 | `TYPE` | `0x01` SCAN, `0x02` DISCOVER, `0x03` COMMAND |
| 2 | `N` | number of slots that follow, 0–64 |
| 3 | `HOP` | satellites this frame has passed through so far. The Seed sends 0; each satellite adds 1. |

**Slot**

| Byte | Field |
|---|---|
| 0 | `KIND` (§3) |
| 1 | `D0` |
| 2 | `D1` |
| 3 | `CRC`: CRC-8 over bytes 0–2 of this slot. Polynomial `0x07`, init `0x00`, no reflection, no final XOR (CRC-8/SMBUS). |

Frame length is `4 + 4N` bytes, at most 260 bytes (2.6 ms at 1 Mbaud).

### 2.2 Framing and resynchronisation

* A transmitter sends a whole frame without gaps.
* A receiver starts a frame on `SOF` after the line has been idle for at least 2 byte
  times (20 µs). An idle gap of 2 byte times inside a frame aborts it; the receiver
  drops what it has and waits for the next `SOF`.
* Header fields aren't checksummed. A receiver drops a frame whose `TYPE` is unknown or
  whose `N` is over 64. Slot CRCs protect the payload; a corrupted header shows up at the
  Seed as a frame of the wrong length, a wrong `HOP` or failed slot CRCs.

### 2.3 What a satellite does with a frame

A satellite at chain position `p = HOP_in + 1` that owns `s` slots (`s` is set by its
kind: 4 for the pot and encoder banks, 1 for every other v1 kind):

| `TYPE` | Satellite action | Out |
|---|---|---|
| SCAN | Consume the first `s` slots (its outputs). Forward the remaining `N − s` slots unchanged. Append `s` input slots. | Same `N`, `HOP + 1` |
| DISCOVER | Forward all `N` slots unchanged. Append one descriptor slot (§4). | `N + 1`, `HOP + 1` |
| COMMAND | Forward all slots unchanged. Act on any command addressed to it (§5). | Same `N`, `HOP + 1` |

**Pipelining:** a satellite starts sending once it holds enough of the frame that it
can't run dry: the header and, in a SCAN, the `s` slots it consumes, plus the byte in
flight. That is a lag of `1 + 4s` byte times in a SCAN (5 for a one-slot satellite, 17
for a four-slot bank) and 5 for DISCOVER and COMMAND. It can't be less: the first byte
it forwards in a SCAN is the first byte after its own slots, and it must have arrived
before it can be sent. From then on it sends one byte for every byte it receives, and
once the input ends it sends what it still holds and then its own slot(s). The output is
continuous, and the frame length is what the table says. The appended slot must be
ready before the frame arrives: satellites sample continuously and keep the latest slot
built. (Corrected 2026-10-19 from "holds back exactly 4 bytes, 40 µs": that lag can't
hold for a SCAN, and a four-slot bank needs far more. `sat_lag_bytes()` in
`rodeo-sdk/common/sat_frame.h`.)

A satellite that receives a SCAN with `N < s` forwards it unchanged with `HOP + 1` and
reports nothing (the Seed's map is stale; §5.1 recovers).

---

## 3. Slot kinds

`D0`/`D1` are bytes 1 and 2 of the slot. 16-bit values are little-endian (`D0` low).

### 3.1 Input slots (satellite → Seed)

**Report state, not events.** If a scan frame is lost, nothing is missed: counters
wrap, and the Seed works out what changed from the difference with the last good value.

| `KIND` | Name | `D0`, `D1` |
|---|---|---|
| `0x00` | `EMPTY` | 0, 0 |
| `0x01` | `POT` | position, 16-bit unsigned, scaled to 0–65535 whatever the ADC's resolution. The satellite filters it; the Seed applies any further smoothing or hysteresis. |
| `0x02` | `BUTTON` | `D0` bit 0: pressed now. `D1`: press counter, +1 on each debounced press, wraps at 256. |
| `0x03` | `ENCODER` | `D0`: position counter in detents, wraps at 256 (the signed difference gives direction). `D1` bit 0: switch pressed now; bits 4–7: switch press counter, wraps at 16. |
| `0x04` | `SWITCH` | `D0`: position index from 0. `D1`: 0. |
| `0x7F` | `FAULT` | `D0`: fault code (§7). `D1`: detail. |

### 3.2 Output slots (Seed → satellite)

| `KIND` | Name | `D0`, `D1` |
|---|---|---|
| `0x00` | `NONE` | ignored; the satellite keeps its current output state |
| `0x10` | `LED` | `D0`: brightness 0–255 (0 = off). `D1`: 0 steady, 1 slow blink (1 Hz), 2 fast blink (4 Hz). |
| `0x11` | `RGB` | colour as RGB565, little-endian (`D0` low): bits 0–4 blue, 5–10 green, 11–15 red. 0 is off. |

**`RGB` fits in the two bytes every slot has** because RGB565 is sixteen bits, so a
colour needs no second slot and no change to §2. It carries no brightness or blink
field: scale the colour instead, and blink by sending a different colour. A satellite
with colour LEDs also accepts `LED` on the same slot and shows it as white at that
brightness, so a Seed that knows nothing about colour can still drive it.

A satellite ignores an output slot that fails its CRC or whose `KIND` it doesn't
support, and keeps its previous output state.

---

## 4. Discovery

DISCOVER is how the Seed learns the chain: how many satellites, in what order, of what
kind, running what firmware. The Seed sends `N = 0`; each satellite appends one
descriptor slot.

**Descriptor slot**

| Byte | Value |
|---|---|
| `KIND` | `0x80` |
| `D0` | bits 0–3: board kind (§1.6). Bits 4–5: slots owned minus 1. Bit 6: this satellite is last (`PRESENT` high). Bit 7: 0. |
| `D1` | firmware version: bits 4–7 major, bits 0–3 minor |

**On the Seed:** after DISCOVER returns, `HOP` is the satellite count and the slots are
the descriptors in chain order, so the Seed knows each satellite's position, kind and
slot count. It checks that:

* `HOP` equals the number of descriptors, and every descriptor's CRC is good;
* exactly one descriptor, the last, has the "last" bit set;
* every satellite's major firmware version matches what the Seed supports. A mismatch is
  reported (OLED) rather than silently ignored.

It then compares the chain with the instrument's expected layout, if it has one, and
builds the SCAN frame: `N` = the sum of the slots owned, output slots in chain order.

---

## 5. Commands

A COMMAND frame carries one or more command slots. Every satellite forwards them; the
satellite at the addressed position acts. Position 0 means every satellite.

| `KIND` | Command | `D0` | `D1` |
|---|---|---|---|
| `0x40` | `CLOSE_RING_HERE` | target position | 0 |
| `0x41` | `RING_AUTO` | target position, or 0 | 0 |
| `0x42` | `RESET` | target position, or 0 | `0x5A` (guard; any other value is ignored) |
| `0x43` | `IDENTIFY` | target position | seconds to blink its LED fast |

### 5.1 Recovery

The Seed treats the ring as broken when three SCAN frames in a row don't return within
the timeout (§6.2) or return with the wrong `HOP`. It then:

1. sends DISCOVER (up to 3 attempts). If it returns, the Seed rebuilds the map and
   resumes. This covers a satellite added or removed while powered.
2. If DISCOVER doesn't return, finds the break (§5.2).

### 5.2 Finding a break

A dead or unflashed satellite still pulls `PRESENT` low on the board before it, so that
board keeps sending into a dead end. Commands still reach every satellite up to the
break, so:

1. For `p = 1, 2, …`: send `RING_AUTO` to position `p − 1` (from `p = 2`), then
   `CLOSE_RING_HERE` to position `p`, then DISCOVER. The `RING_AUTO` matters: a
   satellite stays closed until told otherwise, so without it every DISCOVER after
   the first turns round at satellite 1 and the search never gets past it.
   (Added 2026-10-19; `rodeo-sdk/common/sat_chain.cpp` does this, and its host test
   fails without it.)
2. The largest `p` for which DISCOVER returns is the last working satellite. The fault is
   in satellite `p + 1` or the cable into it.
3. Report "chain broken after satellite p" (OLED), and keep scanning the first `p`
   satellites so the instrument stays usable.
4. Send `RING_AUTO` to position `p` once the fault is fixed (or retry periodically).

`CLOSE_RING_HERE` makes a satellite route TX to its return tap regardless of `PRESENT`,
until `RING_AUTO` or a reset. Satellites past the break never receive a frame, so they
never drive `DATA_RET` (§1.3).

---

## 6. Timing

### 6.1 Satellite boot

A satellite is ready to pass frames **within 20 ms** of its supply reaching 2.7 V: it has
read its ID resistor and `PRESENT` and set its routing. The Seed sends its first
DISCOVER **at least 50 ms** after the chain is powered.

### 6.2 Scan cycle

Only one frame is in the ring at a time: the Seed sends the next frame after the
previous one returns, or after a timeout.

* Ring time ≈ (4 + 4N) × 10 µs for the frame, plus every satellite's lag (§2.3):
  (satellites + 4N) × 10 µs for a SCAN. For 30 one-slot satellites: 1.24 ms + 1.5 ms ≈
  **2.7 ms**. For 15 four-slot banks (N = 60): 2.44 ms + 2.55 ms ≈ 5 ms.
* Timeout: 2 × the expected ring time, at least 1 ms.
* **Default scan period: 4 ms (250 scans/s).** A control change reaches the Seed within
  about one period plus one ring time (≈6.7 ms worst case for 30 satellites). Firmware
  may scan faster for small chains.
* The Seed paces scans from a timer, not from the audio callback.

### 6.3 Seed driver notes

From [`rev-b.md`](rev-b.md#satellite-chain-on-usart3-hybrid-analog): a Rodeo USART3
driver in `rodeo-sdk`, on DMA streams libDaisy leaves free (`DMA1_Stream7` RX,
`DMA2_Stream5` TX). A SCAN reply has a known length (`4 + 4N`), so RX is one DMA
transfer of exactly that many bytes. A DISCOVER reply's length isn't known in advance:
arm RX for the 64-slot maximum and read the received count when the timeout fires.

---

## 7. Fault codes (`FAULT` slot)

A satellite sends a `FAULT` slot in place of its normal input slot when it can't
produce one.

| `D0` | Meaning | `D1` |
|---|---|---|
| `0x01` | ID resistor reads between bands | 10 × 15 × reading / full scale (e.g. 73 = 7.3) |
| `0x02` | kind not supported by this firmware | kind |
| `0x03` | ADC or sensor failure | 0 |

---

## 8. Programming and versions

* **v1: satellites are programmed at assembly** through the MCU's programming pads
  (for the CH32V003: SWIO, 3V3 and GND, with a WCH-LinkE). Every satellite gets the
  same image; the ID resistor does the rest.
* The firmware version is reported in discovery (§4). The Seed refuses to scan a chain
  with a mismatched major version and says so, rather than misreading slots.
* Updating satellites through the chain (a bootloader that takes firmware over
  `DATA_FWD`) is deferred. It would add a `TYPE` and needs no electrical change.

---

## 9. Open items

* **Measure on the first build:** the real link resistance and the voltage at the end of
  a long chain; the `DATA_RET` waveform at 1 Mbaud through 30 pass-throughs, and whether
  the idle line, held only by the 10 kΩ pull-up between frames, picks up noise.
* **Multi-slot satellites** (`s` > 1): the format supports up to 4 slots per satellite.
  Kind 5, the pot bank, is the first to use 4; its discovery reply and slot ordering
  are the first real test of §2 and §4 with `s` > 1.
* **The button bank's LED supply is outside this contract.** Kind 7 draws its LED
  power from a 5 V aux cable when one is plugged in and from the chain's 3V3 when it
  is not, capping brightness in firmware in that case (§1.4). Nothing on the chain
  tells the Seed which supply a satellite is on; if that turns out to matter, it
  belongs in the discovery reply (§4), not in a slot.
