# common/rodeo.mk — shared build rules for Rodeo motherboard firmware.
#
# Each app / example Makefile defines TARGET and CPP_SOURCES, then includes this
# file. It wires up the vendored libDaisy + DaisySP, the shared Rodeo headers
# (common/board.h — the pin map), and the local toolchain path from config.mk.
#
#   TARGET      = my_app
#   CPP_SOURCES = src/main.cpp
#   include ../../common/rodeo.mk
#
# RODEO_ROOT is the firmware/ root, two levels up from an example/template dir.

RODEO_ROOT ?= ../..

# Local, machine-specific toolchain path (GCC_PATH). Gitignored — see README.
-include $(RODEO_ROOT)/config.mk

# Rodeo shared headers. board.h is the single source of truth for the pin map.
C_INCLUDES += -I$(RODEO_ROOT)/common

LIBDAISY_DIR = $(RODEO_ROOT)/libDaisy
DAISYSP_DIR  = $(RODEO_ROOT)/DaisySP

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
