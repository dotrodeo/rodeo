# rodeo-002 — host-side screen previews. See preview.cpp.
#
# C++17, unlike the firmware's gnu++14: libDaisy's FixedCapStr wants
# <string_view>. Nothing else differs, and the drawing code is shared verbatim.

CXX ?= clang++
RODEO_ROOT ?= ../../../../rodeo-motherboard/firmware

BUILD = build
BIN   = $(BUILD)/preview

CXXFLAGS += -std=gnu++17 -O1 -g -w
INCLUDES  = -I ../../common -I $(RODEO_ROOT)/libDaisy/src \
            -I $(RODEO_ROOT)/libDaisy/src/util \
            -isystem $(RODEO_ROOT)/DaisySP/Source \
            -isystem $(RODEO_ROOT)/DaisySP/Source/Utility \
            -isystem $(RODEO_ROOT)/DaisySP/DaisySP-LGPL/Source

.PHONY: all ascii clean
all: $(BIN)
	@$(BIN)

ascii: $(BIN)
	@$(BIN) ascii

$(BIN): preview.cpp ../../common/screen.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -x c++ preview.cpp $(RODEO_ROOT)/libDaisy/src/util/oled_fonts.c -o $@

clean:
	rm -rf $(BUILD)
