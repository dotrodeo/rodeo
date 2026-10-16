---
title: "Rodeo Encoder-Hub ↔ Motherboard — I²C Register Contract"
tags: [rodeo, firmware, i2c, encoder-hub, stm32c0, contract]
status: locked — silicon-verified by Bumble 2026-08-20
created: 2026-08-20
updated: 2026-08-20
owner: Fizz (firmware/DSP)
reviewers: [Bumble (hardware — verify against silicon), derek]
---

# Rodeo Encoder-Hub ↔ Motherboard — I²C Register Contract

**This is the wire contract** both firmwares code against: the Daisy Seed motherboard
(I²C **master**) and each `rodeo-encoder-hub` (STM32C031C6, I²C **target**). Change the
byte layout here and you break both sides — so this doc is the single source of truth for
the register map, and the [`RODEO_CONTROL_MAP.md`](../../../instruments/rodeo-001/docs/RODEO_CONTROL_MAP.md) is the source of
truth for which physical control each encoder *slot* means.

- **Firmware (Fizz)** owns this map, the target-side implementation, and the master-side driver.
- **Hardware (Bumble)** owns the electrical half it sits on — verified below — and will
  review this map against the actual board (address bits, encoder/switch polarity).
- **Datasheet ground:** STM32C031C6 (Cortex-M0+, 48 MHz, 32 KB flash / 12 KB RAM),
  I²C1 on PB6 (SCL) / PB7 (SDA).

---

## 1. Electrical foundation (Bumble's lane — locked 2026-08-20)

Reproduced here so the map and the hardware build from one ground. Bumble owns these lines.

