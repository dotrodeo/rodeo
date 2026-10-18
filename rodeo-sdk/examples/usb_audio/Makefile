# USB audio test: TinyUSB UAC1 2x2 48 kHz 24-bit + MIDI + serial on a bare Daisy
# Seed's micro-USB. See rodeo-sdk/docs/REV_B_PLAN.md, "USB".
TARGET      = usb_audio
CPP_SOURCES = src/main.cpp

TINYUSB = ../../tinyusb/src
C_SOURCES = src/usb_descriptors.c \
            $(TINYUSB)/tusb.c \
            $(TINYUSB)/common/tusb_fifo.c \
            $(TINYUSB)/device/usbd.c \
            $(TINYUSB)/class/audio/audio_device.c \
            $(TINYUSB)/class/cdc/cdc_device.c \
            $(TINYUSB)/class/midi/midi_device.c \
            $(TINYUSB)/portable/synopsys/dwc2/dcd_dwc2.c \
            $(TINYUSB)/portable/synopsys/dwc2/dwc2_common.c
C_INCLUDES = -Isrc -I$(TINYUSB)

include ../../common/rodeo.mk
