# Rodeo Motherboard — Verified Hardware Map

Derived from `kicad/rodeo-motherboard.kicad_pcb` in the **main checkout** (the maintained,
complete artifact — 87 components). The root schematic does not instantiate the
subsheets, so a schematic netlist only sees the root sheet; trust the PCB.

libDaisy `seed::Dxx` ↔ STM32 mapping is carried from ElectroSmith's original
Daisy Seed pinout; the `seed::` numbering is identical across Seed rev4 and the
Seed2 DFM for D0–D30.

**This describes the board AS LAID OUT, including a wiring error — see Issue 1.**

---

## Module: Seed3, on the classic Daisy Seed 40-pin footprint

The placed footprint is `A1 = Electrosmith_Daisy_Seed_Rev4`
(`Module:Electrosmith_Daisy_Seed`) — the classic 40-pin castellated Daisy Seed
layout (single 2×20 header, single-ended line audio). **Per the designer, the
Seed3 is pin-compatible with this footprint** (its pinout matches the
non-DFM Seed), so the footprint choice is intentional and correct.

Consequences vs. the earlier Seed2-DFM design, all *good* for us:
* One 40-pin strip instead of five 2×5 banks — pin numbers are the familiar
  Daisy Seed 1–40, no bank-ordering ambiguity.
* Accepts **5 V on VIN**, so feeding VIN from USB VBUS is in-spec (the DFM's
  6.1 V minimum, previously a concern, no longer applies).
* Audio is single-ended and wired straight through (the DFM bank-D reversal is
  gone — see Audio below).

Firmware assumption: the Seed3 enumerates as a standard `DaisySeed` (same
STM32H7 + libDaisy `seed::` map). That follows from pinout compatibility; worth
a one-line confirmation once you have libDaisy building for it.

---

## Power (new on this revision)

USB-C (J1) VBUS → `VIN`, then an on-carrier LDO makes the 3.3 V rail:

```
USB-C VBUS 5V ─┬─► VIN ─┬─► Daisy A1 pin39 (VIN, Daisy's own reg → Daisy 3V3, unused externally)
               │        ├─► U2 IN + EN
               │        ├─► U1 (6N138) VCC
               │        └─► J45–J49 pin3 (power taps)
               │
        U2 TLV76733 (WSON-6, fixed 3.3V, 1A)
        EN←VIN (always on)   SNS←OUT (local sense)
               │
              +3V3 ─► both ADCs (AVDD/DVDD/IOVDD/REFP0/START), all 24 pots,
                      expansion headers, MIDI pull-ups, J45–J49 pin2
```

* **U2 = TLV76733DRVx**, 5 V→3.3 V LDO. IN/EN/SNS/caps all correctly connected.
  Input: C12 (22 µF) + C1/C3. Output: C9 (1 µF) + per-chip decoupling. Dropout
  and thermal are negligible at this load (~tens of mA). Looks correct.
* **The pot supply and the ADC reference are the same U2 3.3 V rail** — so pot
  readings are ratiometric and rail variation cancels. This is exactly what a
  control scanner wants.
* **Daisy's own 3.3 V outputs (pin21 `3V3_A`, pin38 `3V3_D`) are left
  unconnected** — the carrier rail is fully independent of the module. Clean.
* CC1/CC2 have 5.1 K pulldowns (R6/R8) → advertises as a 5 V sink. D1 = TVS on VIN.

### "Power jumpers" — not found as components

There are **no jumper components** in the design — no `SolderJumper`, `NetTie`,
`Net_Tie`, or `JP*`/`SB*` parts in schematic or PCB ("jumper" appears only as
the KiCad `duplicate_pin_numbers_are_jumpers` syntax token). The only new
power-related parts are five **3-pin JST-ZH connectors J45–J49**, each
`1=GND, 2=+3V3, 3=VIN` — power *taps/distribution*, not selectable jumpers.
If the intent was a rail-select or source-select jumper, it isn't on the board
yet. **Reconcile with what you thought you added.**

---

## SPI — ADS114S08 bus (U3 = ADC A, U4 = ADC B)