| Item | Value |
|---|---|
| **Role** | Hub = I²C **target**; motherboard Daisy = I²C **master** |
| **Bus** | I²C1, hub pins PB6 (SCL) / PB7 (SDA). **Shared bus:** the SSD1309 **OLED @ `0x3C`** (feedback display) + up to 4 hubs (`0x30–0x33`). The OLED is on **I²C**, not SPI — SPI1 is fully consumed by the two ADS114S08 ADCs, so the SPI-OLED variant is deferred to Rev-B (Decision A / `RODEO_CONTROL_MAP.md`, 2026-08-15, confirmed Derek 2026-08-20). No address collision: `0x3C` vs `0x30–0x33` |
| **Address** | 7-bit **0x30–0x33**, set by 2 cuttable jumpers (PD0 + PA15); both bridged = base **0x30** |
| **Speed** | Boot **100 kHz**; move to **400 kHz** once the full populated bus is validated |
| **Pull-ups** | **Hub is the Rev-A bus anchor (LOCKED 2026-08-20).** Produced motherboard has none, Daisy's ~40 kΩ internal pulls can't carry the bus, and the HiLetgo SSD1309 has none — so the hub carries **2× 2.2 kΩ, SDA→+3V3 and SCL→+3V3** (Qwiic/MCU +3V3 rail, *not* `LED_3V3`), near J1/J2. **No jumper** (Rev-A space) — safe because every Rev-A hub always pairs with a Rev-A (pull-less) motherboard; both revise to Rev-B together, so a double-pulled bus never happens. Consequence (accepted): OLED shares the bus, so **no hub = no I²C**. Firmware is **indifferent to the anchor** — it only needs valid bus pulls at bring-up. **Rev-B:** pulls move to the motherboard. Bumble owns placement |
| **Clock stretching** | Hub serves reads from a **RAM shadow** refreshed by the ~1 kHz poll → stretch ≈ 0. Master must still tolerate stretch defensively |
| **Encoders** | **9 per hub**, slots **1–9 = connectors J3–J11** in order. Slot N ↔ connector J(N+2) ↔ panel encoder N (canonical, matches Honey's panel numbering) |
| **Signal polarity** | All inputs **active-low** via the hub's internal pull-ups (encoder common = GND, switch S line to GND). The hub **normalizes** polarity before it hits the register — see §4 |

The register map is **hardware-generic**: every hub always exposes all 9 encoder slots
regardless of how many are populated. *Which* slots carry live controls, and what those
controls mean, is the control-map's job — not this contract's.

---

## 2. Transaction model

Standard I²C register-pointer peripheral. The hub holds an 8-bit **register pointer** with
**auto-increment**.

**Read** N bytes from register `R`:
```
START · [addr|W] · R · REPEATED-START · [addr|R] · read b0 · read b1 … · STOP
```
Pointer starts at `R`, increments per byte. Reading past the map wraps to `0x00`.

**Write** N bytes from register `R`:
```
START · [addr|W] · R · write b0 · write b1 … · STOP
```
First byte after the address is always the pointer; subsequent bytes are data, pointer
auto-increments.

**Conventions**
- All values **single-byte** except the LED block (§7), which is 3 bytes/encoder.
- Multi-byte scalars would be little-endian, but none exist in v1.
- **Read-to-clear** registers (encoder deltas, switch-event latches) reset **atomically on
  the read of their byte**, under a brief critical section against the poll ISR, so no step
  is lost between the poll and the master read.
- Unpopulated / reserved registers read `0x00`. Writes to read-only or unimplemented
  registers are **ACK'd and ignored** (no NACK) so the master never has to special-case a
  hub revision.

---

## 3. Register map (v1)

| Addr | Name | R/W | Notes |
|---|---|---|---|
| **Info block** ||||
| `0x00` | `WHO_AM_I` | R | Constant **`0x9E`** — hub identity, master probes this to confirm a hub (vs OLED) at an address |
| `0x01` | `FW_VERSION` | R | Hub firmware build version (bumps freely) |
| `0x02` | `MAP_VERSION` | R | **This contract's version = `0x01`**. Master refuses/degrades on mismatch |
| `0x03` | `HUB_ADDRESS` | R | Current 7-bit address `0x30–0x33`. Computed at boot as **`0x30 \| (PA15<<1) \| PD0`** — JP1/PD0 = A0/bit0 (cut → +1), JP2/PA15 = A1/bit1 (cut → +2), default both bridged = `0x30` (verified Bumble 2026-08-20) |
| `0x04` | `ENC_COUNT` | R | Number of encoder slots = **`0x09`** |
| `0x05` | `CAPABILITIES` | R | bit0 encoders, bit1 switches, **bit2 LED present** (=0 until ring board ships), bits3–7 reserved=0 |
| `0x06` | `STATUS` | R | bit0 `ENC_ACTIVITY` (any nonzero delta pending), bit1 `SW_EVENT` (any latched press pending), bit2 `OVERFLOW` (a delta saturated since last read), bits3–7 reserved. **Read-to-clear bit2** only |
| `0x07` | `CMD` | W | Command strobe — see §6 |
| `0x08–0x0F` | *reserved* | R | Read `0x00` |
| **Telemetry block — contiguous, the hot read** ||||
| `0x10` | `ENC1_DELTA` | R/clr | Signed **int8** accumulated detents since last read; **read-clears** |
| `0x11–0x18` | `ENC2..ENC9_DELTA` | R/clr | …one byte per encoder, slots 2–9 |
| `0x19` | `SW_STATE_LO` | R | Live debounced switch level, **bit N = encoder (N+1) pressed** (1 = pressed). Encoders 1–8 |
| `0x1A` | `SW_STATE_HI` | R | bit0 = encoder 9 pressed; bits1–7 reserved=0 |
| `0x1B–0x1F` | *reserved (telemetry pad)* | R | Read `0x00`. Keeps the hot block 16 bytes so a fixed 16-byte burst from `0x10` is future-proof |
| **Switch event latches** ||||
| `0x20` | `SW_PRESSED_LO` | R/clr | Latched **press events** (0→1 edges) since last read, bit N = encoder (N+1). **Read-clears.** Catches a tap that begins & ends between master polls |
| `0x21` | `SW_PRESSED_HI` | R/clr | bit0 = encoder 9; **read-clears** |
| `0x22–0x2F` | *reserved* | R | Read `0x00` |
| **Config** ||||
| `0x30` | `CONFIG` | R/W | bit0 `ENC_DIR_INVERT` (flip CW/CCW sign for all encoders), bit1 `SW_EVENT_ON_RELEASE` (latch release edges too), bits2–7 reserved=0. Defaults `0x00` |
| `0x31–0x3F` | *reserved* | R/W | Read `0x00` |
| **LED block — RESERVED `0x40–0x5F` for future ring upgrade (§7)** — the whole 32-byte span is the reserved LED window; nothing else is ever assigned here ||||
| `0x40–0x5A` | `LED[1..9]` | W | 3 bytes/encoder **G,R,B** (WS2812 order), 27 bytes. Writes **no-op** while `CAPABILITIES.bit2 = 0` |
| `0x5B` | `LED_BRIGHTNESS` | W | Global 0–255 scale; no-op until LEDs populated |
| `0x5C–0x5F` | *reserved (LED)* | W | Pad to a clean 32-byte block |

**The hot read:** a master control task issues one transaction — set pointer `0x10`, read
**up to 16 bytes** — and gets all 9 deltas (auto-cleared) + both switch-state bytes in a
single bus turn. Latched press events (`0x20/0x21`) are a separate short read only when
`STATUS.bit1` says there's something to fetch, or unconditionally each control frame — cheap
either way.

---

## 4. Encoder semantics

- **Delta = signed detents since the master last read that byte.** `+1` per detent in the
  CW ("increase") direction, `−1` CCW. Firmware decodes quadrature with a **Gray-code state
  machine** in the ~1 kHz poll ISR (per-edge EXTI is off the table — several A/B pairs
  share EXTI lines across ports; polling is the correct read for 9 mechanical encoders and
  is trivial load at 48 MHz).
- **Read-clears, saturating.** The accumulator is `int8` and **saturates** at `+127 / −128`;
  if it saturates before the master reads, `STATUS.OVERFLOW` (bit2) is set. At any realistic
  master cadence (≤ a few ms between reads) a human hand produces ≪1 detent/read, so
  saturation is a fault indicator, not an expected path.
- **Direction convention** is CW = positive; flip globally with `CONFIG.ENC_DIR_INVERT` if a
  given panel wiring reads backwards. Per-encoder sign is the *master's* job (control map),
  not the hub's.

## 5. Switch semantics

- Each encoder's push switch (S line) is **active-low**; the hub **normalizes so 1 = pressed**
  in every register. Master never sees the raw polarity.
- **Debounce** is on the hub (~2–5 ms in the poll loop); the master gets clean levels.
- `SW_STATE_*` = **live level** (what's held right now). `SW_PRESSED_*` = **latched press
  edges**, read-to-clear, so a fast tap between polls is never missed. Release edges are
  off by default; enable with `CONFIG.SW_EVENT_ON_RELEASE` if a control needs them.

## 6. Commands (`0x07 CMD`, write-strobe)

| Value | Action |
|---|---|
| `0x01` | `CLEAR_DELTAS` — zero all 9 encoder accumulators |
| `0x02` | `CLEAR_SW_LATCH` — clear `SW_PRESSED_*` |
| `0x03` | `CLEAR_ALL` — both of the above + `STATUS` sticky bits |
| `0xA5` | `SOFT_RESET` — re-read address jumpers, reset shadow (does not drop off the bus) |

Unknown command values are ACK'd and ignored.

## 7. LED block — reserved, stubbed today (Bumble's forward ask)

LEDs are **parked as a future upgrade**: the hub drives `LED_DATA = PB8 = TIM16_CH1`
(DMA-capable, verified) out J13, but **no board mounts the SK6812s yet** and encoder
feedback is carried by the **OLED**, not by rings. Per Bumble's ask, the map **reserves the
LED register block now** (`0x40–0x5F`, 3 bytes/encoder G/R/B + global brightness) so the
future `rodeo-encoder-ring` variant drops in **without renumbering**. Until then:
`CAPABILITIES.bit2 = 0`, the WS2812/TIM16+DMA driver is **stubbed**, and LED writes are
silently no-op'd. Zero cost today.

---

## 8. Master-side behavior (motherboard firmware)

- **Enumerate at boot:** probe `0x30–0x33`, read `WHO_AM_I`; a `0x9E` = a hub, confirm
  `MAP_VERSION == 0x01`. **Skip `0x3C`** — that's the OLED, not a hub. Build the live-hub list.
- **Poll cadence:** read the `0x10` hot block once per control frame (target ~1 kHz control
  update). The OLED **shares I²C1**, so hub bursts interleave with display writes — SSD1309
  full frame ≈23 ms @ 400 kHz, so firmware schedules partial / low-rate OLED refresh against
  the ~1 kHz hub polls (12–16-byte bursts); no contention when scheduled.
- **Apply deltas** to the mapped parameter for that slot per `RODEO_CONTROL_MAP.md`; deltas
  are relative, so a dropped/duplicated frame is self-healing (read-clears prevents dupes).
- **Bus start 100 kHz**, promote to 400 kHz after Bumble validates the populated bus.

---

## 9. Open items / for review

1. ~~**Bumble to verify against silicon.**~~ **RESOLVED 2026-08-20** — address-bit order,
   uniform per-slot A/B/S ordering, and S-line pin all confirmed against the schematic (see
   §10 pin map). CW polarity is a one-time bench calibration via `CONFIG.ENC_DIR_INVERT`
   (EC11 phasing + knob mount can't be certified from the schematic).
2. **Control-map refresh (Bumble/Fizz):** the frozen Path-B allocation predates 9-per-hub —
   it lists two 4-encoder banks (`0x30` = 4 trims, `0x31` = 2 octaves). With a single
   **9-encoder** hub, all 6 planned encoders (OSC2 Detune, Unison detune, Glide, Master
   tune, OSC1 Octave, OSC2 Octave) fit on **one hub at `0x30`** with 3 slots spare — collapse
   to one hub unless a second is wanted for panel geometry. Slot→function table then lands
   in `RODEO_CONTROL_MAP.md`, not here.
3. **INT line:** baseline is master-polled (no interrupt dependency). If the hub exposes an
   INT pad, a future rev can drive it from `STATUS` — noted, not specified, until the board
   confirms a line exists.
4. **`MAP_VERSION` discipline:** any change to §3 byte positions bumps `0x02` and this
   header's `updated:`. Additive-only within reserved ranges is a minor bump; a move/renumber
   is a breaking bump both firmwares must gate on.

---

## 10. Hub target-side pin map (verified Bumble 2026-08-20)

Implementation reference for the STM32C031C6 hub firmware — **not** part of the wire
contract, but the silicon the target codes against. A/B/S assignment is **uniform across all
9 slots**: `A` = connector pin1/`_A`, `B` = pin2/`_B`, `S` = pin3/`_S`. Slot N = connector
J(N+2).

| Slot | Conn | A (pin1) | B (pin2) | S (pin3) |
|---|---|---|---|---|
| E1 | J3  | PA0  | PA1  | PA2  |
| E2 | J4  | PA3  | PA4  | PA5  |
| E3 | J5  | PA6  | PA7  | PB0  |
| E4 | J6  | PB1  | PB2  | PB10 |
| E5 | J7  | PB11 | PB12 | PB13 |
| E6 | J8  | PB14 | PB15 | PA8  |
| E7 | J9  | PA9  | PC6  | PC7  |
| E8 | J10 | PA10 | PA11 | PA12 |
| E9 | J11 | PB3  | PB4  | PB5  |

- **All inputs internal pull-up, active-low.** Encoder common = GND, S = to GND.
- **Poll, don't EXTI:** A/B lines collide on shared EXTI lines by pin *number* across ports
  (e.g. EXTI3 = PA3 + PB3), so per-edge interrupt decode is impossible. The ~1 kHz timer-ISR
  Gray-code poll reads all 27 lines each tick — trivial at 48 MHz.
- **Address jumpers:** JP1 = PD0 (A0/bit0), JP2 = PA15 (A1/bit1).
- **I²C1 target:** PB6 (SCL) / PB7 (SDA). **LED data (reserved):** PB8 = TIM16_CH1 (DMA).

## 11. Open input (blocks the *master*, not the hub skeleton)

**Slot → parameter binding** waits on Honey's final panel layout (Bumble collapsed the voice
to one hub @ `0x30`, 6 of 9 slots, 3 spare). This is a **master-side** concern — the hub
firmware is slot-generic and needs none of it — so the STM32C0 skeleton proceeds now; the
binding lands in `RODEO_CONTROL_MAP.md` when the panel finalizes.
