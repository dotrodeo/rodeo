# Rodeo 001 firmware — target build (Daisy Seed / STM32H750, arm-none-eabi)
#
# Usage:
#   make deps        # build libDaisy + DaisySP static libs (first time / after update)
#   make             # build firmware -> build/rodeo-001.{elf,bin}
#   make program-dfu # flash over the Daisy's onboard micro-USB (hold BOOT, tap RESET)
#   make host-test   # run native host unit tests
#   make clean

TARGET = rodeo-001

# Optional local toolchain config (gitignored). On macOS the Homebrew
# arm-none-eabi-gcc ships without newlib; point GCC_PATH at the ARM GNU
# Toolchain in config.mk (see README) so plain `make` works.
-include config.mk

CPP_SOURCES = \
src/main.cpp \
src/platform/system.cpp \
src/engine/voice.cpp \
src/engine/synth.cpp \
src/midi/midi_engine.cpp

# Make our own headers ("platform/board.h", ...) visible to the compiler.
C_INCLUDES += -Isrc

LIBDAISY_DIR = libDaisy
DAISYSP_DIR  = DaisySP

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# --- convenience targets (added after the core include) --------------------
deps:
	$(MAKE) -C $(LIBDAISY_DIR) $(if $(GCC_PATH),GCC_PATH=$(GCC_PATH))
	$(MAKE) -C $(DAISYSP_DIR) $(if $(GCC_PATH),GCC_PATH=$(GCC_PATH))

host-test:
	$(MAKE) -C tests

.PHONY: deps host-test
