# Rodeo host-side unit tests

Fast, native unit tests for the **pure logic** in the firmware drivers — the
math and state machines that don't touch hardware. They compile for your **Mac
(host)** with `clang++`, not for the STM32, so there's no board, no flashing,
and a run takes milliseconds.

## Run them

```sh
cd rodeo-motherboard/firmware/common/tests
make test      # build + run everything
make clean
```

You should see GoogleTest print each test and a final `[  PASSED  ]` line.

## What lives here

| File | Purpose |
|------|---------|
| `Makefile` | Host build. Reuses the googletest vendored in `libDaisy/tests` — no new dependency. |
| `gtest_all.cpp` / `gtest_main.cpp` | GoogleTest framework + entry point (compiled once). |
| `adc_convert_gtest.cpp` | Tests the ADC value pipeline (`common/adc_convert.h`). |

## The rule that makes firmware testable

Unit tests can only run code that **doesn't call the chip**. So the pattern is:
pull the pure logic (a clamp, a code→volts formula, an enum step) into a small
dependency-free header, have the driver delegate to it, and test that header
here. `common/adc_convert.h` is the worked example — `RodeoAdc::GetCode/GetVolts
/GetNorm` delegate straight to it, so these tests guard the **real** firmware
path, not a copy.

Hardware I/O (SPI transfers, GPIO, `libDaisy .Init()`) is **not** tested here —
that's bench bring-up. To test a driver's *sequencing* logic later, put the SPI
call behind a small interface and feed a fake from the test.

## Add a test

1. Extract the pure logic into a header in `common/` if it isn't already.
2. Add `something_gtest.cpp` here with `TEST(Group, Name) { EXPECT_*(...); }`.
   The Makefile globs `*.cpp`, so no Makefile edit is needed.
3. `make test`.
