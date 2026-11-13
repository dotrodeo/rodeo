# Rodeo host-side unit tests

Fast, native unit tests for the **pure logic** in the firmware drivers — the
math and state machines that don't touch hardware. They compile for your **Mac
(host)** with `clang++`, not for the STM32, so there's no board, no flashing,
and a run takes milliseconds.

## Run them

```sh
cd rodeo-sdk/common/tests
make test      # build + run everything
make clean
```

You should see GoogleTest print each test and a final `[  PASSED  ]` line.

```sh
make usb-desc          # every firmware's USB descriptors against usb_desc/golden/
make usb-desc-update   # rewrite golden/ when a change is meant; review the diff
```

`usb-desc` needs the tinyusb submodule, so it isn't part of `make test`; CI runs
both.

## What lives here

| File | Purpose |
|------|---------|
| `Makefile` | Host build. Reuses the googletest vendored in `libDaisy/tests` — no new dependency. |
| `gtest_all.cpp` / `gtest_main.cpp` | GoogleTest framework + entry point (compiled once). |
| `adc_convert_gtest.cpp` | Tests the ADC value pipeline (`common/adc_convert.h`). |
| `pot_ui_gtest.cpp` | Tests the panel logic (`common/pot_ui.h`): stepped-pot hysteresis and the knob-activity watcher that drives the OLED popup in `09_poly_synth_oled`. |
| `svf_stability_gtest.cpp` | Regression guard for the DaisySP `Svf` blow-up that crashed 06/09/rodeo-001. Compiles the **real** vendored DaisySP (see `DSP_SOURCES` in the Makefile) because what it guards is DaisySP's numerical behaviour, not our arithmetic. |
| `usb_desc/` | What every firmware tells a computer over USB (`common/usb_desc.c` with each firmware's `tusb_config.h`), byte for byte: `check.sh` calls TinyUSB's descriptor callbacks on the host, in normal and disk mode, and compares with `golden/`. The golden files began as the descriptors the firmwares had before the SDK took them over, which had run on hardware. |
| `pot_span_gtest.cpp` | Tests the pot span calibration (`rodeo::PotSpan` in `common/adc_convert.h`) that stops every knob reading 91 % because of the satellite's 1 kΩ top-rail resistor. |

## The rule that makes firmware testable

Unit tests can only run code that **doesn't call the chip**. So the pattern is:
pull the pure logic (a clamp, a code→volts formula, an enum step) into a small
dependency-free header, have the driver delegate to it, and test that header
here. `common/adc_convert.h` is the worked example — `RodeoAdc::GetCode/GetVolts
/GetNorm` delegate straight to it, so these tests guard the **real** firmware
path, not a copy.

Hardware I/O (SPI transfers, GPIO, `libDaisy .Init()`) is **not** tested here —
that's bench bring-up. The one deliberate exception is `svf_stability_gtest.cpp`:
DSP has no hardware dependency, so a numerical-stability bug that only shows up
as a dead voice on the bench *can* be caught here in a second. To test a driver's *sequencing* logic later, put the SPI
call behind a small interface and feed a fake from the test.

## Add a test

1. Extract the pure logic into a header in `common/` if it isn't already.
2. Add `something_gtest.cpp` here with `TEST(Group, Name) { EXPECT_*(...); }`.
   The Makefile globs `*.cpp`, so no Makefile edit is needed.
3. `make test`.
