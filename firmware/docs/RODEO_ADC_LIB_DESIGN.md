# Rodeo ADC Abstraction Library — Design

**Status:** Draft (planning). Owner: Fizz (firmware). Analog/hardware input: Bumble.
**Target part:** TI ADS114S08 (×2, U3/U4), 16-bit delta-sigma, 12-ch mux each.
**Datasheet:** SBAS815A. **Board ref-map source of truth:** `firmware/common/board.h`.

> Purpose: turn the verified `04_adc_1ch` bring-up into a reusable, non-blocking
> driver that presents the Rodeo control surface as 24 clean channels — so the
> synth app never touches SPI or the ADS114S08 register map directly.

---

## 1. Scope

**In scope**
- A per-chip ADS114S08 device driver (register/command transport, presence check).
- A two-chip scanner that pipelines a non-blocking round-robin across all
  populated channels and caches the latest value per channel.
- Board-correct defaults baked in (external 3.3 V ref, SPI mode 1, PGA bypass,
  low-latency filter, per-conversion DELAY).

**Out of scope**
- **Channel → logical-control mapping.** Which channel is "filter cutoff" vs
  "osc 2 tune" is the control-map contract (Bumble / daughterboards), and it
  changes independently of this driver. The library hands over indexed channels;
  the app maps them.
- Audio-rate CV/jack inputs (future; different noise-floor regime — see §11).

---

## 2. Hardware context (from `board.h`)

| Signal | Seed pin | MCU | Notes |
|--------|----------|-----|-------|
| SCK    | D8       | —   | shared SPI1 |
| MISO   | D9       | PB4 | ← ADC **DOUT** (orientation bench-verified 2026-08-14) |
| MOSI   | D10      | PB5 | → ADC **DIN** |
| U3 CS  | D25      | PA0 | soft NSS, active low |
| U4 CS  | D26      | PD11| soft NSS, active low |

- **24 channels** = 2 chips × 12 single-ended (AINx vs AINCOM). `kNumAdcChannels
  = 24`, `kNumAdcPerChip = 12`.
- **Reference:** external, REFP0 = +3V3 / REFN0 = GND → 3.3 V **ratiometric**
  span shared with the pot supply. `REG_REF = 0x30` (external ref, both ref
  buffers bypassed — they sit at the rails, outside buffer CM range). Confirmed
  w/ Bumble 2026-08-14.
- **DRDY# is not wired to the MCU.** We poll RDATA on a timed cadence ≥ the
  first-settled-conversion latency for the active rate (§4).
- **SPI mode 1** (CPOL=0, CPHA=1), the ADS114S08 requirement.

---

## 3. Architecture — two layers

### Layer 1 — `Ads114s08` (device driver, one instance per chip)
Thin, generic, reusable for any single ADS114S08. Owns a `SpiHandle&` + one CS
`GPIO`. Knows the chip; knows nothing about "pots" or "channel 7 = cutoff".

```
class Ads114s08 {
  void Init(SpiHandle& spi, Pin cs_pin, const Config& cfg); // resets, applies defaults, presence-checks
  bool Present() const;                 // write+readback verified a live chip at init
  void Reset();
  void Start(); void Stop();            // conversion control
  void WriteReg(uint8_t addr, uint8_t val);
  uint8_t ReadReg(uint8_t addr);
  void SelectChannel(uint8_t mux);      // WREG INPMUX — auto-restarts filter (§4)
  int16_t ReadData();                   // RDATA (16-bit signed)
  void Calibrate();                     // self-offset cal (SFOCAL), optional (§9)
};
```

**Board defaults applied at `Init`** (see §6 register table). **Presence check:**
write a known pattern to a scratch register (e.g. INPMUX) and read it back; a
mismatch means a dead/mis-wired chip → `Present() == false`, surfaced loudly for
bring-up rather than silently returning garbage.

### Layer 2 — `RodeoAdc` (scanner, owns U3 + U4 = 24 ch)
The surface the app actually calls.

