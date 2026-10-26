# Rev B bring-up 10: USB audio 4 in / 4 out across both codecs, with MIDI and
# serial. Defaults to Rev B's USB-C (`make RODEO_USB=fs` for the Seed's
# micro-USB). See src/main.cpp.
TARGET       = usb_audio
CPP_SOURCES  = src/main.cpp ../../common/audio_4x4.cpp
C_SOURCES    = src/usb_descriptors.c
RODEO_USB   ?= hs
TUSB_CLASSES = audio/audio_device.c cdc/cdc_device.c midi/midi_device.c

include ../../common/tinyusb.mk
include ../../common/rodeo.mk
