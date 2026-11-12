# common/tinyusb.mk — TinyUSB for an app, on the USB port chosen at build time.
#
#   make                  the app's default port (its Makefile sets RODEO_USB ?= ...)
#   make RODEO_USB=hs     Rev B: the OTG_HS core on its internal full-speed PHY,
#                         Seed pins 36/37, out through the power board's USB-C
#   make RODEO_USB=fs     the Seed's own micro-USB (OTG_FS)
#
# The app's Makefile sets RODEO_USB, lists the TinyUSB classes it uses in
# TUSB_CLASSES (e.g. cdc/cdc_device.c), includes this, then common/rodeo.mk.
# This adds TinyUSB's core and DWC2 driver, common/usb_port.cpp (pins, clock,
# interrupt for the chosen port) and -DRODEO_USB_HS when it's hs; the app's
# tusb_config.h picks the TinyUSB port from that (usb_port.h).
#
# Switching ports rebuilds from scratch: the objects don't depend on the flag,
# so a stamp in build/ says which port they were built for, and a build/
# without the right stamp is removed.

RODEO_ROOT ?= ../..
RODEO_USB  ?= fs

ifeq ($(RODEO_USB),hs)
C_DEFS += -DRODEO_USB_HS
else ifneq ($(RODEO_USB),fs)
$(error RODEO_USB must be fs or hs, not '$(RODEO_USB)')
endif

ifneq ($(wildcard build),)
ifeq ($(wildcard build/.usb-$(RODEO_USB)),)
$(info build/ is not for RODEO_USB=$(RODEO_USB): rebuilding)
$(shell rm -rf build)
endif
endif
$(shell mkdir -p build && rm -f build/.usb-* && touch build/.usb-$(RODEO_USB))

TINYUSB = $(RODEO_ROOT)/tinyusb/src
C_SOURCES += $(TINYUSB)/tusb.c \
             $(TINYUSB)/common/tusb_fifo.c \
             $(TINYUSB)/device/usbd.c \
             $(addprefix $(TINYUSB)/class/,$(TUSB_CLASSES)) \
             $(TINYUSB)/portable/synopsys/dwc2/dcd_dwc2.c \
             $(TINYUSB)/portable/synopsys/dwc2/dwc2_common.c
CPP_SOURCES += $(RODEO_ROOT)/common/usb_port.cpp
# The descriptors, from the layout and identity in the app's tusb_config.h
C_SOURCES   += $(RODEO_ROOT)/common/usb_desc.c
C_INCLUDES  += -Isrc -I$(TINYUSB)
