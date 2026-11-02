# rodeo-satellite firmware for the CH32V003, on ch32v003fun.
#
#   make          build satellite.bin
#   make flash    flash it with a WCH-LinkE (ch32v003fun's minichlink)
#   make flash-pico PICO=/dev/cu.usbmodemXXXX
#                 flash it with a Pi Pico or Pico 2 running picorvd instead
#                 (../tools/picorvd_flash.py; see the README)
#   make clean && make DEBUG=1 flash monitor
#                 a build that prints its kind and controls over the
#                 WCH-LinkE, for testing one satellite with no Seed attached
#   make clean
#
# Needs a RISC-V GCC (macOS: brew install riscv64-elf-gcc riscv64-elf-binutils)
# and newlib's headers, which ch32v003fun includes but doesn't link. Homebrew's
# RISC-V GCC has none, so they come from the Arm GNU Toolchain the SDK already
# needs (the same headers Fedora's ch32v003fun builds use). Override NEWLIB if
# yours are elsewhere.

all : satellite.bin

TARGET     := satellite
TARGET_MCU := CH32V003

SRC := ../src
ADDITIONAL_C_FILES := $(SRC)/stream.c $(SRC)/kind.c $(SRC)/encoder.c $(SRC)/ladder.c \
                      $(SRC)/buttons.c $(SRC)/lights.c
EXTRA_CFLAGS += -I$(SRC) -I../../rodeo-sdk/common
ifeq ($(DEBUG),1)
EXTRA_CFLAGS += -DSAT_DEBUG
endif

NEWLIB ?= $(lastword $(sort $(wildcard /Applications/ArmGNUToolchain/*/arm-none-eabi/arm-none-eabi/include)))

include ../ch32v003fun/ch32fun/ch32fun.mk

flash : cv_flash

flash-pico : satellite.bin
	@test -n "$(PICO)" || { echo "PICO=/dev/cu.usbmodem... (the Pico's port)"; exit 1; }
	python3 ../tools/picorvd_flash.py --port $(PICO) satellite.bin
clean : cv_clean
