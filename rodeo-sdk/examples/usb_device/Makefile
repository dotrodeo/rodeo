# USB device test on a bare Daisy Seed's micro-USB, on TinyUSB: UAC1 audio 2x2 48 kHz
# 24-bit + MIDI + serial, and a disk mode that offers a RAM disk as USB storage.
# `make RODEO_USB=hs` puts it on Rev B's USB-C instead. See planning/REV_B_PLAN.md, "USB".
TARGET       = usb_device
CPP_SOURCES  = src/main.cpp
C_SOURCES    = src/usb_descriptors.c
RODEO_USB   ?= fs
TUSB_CLASSES = audio/audio_device.c cdc/cdc_device.c midi/midi_device.c msc/msc_device.c
USE_FATFS    = 1

include ../../common/tinyusb.mk
include ../../common/rodeo.mk
