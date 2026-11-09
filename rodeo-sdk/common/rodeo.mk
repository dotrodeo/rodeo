# common/rodeo.mk — shared build rules for every app built on the Rodeo SDK.
#
# Each app / example Makefile defines TARGET and CPP_SOURCES, then includes this
# file. It wires up the vendored libDaisy + DaisySP, the shared Rodeo headers
# (common/board.h — the pin map), and the local toolchain path from config.mk.
#
#   TARGET      = my_app
#   CPP_SOURCES = src/main.cpp
#   include ../../common/rodeo.mk
#
# RODEO_ROOT is the rodeo-sdk/ root. The default suits an app two levels down
# (examples/<name>/); anywhere else sets it explicitly, as the template and the
# instruments do.

RODEO_ROOT ?= ../..

# Local, machine-specific toolchain path (GCC_PATH). Gitignored — see README.
-include $(RODEO_ROOT)/config.mk

# Rodeo shared headers. board.h is the single source of truth for the pin map.
C_INCLUDES += -I$(RODEO_ROOT)/common

# The host protocol (common/host.h) is on wherever an app uses it; RODEO_HOST=0
# compiles it to nothing, leaving the calls in place. As tinyusb.mk does for the
# port, a stamp in build/ says which it was built with, and switching rebuilds
# from scratch (keeping the other stamps, so they don't rebuild it again).
RODEO_HOST ?= 1
C_DEFS     += -DRODEO_HOST=$(RODEO_HOST)
RODEO_HOST_STAMP := build/.host-$(RODEO_HOST)
ifneq ($(filter-out $(RODEO_HOST_STAMP),$(wildcard build/.host-*)),)
$(info build/ is not for RODEO_HOST=$(RODEO_HOST): rebuilding)
RODEO_KEEP_STAMPS := $(filter-out build/.host-%,$(wildcard build/.[a-z]*-*))
$(shell rm -rf build && mkdir build $(if $(RODEO_KEEP_STAMPS),&& touch $(RODEO_KEEP_STAMPS)))
endif
$(shell mkdir -p build && touch $(RODEO_HOST_STAMP))

LIBDAISY_DIR = $(RODEO_ROOT)/libDaisy
DAISYSP_DIR  = $(RODEO_ROOT)/DaisySP

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

# Apps run from SRAM under the Daisy bootloader, whose extdfu build takes DFU over
# pins 36/37: Rodeo's USB-C. `make program-boot` installs it once, through the
# STM32's ROM on the Seed's micro-USB. Statics then live in DTCM, which DMA can't
# reach (see README). APP_TYPE=BOOT_NONE builds for internal flash, over the bootloader.
APP_TYPE ?= BOOT_SRAM
BOOT_BIN ?= $(SYSTEM_FILES_DIR)/dsy_bootloader_v6_4-extdfu-2000ms.bin
include $(SYSTEM_FILES_DIR)/Makefile
