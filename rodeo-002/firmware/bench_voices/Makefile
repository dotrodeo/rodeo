# rodeo-002 voice/FX benchmark. Lives outside rodeo-motherboard/firmware, so it
# points RODEO_ROOT back at the firmware tree that owns the toolchain rules,
# the vendored libDaisy/DaisySP and common/board.h.

RODEO_ROOT := ../../../rodeo-motherboard/firmware

TARGET      = bench_voices
CPP_SOURCES = src/main.cpp

# rodeo-002 runs under the Daisy bootloader, so that is the default here too.
# APP_TYPE decides BOTH the memory map the program is linked for AND the address
# `program-dfu` writes it to, and the two must agree: a bootloader build flashed
# with the plain default lands on 0x08000000, overwrites the bootloader, and
# then crashes silently on boot. The bootloader and the ST ROM DFU also share
# a USB ID, so dfu-util reports success either way. Defaulting here means plain
# `make` and `make program-dfu` agree without anyone remembering a flag.
# Plain internal-flash builds: `make APP_TYPE=BOOT_NONE` (then also pass it to
# program-dfu -- and note that replaces the bootloader).
APP_TYPE ?= BOOT_SRAM

# Switching APP_TYPE doesn't make anything out of date, so make would happily
# keep the image linked for the OTHER memory map. Wipe the build whenever the
# mode changes. This also makes a mismatched `program-dfu` fail loudly -- there
# is no binary left to flash -- instead of writing to the wrong address.
BUILD_DIR ?= build
_LAST_APP_TYPE := $(shell cat $(BUILD_DIR)/.app_type 2>/dev/null)
ifneq ($(_LAST_APP_TYPE),$(APP_TYPE))
$(shell rm -rf $(BUILD_DIR))
endif
$(shell mkdir -p $(BUILD_DIR) && echo $(APP_TYPE) > $(BUILD_DIR)/.app_type)

include $(RODEO_ROOT)/common/rodeo.mk