| Signal | Daisy pin | silk | libDaisy | STM32 | AF role |
|---|---|---|---|---|---|
| `SPI_SCK`  | 9  | SPI1_SCK  | `seed::D8`  | PG11 | SPI1_SCK |
| `SPI_MOSI` net | 10 | SPI1_POCI | `seed::D9`  | PB4  | **SPI1_MISO** |
| `SPI_MISO` net | 11 | SPI1_PICO | `seed::D10` | PB5  | **SPI1_MOSI** |
| `SPI_CS`   | 8  | SPI1_CS   | `seed::D7`  | PG10 | dead-ends (unused, harmless) |
| `SPI_ADC1_CS` | 32 | SAI2_SD_B | `seed::D25` | PA0  | ADC A (U3) CS, GPIO |
| `SPI_ADC2_CS` | 33 | SAI2_SD_A | `seed::D26` | PD11 | ADC B (U4) CS, GPIO |

Both ADCs share SCK/MOSI/MISO through 33 Ω series resistors (R13/R14/R16) and
are individually chip-selected. Use `NSS::SOFT` and drive PA0/PD11 as GPIO.

### ADS114S08 configuration (unchanged, correct)

* `AVDD/DVDD/IOVDD = +3V3` (U2 rail), `AVSS/REFN0 = GND`, `REFP0 = +3V3` →
  external reference on the U2 3.3 V rail.
* `CLK` (pin17) → GND → internal oscillator.
* `START/SYNC` (pin8) → +3V3 → **continuous conversion**; firmware never pulses START.
* `RESET#` pulled high via 10 K (R12 for U3, R15 for U4); no MCU control.
* **`DRDY#` (pin13) unconnected on both parts**, and `DOUT/DRDY#` (pin12) is
  shared on `ADC_MISO`. No usable data-ready line → poll on a fixed cadence with
  `RDATA`.

### Analog channel map (unchanged)

Front-panel pots are 3-pin JST (`1=+3V3, 2=signal, 3=GND`):

| Connector | Net | ADC | Channel |
|---|---|---|---|
| J6 – J17  | `ADC1_1` – `ADC1_12` | A (U3) | AIN0 – AIN11 |
| J18 – J29 | `ADC2_1` – `ADC2_12` | B (U4) | AIN0 – AIN11 |

`ADCn_k` → `AIN(k−1)`, uniformly.

#### Pot cables: STRAIGHT THROUGH. Both ends are pinned the same way.

Checked against both schematics on 2026-10-01, because rodeo-002 spent a session
on it and its firmware briefly claimed the opposite:

| | pin 1 | pin 2 | pin 3 |
|---|---|---|---|
| motherboard `J6`–`J29` | `+3V3` | signal | `GND` |
| `rodeo-potentiometer` `J1` | supply in (→ 1 K → pot top leg, 100 n to GND) | wiper out | `GND` |

**They agree, so pin 1 goes to pin 1.** The satellites are *not* mirrored and a
reversed cable is *not* required.

What a reversed cable does, since one harness was built that way: supply and
ground swap, the same divider runs from the other end, and the travel becomes
**~0.30 V … 3.30 V** instead of **0.00 V … ~3.00 V**. The knob can no longer
reach 0 V and can now reach the full 3.3 V, which correct wiring cannot — the
1 K series resistor forbids it even for a +20 % pot (3.3 × 12/13 = 3.046 V).
That asymmetry is the cheapest way to tell the two apart in firmware.

Nothing is damaged by it: the satellite is passive. rodeo-002 detects and
rescales rather than rebuilding sixteen cables (`rodeo-002/firmware/common/panel_pot.h`),
but that tolerates a **mistake**, not a design, which is why it is not in this
library.

#### The 1 K series resistor means a pot never reaches 3.3 V

The satellite feeds the pot's top leg through 1 K, so full travel is
3.3 × Rpot/(Rpot + 1 K) — **~3.00 V for a nominal 10 K pot, not 3.3 V**.
Uncorrected, every knob tops out at 91 %. `adc_convert.h`'s `AdcCodeToNormSpan`
is what accounts for it; `common/tests/pot_span_gtest.cpp` guards it.

---

## Digital I/O