```
class RodeoAdc {
  void Init(SpiHandle& spi);            // constructs both Ads114s08, applies scan config
  void Update();                        // pump from the main loop; advances the pipelined scan
  uint16_t GetCode(uint8_t ch) const;   // latest raw code, 0..N channels
  float    GetVolts(uint8_t ch) const;  // calibrated volts (ratiometric, 0..3.3 V)
  float    GetNorm(uint8_t ch) const;   // normalized 0..1 for control use
  bool     ChannelPopulated(uint8_t ch) const;
};
```

### Layer 3 — mapping (NOT in this library)
`channel index → logical control` lives with the control map. The app owns it.

---

## 4. Scan timing model (SBAS815A)

The throughput ceiling is **conversion settling**, not SPI speed.

- **Low-latency FIR filter is the power-on default and settles in ONE conversion
  period — zero discards per mux hop** (§9.3.6.1, Table 13). Writing the INPMUX
  register **auto-resets the digital filter and restarts the conversion**
  (§9.5.3.12), so the scan primitive is: **WREG INPMUX → wait one settled period
  → RDATA**. No free-running, no counting discards.
  - (Sinc3 is the alternative: 3-cycle settle, ~2.5× slower/hop. Only if we ever
    need its extra noise rejection. Default stays FIR.)
- **Per-hop time** = one conversion period + a fixed ~156 µs overhead
  (40·t_MOD; of which the programmable DELAY is 14·t_MOD by default).
  → **1.16 ms @ 1000 SPS**, 0.41 ms @ 4000 SPS.
- **The modulator sampling clock is fixed:** f_MOD = DR × OSR = **256 kHz for
  every data rate** (= f_CLK/16 at 4.096 MHz). Output data rate is set purely by
  decimation. **Consequence (Bumble, corrected):** a lower data rate buys
  *noise averaging* (narrower bandwidth, more ENOB), **not** switched-cap
  acquisition margin. Do not pick the rate to "help settling" — pick it for
  noise + revisit target.

### The multiplier: the two chips convert in parallel
U3 and U4 have independent converters and separate CS; they share only the bus.
The ~1.16 ms wait is *conversion* time, during which the bus is idle. So the
scanner kicks U3's next channel **and** U4's next channel, both settle
concurrently, then reads both. The bus only serializes the ~10 µs register pokes.
**A 24-ch scan is therefore two parallel 12-ch scans.**

| Data rate | Per-hop (FIR) | 24-ch revisit (12 per chip, pipelined) |
|-----------|---------------|-----------------------------------------|
| 500 SPS   | 2.16 ms       | ~39 Hz/ch (max analog-settle & noise margin) |
| **1000 SPS** | **1.16 ms** | **~72 Hz/ch  ← V1 target** |
| 2000 SPS  | 0.66 ms       | ~126 Hz/ch |
| 4000 SPS  | 0.41 ms       | ~205 Hz/ch |

Dropping unpopulated channels from the scan list scales all of these up
linearly (12 populated ≈ double the rate of 24).

---

## 5. Analog front-end constraints (Bumble)

- **PGA bypassed** (`PGA_EN=00`, `GAIN=000` — this is the **reset default**).
  Required for true rail-to-rail: in bypass the absolute input range is
  **AVSS−0.05 V … AVDD+0.05 V** (SBAS815A Eq. 6) — 50 mV *past* both rails, so
  pots reach true 0 and full-scale. An **enabled** PGA limits input to
  **AVSS+0.15 V … AVDD−0.15 V** (Eq. 5, + a gain-dependent term) — it would clip
  ~150 mV off *each* end of pot travel. Bypass is also what puts us in
  switched-cap land in the first place.
- **Source impedance spec: ≤ 10 kΩ pots** — this is the real settling guardrail,
  written into the daughterboard/control-map contract. Worst case ~2.5 kΩ at
  mid-wiper is comfortable for the switched-cap input; a knob needs ~10–11 ENOB,
  far below the 16-bit settling bar. 100k pots (~25k source) would need
  DELAY + reservoir cap or a buffer — not V1.
