# rodeo-satellite firmware for the CH32V003, on ch32v003fun.
#
#   make          build satellite.bin
#   make flash    flash it with a WCH-LinkE (ch32v003fun's minichlink)
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

NEWLIB ?= $(lastword $(sort $(wildcard /Applications/ArmGNUToolchain/*/arm-none-eabi/arm-none-eabi/include)))

include ../ch32v003fun/ch32fun/ch32fun.mk

flash : cv_flash
clean : cv_clean
