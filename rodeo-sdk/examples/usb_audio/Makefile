# Rev B bring-up 10: USB audio 4 in / 4 out across both codecs, with MIDI and
# serial. Defaults to Rev B's USB-C (`make RODEO_USB=fs` for the Seed's
# micro-USB) and 16-bit audio (`make AUDIO_BITS=24` for 24-bit, which works only
# on a port wired straight to the computer, not behind a hub). See src/main.cpp.
TARGET       = usb_audio
CPP_SOURCES  = src/main.cpp ../../common/audio_4x4.cpp
C_SOURCES    = src/usb_descriptors.c
RODEO_USB   ?= hs
TUSB_CLASSES = audio/audio_device.c cdc/cdc_device.c midi/midi_device.c

AUDIO_BITS  ?= 16

include ../../common/tinyusb.mk

ifeq ($(filter 16 24,$(AUDIO_BITS)),)
$(error AUDIO_BITS must be 16 or 24, not '$(AUDIO_BITS)')
endif
C_DEFS += -DAUDIO_BITS=$(AUDIO_BITS)
# As tinyusb.mk does for the port: the objects don't depend on the flag, so a
# stamp says which depth build/ holds, and changing it rebuilds from scratch.
ifeq ($(wildcard build/.bits-$(AUDIO_BITS)),)
$(shell rm -rf build && mkdir build && touch build/.usb-$(RODEO_USB) build/.bits-$(AUDIO_BITS))
endif
include ../../common/rodeo.mk
