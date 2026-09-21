# rodeo-002 — firmware

Instance firmware for rodeo-002. Shares the platform's toolchain, vendored
libDaisy/DaisySP and build rules with `rodeo-motherboard/firmware` — each app
here just points `RODEO_ROOT` back at that tree.

| App | What it is |
|---|---|
| `bench_voices/` | Voice + texture-chain CPU benchmark. Answers "how many voices?" before the instrument is designed around a guess. |

## Building

Same toolchain as the motherboard firmware (see
[`rodeo-motherboard/firmware/README.md`](../../rodeo-motherboard/firmware/README.md)):
the official Arm GNU Toolchain with newlib, `GCC_PATH` set in
`rodeo-motherboard/firmware/config.mk`, and the submodules initialised
(`git submodule update --init --recursive`).

```bash
cd rodeo-002/firmware/bench_voices
make
make program-dfu   # Daisy in DFU first: hold BOOT, tap RESET
```

### The instrument runs under the Daisy bootloader

The Seed's internal flash is 128 KB and `bench_voices` alone uses ~90% of it, so the
instrument firmware is built as `APP_TYPE = BOOT_SRAM`: the Daisy bootloader copies it from
QSPI into SRAM at boot (see the concept doc, *Firmware platform*). One-time setup, then
updates without the BOOT button:

```bash
make program-boot                  # once: installs the Daisy bootloader (Daisy in ST DFU)
make APP_TYPE=BOOT_SRAM
make APP_TYPE=BOOT_SRAM program-dfu   # within 2 s of a reset, while the bootloader waits
```

`bench_voices` still defaults to plain internal flash, which is how its published numbers
were measured. **Re-run it once with `APP_TYPE=BOOT_SRAM`** to confirm those numbers carry
over before relying on them in that mode.

If you are building inside a git worktree where the submodules aren't checked
out, point at the main checkout instead of initialising them again:

```bash
make RODEO_ROOT=/path/to/rodeo/rodeo-motherboard/firmware
```

## `bench_voices` — what it measures and how to read it

For each candidate engine it holds a sustained chord at a rising voice count and
reports audio-callback load from libDaisy's `CpuLoadMeter`. The whole sweep runs
twice: voices alone, then voices plus the full texture chain (overdrive + fold,
chorus, bitcrush, and the granular delay with its 4-second SDRAM buffer,
configured to its worst case — long dense grains, shimmer pitch, feedback up).

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
- **One effect type per slot.** Character, Movement and Texture each implement
  the option I expect to be most expensive; the alternates should come in under
  these numbers. Space is implemented properly because it is the point.
- **Voices all run the same engine**, which is how the instrument works — engine
  is a per-patch choice.
- **Grain count is capped** at `kMaxGrains` (12), which is also what bounds the
  granular delay's worst-case cost.