- **DELAY as cheap insurance:** the per-conversion start DELAY fixes *external*
  node settling (pin/cable/reservoir cap charging through the source R before the
  modulator starts). With raw inputs (no pin cap) external settling is already
  sub-µs, so DELAY buys little *today*. Convenient result: the **reset default,
  `DELAY=000` = 14·t_MOD ≈ 54.7 µs, already *is* our small-non-zero insurance** —
  no register change needed, and the §4 per-hop math already includes it. (Min is
  the non-monotonic code `111` = 1·t_MOD ≈ 3.9 µs; max `110` = 4096·t_MOD ≈ 16 ms.
  Shortening to `111` would shave ~50 µs/hop — unnecessary, we have rate headroom.
  DELAY becomes the real lever the day a reservoir cap is added.)
- **Smoothness ≠ scan rate:** audible smoothness comes from firmware one-pole
  slewing of the control value between updates, not from sampling faster. Keeps
  the scan rate modest.
- **Rotary-switch ladders** are discrete DC — flag "slow," add hysteresis/debounce.

**Settling — how we actually bound it (honest note):** the switched-cap RC calc
`err = exp(−t_acq / ((Rs + Rsw)·Cs))` **cannot be filled from the datasheet** —
SBAS815A (and the ADS124S08 sibling) publish neither the input sampling
capacitance `Cs`, the switch resistance `Rsw`, nor the acquisition window
`t_acq`. Instead TI characterizes the bypassed input as a **leakage** spec:
**absolute input current 0.5 nA typ (PGA bypassed)**, differential 1 nA/V typ
(§7.5). Through a ~2.5 kΩ mid-wiper that's a ~1.3 µV drop — utterly negligible
vs a control-grade LSB. So the settling argument is: *leakage is tiny and the
source is low-Z*, and the **real proof is the empirical pot sweep** (§13), not a
formula. If we ever need the hard RC number (e.g. for a high-Z / reservoir-cap
variant), it comes from TI E2E or bench measurement, not the datasheet.

---

## 6. Register defaults (applied by `Ads114s08::Init`)

| Reg | Addr | Value | Meaning |
|-----|------|-------|---------|
| INPMUX   | 0x02 | (per scan) | MUXP=AINx, MUXN=AINCOM (single-ended) |
| PGA      | 0x03 | `0x00` | PGA_EN=00 (bypass, reset default), GAIN=000, DELAY=000 (14·t_MOD) |
| DATARATE | 0x04 | `0x1B` | FILTER=1 (low-latency) + DR=1011 (1000 SPS); reset default is 0x14 (FILTER=1, 20 SPS) |
| REF      | 0x05 | **0x30** | external REFP0/REFN0, buffers bypassed, int ref off |

PGA reg reset default is already `0x00` (bypass + default DELAY), so we
effectively only need to write DATARATE and REF beyond the per-scan INPMUX.
All other registers left at reset unless calibration is enabled (§9).

---

## 7. Scan state machine (Layer 2 `Update()`)

Non-blocking; called every main-loop iteration. Time-driven since DRDY# unwired.

```
state PER CHIP (U3, U4 advance in lockstep windows):
  IDLE        -> select next populated channel on BOTH chips (WREG INPMUX x2),
                 record t_start, go WAIT
  WAIT        -> if (now - t_start) >= t_settle(rate): go READ
  READ        -> RDATA U3 -> cache[ch_u3]; RDATA U4 -> cache[ch_u4];
                 advance channel cursor; go IDLE
```

- One `t_settle` window services one channel on *each* chip (2 channels total).
- Cursor walks only the populated-channel list.
- `Update()` never blocks: if the settle window hasn't elapsed it returns
  immediately. The audio callback / UI read the cache, never the bus.

---

## 8. Configuration surface

- **Populated-channel list** — bitmask or list per chip; scanner skips the rest
  (linear throughput win). Default: all 24.
- **Data rate** — default 1000 SPS; changeable (500 for more noise margin, up to
  4000 if channel count grows).
