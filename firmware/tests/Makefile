# Host-native unit tests (compiled for the dev machine, no hardware).
CXX      ?= c++
CXXFLAGS := -std=c++17 -I../src -Wall -Wextra -O0 -g

TESTS := test_bringup

all: run

test_bringup: test_bringup.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

run: $(TESTS)
	@./test_bringup && echo "ALL HOST TESTS PASSED"

clean:
	rm -f $(TESTS)

.PHONY: all run clean
