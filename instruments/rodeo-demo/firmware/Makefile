# rodeo-demo: a small synth and sampler for Rev B, played over USB-MIDI (or TRS
# MIDI) from a pot bank and an encoder bank on the satellite chain, recorded over
# USB audio. See ../docs/RODEO_DEMO_CONCEPT.md and src/main.cpp.
#
#   make                  build for Rev B's USB-C (`make RODEO_USB=fs`: the Seed's micro-USB)
#   make program-dfu      flash it, after 'B' on the serial port (or the bootloader's
#                         2-second window at power-up)
#   make AUDIO_BITS=24    24-bit USB audio: direct ports only, not behind a hub
TARGET       = rodeo-demo
CPP_SOURCES  = src/main.cpp src/synth.cpp src/samples.cpp src/panel.cpp src/usb_audio.cpp \
               $(RODEO_ROOT)/common/audio_4x4.cpp \
               $(RODEO_ROOT)/common/sat_chain.cpp $(RODEO_ROOT)/common/sat_chain_port.cpp
C_SOURCES    = src/usb_descriptors.c
RODEO_ROOT   = ../../../rodeo-sdk
RODEO_USB   ?= hs
TUSB_CLASSES = audio/audio_device.c cdc/cdc_device.c midi/midi_device.c
USE_FATFS    = 1
# The reverb is DaisySP-LGPL's ReverbSc. rodeo-demo is open source, so linking
# LGPL is fine.
USE_DAISYSP_LGPL = 1

AUDIO_BITS  ?= 16

include $(RODEO_ROOT)/common/tinyusb.mk

ifeq ($(filter 16 24,$(AUDIO_BITS)),)
$(error AUDIO_BITS must be 16 or 24, not '$(AUDIO_BITS)')
endif
C_DEFS += -DAUDIO_BITS=$(AUDIO_BITS)
# As tinyusb.mk does for the port: a stamp says which depth build/ holds, and
# changing it rebuilds from scratch.
ifeq ($(wildcard build/.bits-$(AUDIO_BITS)),)
$(shell rm -rf build && mkdir build && touch build/.usb-$(RODEO_USB) build/.bits-$(AUDIO_BITS))
endif
include $(RODEO_ROOT)/common/rodeo.mk
