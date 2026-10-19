# Rodeo host-side unit tests (GoogleTest).
#
# Compiles pure-logic firmware code for the HOST (your Mac), not ARM — no board,
# no flashing, runs in milliseconds. Reuses the googletest already vendored in
# libDaisy/tests, so there is NO new dependency to fetch.
#
#   make test     # build + run all tests
#   make clean
#
# Mirrors libDaisy/tests/Makefile (same clang++ / gnu++14 / gtest wiring).

CXX ?= clang++

SRC_PATH   = .
BUILD_PATH = build
BIN_PATH   = $(BUILD_PATH)/bin
BIN_NAME   = rodeo_gtest

# googletest, reused from libDaisy's vendored copy (no re-vendoring).
GTEST_DIR = ../../libDaisy/tests/googletest

# svf_stability_gtest.cpp exercises the REAL vendored DaisySP DSP, because what
# it guards is DaisySP's numerical behaviour, not our own logic -- a
# reimplementation here would not catch the bug. Only the handful of translation
# units that test needs are compiled; the rest of DaisySP is not pulled in.
DAISYSP_DIR = ../../DaisySP/Source
DSP_SOURCES = $(DAISYSP_DIR)/Filters/svf.cpp \
              $(DAISYSP_DIR)/Synthesis/oscillator.cpp \
              $(DAISYSP_DIR)/Control/adsr.cpp
DSP_OBJECTS = $(patsubst $(DAISYSP_DIR)/%.cpp,$(BUILD_PATH)/daisysp/%.o,$(DSP_SOURCES))

# Host-safe .cpp files from common/ that the tests link (the rest of common/
# needs libDaisy and the target).
COMMON_SOURCES = ../sat_chain.cpp
COMMON_OBJECTS = $(patsubst ../%.cpp,$(BUILD_PATH)/common/%.o,$(COMMON_SOURCES))

SRC_EXT = cpp
SOURCES = $(shell find $(SRC_PATH) -name '*.$(SRC_EXT)' | sort -k 1nr | cut -f2-)
OBJECTS = $(SOURCES:$(SRC_PATH)/%.$(SRC_EXT)=$(BUILD_PATH)/%.o)
DEPS    = $(OBJECTS:.o=.d) $(DSP_OBJECTS:.o=.d) $(COMMON_OBJECTS:.o=.d)

# NOTE: no -Werror here (libDaisy uses it) — the vendored gtest-all.cc can trip
# newer host-clang warnings, which we don't want failing OUR test build.
COMPILE_FLAGS = -std=gnu++14 -Wall -Wextra -g -pthread -DUNIT_TEST=1
INCLUDES = -I $(GTEST_DIR)/ \
           -I $(GTEST_DIR)/googletest/ \
           -I $(GTEST_DIR)/googletest/include/ \
           -I ../ \
           -I . \
           -I $(DAISYSP_DIR) \
           -I $(DAISYSP_DIR)/Filters \
           -I $(DAISYSP_DIR)/Utility \
           -I $(DAISYSP_DIR)/Synthesis \
           -I $(DAISYSP_DIR)/Control
LIBS = -pthread

.PHONY: default_target
default_target: release

.PHONY: release
release: export CXXFLAGS := $(CXXFLAGS) $(COMPILE_FLAGS)
release: dirs
	@$(MAKE) all

.PHONY: dirs
dirs:
	@mkdir -p $(dir $(OBJECTS))
	@mkdir -p $(BIN_PATH)

.PHONY: clean
clean:
	@$(RM) $(BIN_NAME)
	@$(RM) -r $(BUILD_PATH)

.PHONY: all
all: $(BIN_PATH)/$(BIN_NAME)
	@$(RM) $(BIN_NAME)
	@ln -s $(BIN_PATH)/$(BIN_NAME) $(BIN_NAME)

test: release c-headers
	./$(BIN_NAME)

# sat_frame.h is also built as C, by the satellites' toolchain. Compile it as
# strict C99 here so a C++-only construct fails on the host, not on the bench.
CC ?= clang
.PHONY: c-headers
c-headers:
	@echo '#include "sat_frame.h"' | $(CC) -std=c99 -Wall -Wextra -Werror -pedantic -I ../ -x c -fsyntax-only -
	@echo "sat_frame.h: builds as C99"

$(BIN_PATH)/$(BIN_NAME): $(OBJECTS) $(DSP_OBJECTS) $(COMMON_OBJECTS)
	@echo "Linking: $@"
	$(CXX) $(OBJECTS) $(DSP_OBJECTS) $(COMMON_OBJECTS) -o $@ $(LIBS)

$(BUILD_PATH)/common/%.o: ../%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling: $< -> $@"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MP -MMD -c $< -o $@

$(BUILD_PATH)/daisysp/%.o: $(DAISYSP_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling: $< -> $@"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MP -MMD -c $< -o $@

-include $(DEPS)

$(BUILD_PATH)/%.o: $(SRC_PATH)/%.$(SRC_EXT)
	@echo "Compiling: $< -> $@"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MP -MMD -c $< -o $@
