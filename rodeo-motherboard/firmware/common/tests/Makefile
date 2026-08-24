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

SRC_EXT = cpp
SOURCES = $(shell find $(SRC_PATH) -name '*.$(SRC_EXT)' | sort -k 1nr | cut -f2-)
OBJECTS = $(SOURCES:$(SRC_PATH)/%.$(SRC_EXT)=$(BUILD_PATH)/%.o)
DEPS    = $(OBJECTS:.o=.d)

# NOTE: no -Werror here (libDaisy uses it) — the vendored gtest-all.cc can trip
# newer host-clang warnings, which we don't want failing OUR test build.
COMPILE_FLAGS = -std=gnu++14 -Wall -Wextra -g -pthread -DUNIT_TEST=1
INCLUDES = -I $(GTEST_DIR)/ \
           -I $(GTEST_DIR)/googletest/ \
           -I $(GTEST_DIR)/googletest/include/ \
           -I ../ \
           -I .
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

test: release
	./$(BIN_NAME)

$(BIN_PATH)/$(BIN_NAME): $(OBJECTS)
	@echo "Linking: $@"
	$(CXX) $(OBJECTS) -o $@ $(LIBS)

-include $(DEPS)

$(BUILD_PATH)/%.o: $(SRC_PATH)/%.$(SRC_EXT)
	@echo "Compiling: $< -> $@"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MP -MMD -c $< -o $@
