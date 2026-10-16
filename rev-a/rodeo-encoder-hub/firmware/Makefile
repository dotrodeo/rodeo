# rodeo-encoder-hub firmware — STM32C031C6 (Cortex-M0+), bare-metal + STM32Cube LL.
# make            -> build/rodeo-encoder-hub.elf (+ .bin, size)
# make flash      -> flash over SWD with probe-rs   (see README "Flashing")
# make flash-ocd  -> same, via OpenOCD
# make probe-check       -> prove the probe enumerates and the target answers
# make probe-check-reset -> same, connecting under reset (rescue path)
# make clean

TARGET  = rodeo-encoder-hub
BUILD   = build

# Toolchain: set GCC_PATH to the ARM GNU Toolchain bin dir (with trailing slash)
# if arm-none-eabi-gcc is not on PATH, e.g.
#   make GCC_PATH=/Applications/ArmGNUToolchain/14.3.rel1/arm-none-eabi/bin/
# Or drop it in a gitignored config.mk (GCC_PATH := .../bin/).
-include config.mk
GCC_PATH ?=
PREFIX  = arm-none-eabi-
CC      = $(GCC_PATH)$(PREFIX)gcc
OBJCOPY = $(GCC_PATH)$(PREFIX)objcopy
SIZE    = $(GCC_PATH)$(PREFIX)size

# ---- MCU ----
CPU = -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft

# ---- paths ----
V   = vendor
CMSIS_CORE = $(V)/CMSIS/Core/Include
CMSIS_DEV  = $(V)/CMSIS/Device/ST/STM32C0xx
LL         = $(V)/STM32C0xx_HAL_Driver

INCLUDES = \
  -Isrc -Iinclude \
  -I$(CMSIS_CORE) \
  -I$(CMSIS_DEV)/Include \
  -I$(LL)/Inc

DEFINES = -DSTM32C031xx -DUSE_FULL_LL_DRIVER

# ---- sources ----
C_SOURCES = \
  src/main.c \
  src/hub.c \
  src/encoder.c \
  src/i2c_target.c \
  $(CMSIS_DEV)/Source/system_stm32c0xx.c \
  $(LL)/Src/stm32c0xx_ll_gpio.c \
  $(LL)/Src/stm32c0xx_ll_i2c.c \
  $(LL)/Src/stm32c0xx_ll_rcc.c \
  $(LL)/Src/stm32c0xx_ll_utils.c

ASM_SOURCES = $(CMSIS_DEV)/Source/startup_stm32c031xx.s

LDSCRIPT = STM32C031C6.ld

# ---- flags ----
OPT = -Og -g3
CFLAGS = $(CPU) $(DEFINES) $(INCLUDES) $(OPT) \
  -Wall -Wextra -ffunction-sections -fdata-sections -fno-common -std=gnu11
ASFLAGS = $(CPU) $(OPT) -Wall -ffunction-sections -fdata-sections
LDFLAGS = $(CPU) -T$(LDSCRIPT) -specs=nano.specs -specs=nosys.specs \
  -Wl,--gc-sections -Wl,--no-warn-rwx-segments \
  -Wl,-Map=$(BUILD)/$(TARGET).map,--cref -lc -lm -lnosys

OBJECTS  = $(addprefix $(BUILD)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin size

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD)/%.o: %.s | $(BUILD)
	$(CC) -c $(ASFLAGS) $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

size: $(BUILD)/$(TARGET).elf
	$(SIZE) $<

$(BUILD):
	mkdir -p $(BUILD)

# ---- flashing (SWD) ----
# There is no USB DFU on this part, so firmware goes in over SWD through the
# 5-pin J12 header. Pin order is GND/VDD/SWDIO/SWCLK/NRST, pin 1 first, and pin 1
# is the SQUARE pad — there are no silkscreen labels, so counting from the wrong
# end is easy and silent (SWDIO is dead centre, so it lands right either way).
# ANY CMSIS-DAP probe works — including a
# Raspberry Pi Pico or Pico 2 running Raspberry Pi's `debugprobe` firmware,
# which is the cheapest way in if you have no ST-Link. Wiring and first-time
# setup are in README.md "Flashing".
#
# Use probe-rs, NOT the Homebrew OpenOCD: STM32C0 flash support landed in
# OpenOCD *after* the 0.12.0 release, whose stm32l4x driver covers only
# STM32G0/G4/L4/L4+/L5/U5/WB/WL. Dropping in a newer stm32c0x.cfg does not help
# — the config resolves but `flash probe` then fails to identify the device.
CHIP       = STM32C031C6
OPENOCD_IF = interface/cmsis-dap.cfg

# probe-rs knows this part by name, so this never auto-detects. --reset runs the
# firmware once the write finishes (probe-rs 0.32: `download` takes --reset;
# there is no --start). --verify reads the flash back afterwards — worth the
# seconds it costs, because a pogo contact that drops mid-write otherwise fails
# silently and only shows up as a board that misbehaves later.
flash: $(BUILD)/$(TARGET).elf
	probe-rs download --reset --verify --chip $(CHIP) $<

# Connectivity check — run this BEFORE trusting a flash. Two stages that fail
# for different reasons: `list` proves the PROBE enumerates over USB, `info`
# proves the TARGET answers on SWD. With a hand-held pogo probe the usual fault
# is contact, not configuration.
#
# --verbose is REQUIRED here, not cosmetic. Plain `probe-rs info` tries to
# auto-detect the chip against probe-rs's registry and gives up on an STM32C0
# with "The connected chip could not automatically be determined" EVEN WHEN THE
# WIRING IS PERFECT. --verbose skips the registry and enumerates the debug port
# directly, which is the question this check is actually asking. (Flashing is
# unaffected: `make flash` passes --chip explicitly, so it never auto-detects.)
#
# If it reports no target, add --connect-under-reset — that is what the NRST
# wire is for, and it is the rescue path if firmware ever holds the bus busy.
probe-check:
	probe-rs list
	probe-rs info --protocol swd --verbose

# Rescue path for probe-check. --connect-under-reset holds the part in reset
# while the probe attaches, which is what the NRST wire buys you: it gets in
# even if firmware is holding the bus busy.
probe-check-reset:
	probe-rs list
	probe-rs info --protocol swd --verbose --connect-under-reset

# OpenOCD alternative. Requires an OpenOCD NEWER than 0.12.0 (build from master,
# or any release that lists STM32C0 in `device_families`) — see the note above.
flash-ocd: $(BUILD)/$(TARGET).elf
	openocd -f $(OPENOCD_IF) -f target/stm32c0x.cfg \
	  -c "program $< verify reset exit"

clean:
	rm -rf $(BUILD)

.PHONY: all clean size flash flash-ocd probe-check probe-check-reset
