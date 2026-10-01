# rodeo-001 firmware — the instrument, built on the rodeo-motherboard boilerplate.
#
# Consume-not-copy: RODEO_ROOT points at the shared Rodeo SDK
# tree, so this app links the vendored libDaisy/DaisySP and the shared common/
# drivers (board.h pin map, RodeoAdc scanner, ADS114S08 driver) WITHOUT forking
# them. Driver/pin-map fixes upstream propagate here for free.
#
#   make              # build this app
#   make program-dfu  # flash over USB DFU (hold BOOT, tap RESET first)

RODEO_ROOT := ../../rodeo-sdk

TARGET = rodeo_001
CPP_SOURCES = \
	src/main.cpp \
	$(RODEO_ROOT)/common/rodeo_adc.cpp \
	$(RODEO_ROOT)/common/ads114s08.cpp

include $(RODEO_ROOT)/common/rodeo.mk
