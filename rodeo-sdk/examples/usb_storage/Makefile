# Rev B bring-up 09: the internal microSD card as a USB drive, in a disk mode.
# Defaults to Rev B's USB-C (`make RODEO_USB=fs` for the Seed's micro-USB).
# See src/main.cpp.
TARGET       = usb_storage
CPP_SOURCES  = src/main.cpp
C_SOURCES    = src/usb_descriptors.c
RODEO_USB   ?= hs
TUSB_CLASSES = cdc/cdc_device.c midi/midi_device.c msc/msc_device.c
USE_FATFS    = 1

include ../../common/tinyusb.mk
include ../../common/rodeo.mk
