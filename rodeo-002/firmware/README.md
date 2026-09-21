# rodeo-002 — firmware

Instance firmware for rodeo-002. Shares the platform's toolchain, vendored
libDaisy/DaisySP and build rules with `rodeo-motherboard/firmware` — each app
here just points `RODEO_ROOT` back at that tree.

| App | What it is |
|---|---|
| `common/` | **Shared code** for every app and, later, the instrument: the voice engines (`engines.h`), the four texture-chain slots (`fx_character.h`, `fx_movement.h`, `fx_texture.h`, `fx_space.h`), the chain (`texture_chain.h`), helpers (`dsp_util.h`) — header-only and host-compilable — plus `app.mk`, the build rules every app includes. |
| `tests/` | Host-side GoogleTest for `common/`, against the real vendored DaisySP: every effect type at every corner must stay finite, bounded, quiet after the input stops, transparent at zero, and audible at full; plus a fuzz test that twiddles the whole chain at random. `make test`. |
| `bench_voices/` | CPU benchmark: the cost of every effect type, then voices per engine with the worst-case chain running. |
| `bench_space/` | Space-slot profiler (~40 s): splits Space's cost into ReverbSc, shimmer, pre-delay, grain engine (cost per grain) and running-at-mix-0, to decide which optimisation pays. |

## Building

Same toolchain as the motherboard firmware (see
[`rodeo-motherboard/firmware/README.md`](../../rodeo-motherboard/firmware/README.md)):
the official Arm GNU Toolchain with newlib, `GCC_PATH` set in
`rodeo-motherboard/firmware/config.mk`, and the submodules initialised
(`git submodule update --init --recursive`).

**rodeo-002 runs under the Daisy bootloader** (`APP_TYPE = BOOT_SRAM`): the Seed's 128 KB
internal flash is too small for the instrument, so the bootloader copies the program from QSPI
into SRAM at boot (see the concept doc, *Firmware platform*). Every app **defaults to that
mode** — the rule lives once, in `common/app.mk` — so plain `make` and `make program-dfu`
agree.

One-time setup — install the bootloader from the chip's ROM DFU:

```bash
make program-boot     # first: hold BOOT, tap RESET
```

Then, for every update:

```bash
cd rodeo-002/firmware/bench_voices
make
make program-dfu
```

**Before `program-dfu`, park the bootloader:** tap RESET, and while the LED is pulsing slowly,
tap BOOT once — a few rapid blinks confirm it will now wait indefinitely. Don't try to race
the bootloader's 2.5-second grace period instead: dfu-util can find the device and still lose
it mid-connect (`Cannot claim interface - LIBUSB_ERROR_NO_DEVICE`) when the window closes.
(Behaviour per libDaisy's `doc/md/_a7_Getting-Started-Daisy-Bootloader.md`.)

**Why `APP_TYPE` must match on build *and* flash.** It sets both the memory map the program is
linked for and the address `program-dfu` writes to. A bootloader build flashed as a plain one
lands on `0x08000000`, overwrites the bootloader, and crashes silently at boot — and dfu-util
reports success, because the bootloader and the chip's ROM DFU share a USB ID. That happened on
2026-09-21. The Makefile now wipes the build whenever `APP_TYPE` changes, so a mismatched flash
fails with a missing file instead. **Recovery if it happens anyway:** reinstall the bootloader
(`make program-boot` from ROM DFU), then flash normally.

Plain internal-flash builds are still available with `make APP_TYPE=BOOT_NONE` — pass it to
`program-dfu` as well, and note that it replaces the bootloader. The platform's bring-up
examples are plain builds, so **flashing any of them erases the bootloader** too.

`bench_voices` was first measured as a plain build, then **re-run under the bootloader on
2026-09-21: the numbers carry over, ~3% cheaper on average** (concept doc, *Resource budget*).
In that mode it sweeps to 12 voices rather than 16: the bootloader build puts zero-initialised
globals in the 128 KB DTCM, which 16 voices overflow.

If you are building inside a git worktree where the submodules aren't checked
out, point at the main checkout instead of initialising them again:

```bash
make RODEO_ROOT=/path/to/rodeo/rodeo-motherboard/firmware
```

## `bench_voices` — what it measures and how to read it

**First, the FX pass (~40 s).** Four cheap voices supply signal. Every
texture-chain slot is bypassed — which costs nothing — except the one under
test, pushed to its most expensive settings (full amount; for Space, long dense
pitched grains with feedback). Each effect type's cost is its load over the
all-bypassed baseline:

```
FX,slot,type,avg_pct,max_pct,cost_pct
```

Then the most expensive type from each slot runs all at once: **the worst-case
chain**.

**Then the engine sweep**: for each candidate engine it holds a sustained chord
at a rising voice count and reports audio-callback load from libDaisy's
`CpuLoadMeter`, twice — voices alone, then voices plus **the worst-case chain
measured above**. These are the real effects from `../common`, so the voice
counts are the ones the instrument will get.

The voice under test is the rodeo-002 voice from the control map: one engine →
`Svf` low-pass → amp ADSR, with a block-rate filter ADSR.

Output is CSV over the serial log at 115200. Each step announces itself first,
so if the board ever does wedge, the log's last line names what was playing:

```
RUN,engine,fx,voices
CSV,engine,fx,voices,avg_pct,max_pct
```

then a summary of the largest voice count that stayed under 70% average load.

A step that cannot run in real time reports `CSV,engine,fx,voices,OVER,OVER`
and the sweep moves to the next engine. This matters more than it sounds: an
overrunning audio callback starves every lower-priority interrupt including
SysTick, so `System::GetNow()` and `System::Delay()` stop advancing and the
main thread freezes mid-measurement with the chord still sounding. The first
hardware run died exactly that way, at step 24. The harness therefore clocks
itself from the audio callback (one block = 48 samples = 1 ms exactly) and the
callback drops all voices after 16 consecutive over-budget blocks, which frees
the CPU and lets the sweep continue.

**Read the 70% column, not the 90% one.** Nothing else runs during the sweep —
no 16-pot ADC scan over SPI, no encoder-hub I²C polling, no OLED redraw, no
MIDI. Those need real headroom, so 70% is the honest ceiling and 90% is the
cliff. Audio also plays out of the jacks while it runs, so you can hear each
engine being measured.

### Caveats worth knowing before trusting the numbers

- **Wavetable and Chord aren't in the sweep.** They still need porting from the
  Mutable Instruments source, so the benchmark cannot speak for them. They are
  likely the two most expensive engines in the concept's list.
- **Voices all run the same engine**, which is how the instrument works — engine
  is a per-patch choice.
- **Grain count is capped** at `kMaxGrains` (12), which is also what bounds the
  granular delay's worst-case cost.
