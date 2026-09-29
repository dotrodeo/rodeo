# rodeo-002 — the instrument. Build rules are in ../common/app.mk.
#
# The platform's drivers (ADC scanner, encoder-hub master, OLED transport) live
# in rodeo-motherboard/firmware/common and are compiled in here.

TARGET      = synth
CPP_SOURCES = src/main.cpp \
              $(RODEO_ROOT)/common/rodeo_adc.cpp \
              $(RODEO_ROOT)/common/ads114s08.cpp \
              $(RODEO_ROOT)/common/rodeo_encoders.cpp \
              $(RODEO_ROOT)/common/encoder_hub.cpp \
              $(RODEO_ROOT)/common/oled_ssd130x_bulk.cpp

include ../common/app.mk
