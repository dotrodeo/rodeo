# rodeo-encoder-hub firmware — STM32C031C6 (Cortex-M0+), bare-metal + STM32Cube LL.
# make        -> build/rodeo-encoder-hub.elf (+ .bin, size)
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

clean:
	rm -rf $(BUILD)

.PHONY: all clean size
