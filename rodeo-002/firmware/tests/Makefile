# rodeo-002 host-side tests (GoogleTest) for the texture-chain effects.
#
# Builds the effect headers in ../common for the HOST (your Mac), against the
# REAL vendored DaisySP -- what these tests guard is numerical behaviour
# (blow-ups, NaN, runaway feedback), and a reimplementation would not catch it.
# Mirrors rodeo-sdk/common/tests: same clang++ / gnu++14 and the
# googletest already vendored in libDaisy, so no new dependency.
#
#   make test     # build + run
#   make clean
#
# In a git worktree without submodules, point RODEO_ROOT at the main checkout:
#   make test RODEO_ROOT=/path/to/rodeo/rodeo-sdk

CXX ?= clang++

RODEO_ROOT ?= ../../../rodeo-sdk

BUILD_PATH = build
BIN_NAME   = rodeo002_gtest

GTEST_DIR   = $(RODEO_ROOT)/libDaisy/tests/googletest
DAISYSP_DIR = $(RODEO_ROOT)/DaisySP/Source
LGPL_DIR    = $(RODEO_ROOT)/DaisySP/DaisySP-LGPL/Source

# Only the DaisySP translation units the effects actually use.
DSP_SOURCES = $(DAISYSP_DIR)/Filters/svf.cpp \
              $(DAISYSP_DIR)/Control/adsr.cpp \
              $(DAISYSP_DIR)/Synthesis/oscillator.cpp \
              $(DAISYSP_DIR)/Synthesis/variableshapeosc.cpp \
              $(DAISYSP_DIR)/Synthesis/variablesawosc.cpp \
              $(DAISYSP_DIR)/Synthesis/fm2.cpp \
              $(DAISYSP_DIR)/Synthesis/formantosc.cpp \
              $(DAISYSP_DIR)/Synthesis/vosim.cpp \
              $(DAISYSP_DIR)/Synthesis/zoscillator.cpp \
              $(DAISYSP_DIR)/PhysicalModeling/resonator.cpp \
              $(DAISYSP_DIR)/Utility/dcblock.cpp \
              $(DAISYSP_DIR)/Dynamics/crossfade.cpp \
              $(DAISYSP_DIR)/Effects/overdrive.cpp \
              $(DAISYSP_DIR)/Effects/wavefolder.cpp \
              $(DAISYSP_DIR)/Effects/decimator.cpp \
              $(DAISYSP_DIR)/Effects/sampleratereducer.cpp \
              $(LGPL_DIR)/Effects/reverbsc.cpp
DSP_OBJECTS = $(patsubst %.cpp,$(BUILD_PATH)/dsp/%.o,$(notdir $(DSP_SOURCES)))

SOURCES = $(wildcard *.cpp)
OBJECTS = $(SOURCES:%.cpp=$(BUILD_PATH)/%.o)

CXXFLAGS += -std=gnu++14 -O2 -Wall -Wextra -g -pthread -DUNIT_TEST=1
INCLUDES  = -I $(GTEST_DIR)/ -I $(GTEST_DIR)/googletest/ \
            -I $(GTEST_DIR)/googletest/include/ \
            -I ../common -I $(RODEO_ROOT)/common -isystem $(DAISYSP_DIR) -isystem $(DAISYSP_DIR)/Utility \
            -isystem $(LGPL_DIR)

vpath %.cpp $(sort $(dir $(DSP_SOURCES)))

.PHONY: test clean
test: $(BUILD_PATH)/$(BIN_NAME)
	./$(BUILD_PATH)/$(BIN_NAME)

$(BUILD_PATH)/$(BIN_NAME): $(OBJECTS) $(DSP_OBJECTS)
	$(CXX) $^ -o $@ -pthread

$(BUILD_PATH)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD_PATH)/dsp/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -w $(INCLUDES) -MMD -MP -c $< -o $@

clean:
	rm -rf $(BUILD_PATH)

-include $(OBJECTS:.o=.d) $(DSP_OBJECTS:.o=.d)
