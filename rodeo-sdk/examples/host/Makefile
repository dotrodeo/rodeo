# The host protocol (common/host.h): a drone whose parameters the computer can
# see and set over USB-MIDI, played by the first pot bank and by MIDI notes.
# Rev B. See src/main.cpp.
#
#   make && make program-dfu    after the dfu command, or 'B' in another example
#   make RODEO_HOST=0           the same firmware with the protocol compiled out
TARGET       = host
CPP_SOURCES  = src/main.cpp \
               ../../common/host.cpp ../../common/audio_4x4.cpp \
               ../../common/sat_chain.cpp ../../common/sat_chain_port.cpp
C_SOURCES    = src/usb_descriptors.c
RODEO_USB   ?= hs
TUSB_CLASSES = midi/midi_device.c
# The commit, for hello
C_DEFS += -DBUILD_ID=\"$(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)\"

include ../../common/tinyusb.mk
include ../../common/rodeo.mk
