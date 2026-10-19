# rodeo-satellite host tests (GoogleTest): the satellite's logic, built for the
# host. The firmware's C sources compile as C99, exactly as the satellite's
# toolchain builds them; the tests are C++. googletest comes from libDaisy's
# vendored copy in rodeo-sdk, as the SDK's own tests use it.
#
#   make test
#   make clean

SDK       = ../../rodeo-sdk
GTEST_DIR = $(SDK)/libDaisy/tests/googletest
CC  ?= clang
CXX ?= clang++
BUILD = build

INCLUDES = -I ../src -I $(SDK)/common -I $(GTEST_DIR) -I $(GTEST_DIR)/googletest \
           -I $(GTEST_DIR)/googletest/include
CFLAGS   = -std=c99 -Wall -Wextra -Werror -pedantic -g $(INCLUDES)
CXXFLAGS = -std=gnu++14 -Wall -Wextra -g -pthread $(INCLUDES)

C_SRC   = $(wildcard ../src/*.c)
C_OBJ   = $(patsubst ../src/%.c,$(BUILD)/src/%.o,$(C_SRC))
CPP_SRC = $(wildcard *.cpp)
CPP_OBJ = $(patsubst %.cpp,$(BUILD)/%.o,$(CPP_SRC))

.PHONY: test clean
test: $(BUILD)/satellite_tests
	./$(BUILD)/satellite_tests

$(BUILD)/satellite_tests: $(C_OBJ) $(CPP_OBJ)
	$(CXX) $^ -o $@ -pthread

$(BUILD)/src/%.o: ../src/%.c $(wildcard ../src/*.h) $(SDK)/common/sat_frame.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.cpp $(wildcard ../src/*.h) $(SDK)/common/sat_frame.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)