`DIG_1`–`DIG_12` → 2-pin JST (`1=signal, 2=GND`), J30–J41. No 3.3 V on these.

| Net | Daisy pin | silk | libDaisy | STM32 | Notes |
|---|---|---|---|---|---|
| `DIG_1`  | 2  | SD_DATA_3 | `seed::D1`  | PC11 | digital |
| `DIG_2`  | 3  | SD_DATA_2 | `seed::D2`  | PC10 | digital |
| `DIG_3`  | 4  | SD_DATA_1 | `seed::D3`  | PC9  | digital |
| `DIG_4`  | 5  | SD_DATA_0 | `seed::D4`  | PC8  | digital |
| `DIG_5`  | 6  | SD_CMD    | `seed::D5`  | PD2  | digital |
| `DIG_6`  | 7  | SD_CLK    | `seed::D6`  | PC12 | digital |
| `DIG_7`  | 22 | ADC_0     | `seed::D15` / `A0` | PC0 | ADC-capable |
| `DIG_8`  | 23 | ADC_1     | `seed::D16` / `A1` | PA3 | ADC-capable |
| `DIG_9`  | 24 | ADC_2     | `seed::D17` / `A2` | PB1 | ADC-capable, **3.3 V-tolerant only** |
| `DIG_10` | 25 | ADC_3     | `seed::D18` / `A3` | PA7 | ADC-capable, **3.3 V-tolerant only** |
| `DIG_11` | 26 | ADC_4     | `seed::D19` / `A4` | PA6 | ADC-capable |
| `DIG_12` | 27 | ADC_5     | `seed::D20` / `A5` | PC1 | ADC-capable |

`DIG_1`–`DIG_6` are on the SDMMC pins (digital only, fine for buttons/encoders);
`DIG_7`–`DIG_12` are ADC-capable, so they double as extra analog inputs.

### Status LED

`DAISY_STATUS` → Daisy pin28 (`ADC_6` / `seed::D21` / PC4) → R10 (1 K) → LED D5 →
GND. Active high. The module's own user LED (`hw.SetLed()`) also works.

---

## Audio (now correct — the old reversal is gone)

Single-ended, via 3.5 mm TRS jacks.

| Jack | Function | Wiring |
|---|---|---|
| J4 | Line IN  | Tip → R5 100 Ω → `AUDIO_IN_L` (pin16); Ring → R4 100 Ω → `AUDIO_IN_R` (pin17); Sleeve GND |
| J5 | Line OUT | Tip → `AUDIO_OUT_L+` (pin18); Ring → `AUDIO_OUT_R+` (pin19); Sleeve GND |

`AUDIO_IN_L/R` → Daisy 16/17, `AUDIO_OUT_L/R` → Daisy 18/19, all correct. The
`+` suffix on the out nets is cosmetic leftover from the DFM's differential
naming; the original Seed's outputs are single-ended.

---

## MIDI (correct)

TRS Type A on 3.5 mm jacks. USART1 (`seed::D13`/PB6 TX, `seed::D14`/PB7 RX) —
libDaisy's default `MidiUartTransport` config, no pin overrides needed.

| Jack | Function | Wiring |
|---|---|---|
| J2 | MIDI IN  | Tip/Ring → 6N138 (U1) opto via R1 220 Ω, D2 1N4148 clamp; opto out (pin6) → `UART_RX`, pulled to +3V3 by R11 470 Ω |
| J3 | MIDI OUT | Tip → R3 220 Ω → `UART_TX`; Ring → R2 220 Ω → +3V3; Sleeve GND |

U1 VCC is on VIN (5 V); its open-collector output is pulled to +3V3, so the
Daisy sees 3.3 V logic. Fine.

---

## Expansion headers

| Header | Type | Signals |
|---|---|---|
| J42 | I²C  | +3V3, GND, `I2C_SDA` (`seed::D12`/PB9), `I2C_SCL` (`seed::D11`/PB8) |
| J43 | SPI  | +3V3, GND, `SPI_MOSI`, `SPI_MISO`, `SPI_SCK` |
| J44 | UART | +3V3, GND, `UART_TX`, `UART_RX` |

The SPI header shares the ADC bus and has no dedicated chip select. The UART
header is the same USART1 as MIDI — only one of the two can be used at a time.