- **DELAY code** — small non-zero default; raise if a reservoir cap is added.
- **Per-channel smoothing coefficient** — one-pole; app or lib layer TBD.
- **Filter** — low-latency default; sinc3 selectable if noise rejection needed.

---

## 9. Calibration (optional, phase 2)

ADS114S08 supports self-offset (SFOCAL), system-offset (SYOCAL) and system-gain
(SYGCAL). For ratiometric pot reads absolute accuracy barely matters (endpoints
self-normalize via `GetNorm`), so V1 can skip calibration. Expose
`Ads114s08::Calibrate()` for future absolute-referenced inputs.

---

## 10. Error handling

- **Presence check at init** (write+readback) → `Present()`. `RodeoAdc::Init`
  logs which chips answered; a missing chip degrades to its 12 channels reading
  a sentinel rather than hanging.
- Optional integrity: the ADS114S08 can prepend a STATUS byte / append CRC to
  RDATA (SYS reg). Consider enabling STATUS for bring-up.

---

## 11. Future: absolute-referenced inputs (CV/jacks)

With the ref buffers bypassed, +3V3 noise couples straight into the reference
(no isolation). Harmless for pots (ratiometric — wiper and ref move together,
cancels), but it sets the noise floor for any absolute-referenced input. Revisit
reference filtering/buffering when CV/jack inputs are added. Not a V1 concern.

---

## 12. Datasheet numbers — resolved (SBAS815A)

- [x] **f_MOD** — fixed **256 kHz** (= f_CLK/16, §9.3.5), independent of data
      rate; DR is pure decimation. → lower SPS = noise averaging, *not*
      acquisition margin (§4).
- [x] **PGA CM headroom** — enabled: **±0.15 V** from each rail (Eq. 5) → would
      clip ~150 mV/end. Bypassed: **AVSS−0.05 V … AVDD+0.05 V** (Eq. 6) →
      rail-to-rail. Justifies bypass (§5, §6).
- [x] **DELAY[2:0]** — reset default `000` = **14·t_MOD ≈ 54.7 µs** (our
      insurance value, no change needed); min `111` = 1·t_MOD ≈ 3.9 µs (non-
      monotonic); max `110` = 4096·t_MOD ≈ 16 ms (§5).
- [~] **Cs / t_acq / Rsw** — **NOT published** in SBAS815A or the ADS124S08
      sibling. TI specs the bypassed input as leakage (0.5 nA typ) instead of an
      RC. So the switched-cap settling formula is un-fillable from the datasheet;
      we bound settling by "low-Z source + tiny leakage" and **prove it
      empirically** (§13). Hard RC number, if ever needed, from TI E2E / bench.

---

## 13. Validation plan

1. **Bench (tomorrow, w/ JST connectors):** sweep a 10k pot on one channel via
   `04_adc_1ch` (already carries the external-3.3V-ref fix). Transfer curve must
   be **monotonic, linear, endpoints hit 0 and full-scale**, no mid-scale
   compression. Compression/gain-droop = settling trouble; escalation order
   DELAY → reservoir cap + DELAY → daughterboard buffer.
2. **Driver proof:** bring up Layer 1 against one chip, verify presence check +
   single-channel read matches `04_adc_1ch`.
3. **Scanner proof:** enable both chips, confirm pipelined revisit rate matches
   the §4 table (~72 Hz/ch @ 1000 SPS) and all 24 channels track independent
   inputs.

---

## 14. References

- **SBAS815A** — ADS114S08/06 datasheet. Filter/settling: §9.3.6, Tables 11–13,
  15, Fig 72. Registers: §9.6 (INPMUX 02h, PGA 03h, DATARATE 04h, REF 05h).
  Scan/WREG restart: §9.5.3.12, §9.5.4.3, Fig 92. No-DRDY polling: §9.5.5.
- `firmware/common/board.h` — pin + channel-count source of truth.
- `firmware/examples/04_adc_1ch` — verified single-channel bring-up / proof harness.
