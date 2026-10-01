# Rodeo SDK — top-level helpers.
#
# The real builds live in template/ and examples/<name>/. cd into one and run
# `make` (then `make program-dfu` to flash). This top-level file just builds the
# vendored libraries once, and offers a convenience sweep build of all examples.
#
#   make deps   # build libDaisy + DaisySP static libs (first time / after bump)
#   make all    # build every example
#   make clean  # clean every example + template

-include config.mk

LIBDAISY_DIR = libDaisy
DAISYSP_DIR  = DaisySP
GCC := $(if $(GCC_PATH),GCC_PATH=$(GCC_PATH))

EXAMPLES := $(sort $(dir $(wildcard examples/*/)))

.PHONY: deps all clean $(EXAMPLES)

deps:
	$(MAKE) -C $(LIBDAISY_DIR) $(GCC)
	$(MAKE) -C $(DAISYSP_DIR) $(GCC)

all: $(EXAMPLES)

$(EXAMPLES):
	$(MAKE) -C $@

clean:
	@for d in $(EXAMPLES) template; do $(MAKE) -C $$d clean; done