---

## Issues found

### 1. RESOLVED AS BUILT — SPI MOSI/MISO were swapped in the layout

The ADC data lines are crossed at the Daisy header:

| Copper path | Lands on | STM32 AF | Correct? |
|---|---|---|---|
| ADC `DIN` (U3/U4 pin10) ─ R16 ─ `SPI_MOSI` net ─ Daisy pin10 | `seed::D9` / PB4 | **SPI1_MISO** (MCU input) | ✗ DIN needs the MCU's *output* |
| ADC `DOUT` (U3/U4 pin12) ─ R13 ─ `SPI_MISO` net ─ Daisy pin11 | `seed::D10` / PB5 | **SPI1_MOSI** (MCU output) | ✗ two outputs fight when a CS is asserted |

The net *names* are swapped relative to the Daisy's pin functions — the label
"SPI_MOSI" was attached to the Daisy's POCI (=MISO) pin. On the STM32H7, SPI1
pin functions are fixed: PB4 = MISO, PB5 = MOSI; there is no alternate-function
that swaps them. So with hardware SPI:

* the MCU can never drive `DIN`, so it can never send `RDATA`/config commands;
* the MCU's MOSI output is short-circuited against the ADCs' `DOUT` output.

**Firmware cannot work around this.** Options were:
1. **Board fix (recommended):** swap the two Daisy-side connections — route
   `ADC_MOSI` (DIN) to Daisy pin11/PB5 and `ADC_MISO` (DOUT) to pin10/PB4. Two
   short reroutes near the header, or swap where R13 and R16 land.
2. Bit-bang SPI on plain GPIO (slow, not worth it here).

**Option 1 is what the board in hand has.** Three pieces of evidence, all
pointing the same way:

* `firmware/common/board.h` carries the *corrected* mapping —
  `kSpiMiso = 10 // seed::D9 (PB4) <- ADC DOUT`,
  `kSpiMosi = 11 // seed::D10 (PB5) -> ADC DIN`.
* `firmware/README.md`: the SPI orientation and conversion path are
  **bench-confirmed on real hardware** (`04_adc_1ch`, 2026-08-14).
* rodeo-002 has been reading sixteen pots through these ADCs since September.

This section read **"CRITICAL … the MCU cannot talk to the ADCs … fix before
fab"** until 2026-10-01, long after all three of those were true, so anyone
reading the doc would have concluded the hardware was dead. The analysis above
is kept because it is why the fix was needed; only the status was wrong.

**Still worth recording:** *how* it was resolved — a corrected layout before
fab, or a reroute on the board — is not written down anywhere. Whoever knows
should add a line.

### 2. USB-C is still power-only, and the Daisy's USB pins now dead-end

`J1` D+/D− (A6/B6, A7/B7) are unconnected, and `USB_D+`/`USB_D−` terminate at
Daisy pins 37/36 with nothing else on them. No USB data through the panel
connector — no USB-MIDI/audio and no DFU flashing via USB-C.

USB device functionality is still available through the **Daisy's own onboard
micro-USB**, and flashing works over that port (or SWD). So this is only a
problem if you specifically wanted panel-mounted USB. Given the project brief
lists USB as an MCU responsibility, worth a decision: wire J1 D+/D− to Daisy
37/36, or accept "use the module's port."

### 3. Schematic hierarchy doesn't include the subsheets (pre-existing)

The root `rodeo-motherboard.kicad_sch` instantiates no child sheets, so a schematic-only
netlist sees just the root (47 parts) while the PCB has all 87. This was already
true on the original board, so it looks like an established workflow rather than
a regression — but ERC and "update PCB from schematic" won't behave. Flag only;
the PCB is the source of truth here.

---

### 4. REV-B REVIEW — audible supply/ground coupling into the audio output

**Symptom** (rodeo-002, 2026-09-24, Derek): a high-pitched hum on the line out
whenever a MIDI key is held, **stacking with each additional key**. Powered from
a USB brick.

**It is not the firmware.** Two measurements say so:

* Sustained notes rendered on the host through the whole signal path — voices
  plus the texture chain, four engines — contain **no component that stays at a
  fixed frequency when the note changes**, down to -80 dB. Everything moves with
  the pitch. Rendering at a 16-sample block instead of 48 gives an identical
  spectrum, so the control grid is not leaking in either.
* On hardware, **the hum persists with master volume at zero**. Volume is
  applied in software, so the codec is being fed digital silence while the
  voices keep running. Whatever is audible is not in the audio data.

**Mechanism — hypothesis, consistent with the evidence but NOT yet scoped.**
The audio callback does a block's work in a burst and then idles, so the MCU's
current draw is a **1 kHz pulse train whose amplitude scales with the number of
voices**. Two paths lead from there to the jack, and this board has both:

* **Through VIN.** `USB-C VBUS → VIN → Daisy pin 39`, and the Seed makes its own
  analog 3.3 V for the codec from that pin — `3V3_A`/`3V3_D` are outputs, left
  unconnected here, so the codec's analog rail cannot be fed or filtered
  externally. The pulsed draw is pulled through the USB cable and brick, whose
  impedance and regulation bandwidth turn it into ripple on VIN; whatever the
  Seed's internal LDO does not reject at 1 kHz and its harmonics lands on the
  codec's analog supply.
* **Through ground.** The outputs are single-ended and **J5's sleeve is the
  shared board GND**, the same return carrying ADC SPI bursts, I²C to the hub
  and OLED, the MIDI opto and the pot supply. Any IR drop between the codec's
  ground reference and the jack sleeve sits in series with the output signal.

Which path dominates cannot be settled from the firmware side. Both are worth
addressing and both are cheap.

**Bench tests that apportion the blame before committing to a respin:**

1. **Run from a USB power bank.** A large drop points at the brick and cable
   impedance rather than the board.
2. **Tack a 220–470 µF low-ESR cap across VIN/GND at A1.** If that kills it, the
   fix is local bulk decoupling and nothing more.
3. **Scope VIN (AC-coupled) at the Seed** while holding one key, then six. Look
   for ripple at 1 kHz and multiples that grows with voice count.
4. **Unplug the hub and OLED** (the panel dies, audio does not). Apportions the
   I²C share of the return current.
5. Firmware can supply a **constant-CPU-load build** — all eight voices always
   rendered, unused ones gated silent. If the hum stops tracking the key count,
   the load → supply path is proven outright.

**For Rev B, review:**

* **Local bulk capacitance at the Seed's VIN/GND pins** — the cheapest and most
  likely sufficient change. It keeps the 1 kHz pulse loop local instead of
  letting it run out through the USB cable.
* **Ground topology around the audio jacks.** Return J4/J5 sleeves to the Seed's
  ground reference by a dedicated path rather than through the pour that carries
  ADC, I²C and MIDI return current, and keep those loops away from it.
* **Output series resistance.** J4 (line in) has 100 Ω per leg; **J5 has none** —
  tip goes straight to the Daisy pin. A series R (and optionally a shunt cap)
  gives a defined output impedance and keeps digital hash off the tip. It will
  not fix supply-borne hum on its own.
* **A ferrite or LC between the USB connector and VIN**, so the board's pulsed
  draw is not shared back onto VBUS.
* Whether a **clean-supply input option** (barrel jack, or documented guidance
  that the brick's impedance matters) belongs on the board.

---

## Firmware-relevant summary

* **Issue 1 is resolved as built** (see above); ADC bring-up is done and
  rodeo-002 ships on it. The paragraph below is the historical note from when
  it was still open.
* ~~**Blocked by Issue 1:** all ADC bring-up (milestones 1–5, 7) until the SPI
  data lines are un-swapped in hardware.
* **Unblocked:** audio (milestone 6) and MIDI are correctly wired on this
  revision; power is sound and in-spec for the original Seed.
* Firmware pin constants when the board is fixed: SPI1 SCK `seed::D8`,
  MISO `seed::D9`, MOSI `seed::D10`; ADC A CS `seed::D25` (PA0), ADC B CS
  `seed::D26` (PD11); MIDI USART1 default pins; I²C `seed::D11`/`D12`.
* No pins need reserving for a miswired audio bank anymore (that was a
  DFM-board artifact).
