# USB device test on a bare Daisy Seed's micro-USB, on TinyUSB: UAC1 audio 2x2 48 kHz
# 24-bit + MIDI + serial, and a disk mode that offers a RAM disk as USB storage.
# See planning/REV_B_PLAN.md, "USB".
TARGET      = usb_device
CPP_SOURCES = src/main.cpp

TINYUSB = ../../tinyusb/src
C_SOURCES = src/usb_descriptors.c \
            $(TINYUSB)/tusb.c \
            $(TINYUSB)/common/tusb_fifo.c \
            $(TINYUSB)/device/usbd.c \
            $(TINYUSB)/class/audio/audio_device.c \
            $(TINYUSB)/class/cdc/cdc_device.c \
            $(TINYUSB)/class/midi/midi_device.c \
            $(TINYUSB)/class/msc/msc_device.c \
            $(TINYUSB)/portable/synopsys/dwc2/dcd_dwc2.c \
            $(TINYUSB)/portable/synopsys/dwc2/dwc2_common.c
C_INCLUDES = -Isrc -I$(TINYUSB)
USE_FATFS  = 1

include ../../common/rodeo.mk
