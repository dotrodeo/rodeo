# TinyUSB MIDI + CDC serial. USB step 1 on a bare Daisy Seed's micro-USB, and
# bring-up 08 on Rev B's USB-C with `make RODEO_USB=hs`. See src/main.cpp.
TARGET       = usb_midi_cdc
CPP_SOURCES  = src/main.cpp
RODEO_USB   ?= fs
TUSB_CLASSES = cdc/cdc_device.c midi/midi_device.c

include ../../common/tinyusb.mk
include ../../common/rodeo.mk
