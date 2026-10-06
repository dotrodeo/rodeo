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

# Cross-board wire contract: the encoder-hub I2C register map (rodeo_hub_regs.h)
# is authored ONCE in the sibling rodeo-encoder-hub repo and included verbatim by
# BOTH firmwares, so the byte layout can never drift. Path is relative to the
# ecosystem root (../../ from firmware/). Harmless if the sibling repo is absent
# (gcc just ignores a missing -I dir) — only the encoder master driver needs it.
C_INCLUDES += -I$(RODEO_ROOT)/../rodeo-encoder-hub/firmware/include

LIBDAISY_DIR = $(RODEO_ROOT)/libDaisy
DAISYSP_DIR  = $(RODEO_ROOT)/DaisySP

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
