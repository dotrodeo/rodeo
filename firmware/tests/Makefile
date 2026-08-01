# Host-native unit tests (compiled for the dev machine, no hardware).
CXX      ?= c++
CXXFLAGS := -std=c++17 -I../src -Wall -Wextra -O0 -g

TESTS := test_bringup test_note_stack test_spsc test_voice_alloc

all: run

%: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

run: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done; \
	  echo "ALL HOST TESTS PASSED"

clean:
	rm -f $(TESTS)

.PHONY: all run clean
