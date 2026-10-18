# Rodeo SDK — top-level helpers.
#
# The real builds live in template/ and examples/<name>/. cd into one and run
# `make` (then `make program-dfu` to flash). This top-level file just builds the
# vendored libraries once, and offers a convenience sweep build of all examples.
#
#   make deps   # build libDaisy + DaisySP static libs (first time / after bump)
#   make all    # build every example
#   make clean  # clean every example + template
#   make docs   # regenerate docs/reference/api/ from the headers' comments
#   make docs-check   # fail if docs/reference/api/ is out of date (what CI runs)

-include config.mk

LIBDAISY_DIR = libDaisy
DAISYSP_DIR  = DaisySP
GCC := $(if $(GCC_PATH),GCC_PATH=$(GCC_PATH))

EXAMPLES := $(sort $(dir $(wildcard examples/*/)))

.PHONY: deps all clean docs docs-check $(EXAMPLES)

deps:
	$(MAKE) -C $(LIBDAISY_DIR) $(GCC)
	$(MAKE) -C $(DAISYSP_DIR) $(GCC)

all: $(EXAMPLES)

$(EXAMPLES):
	$(MAKE) -C $@

clean:
	@for d in $(EXAMPLES) template; do $(MAKE) -C $$d clean; done

# The API reference is generated (tools/docs/gen_reference.py) with libclang,
# installed once into tools/docs/.venv.
DOCS_VENV  = tools/docs/.venv
DOCS_PY    = $(DOCS_VENV)/bin/python
DOCS_STAMP = $(DOCS_VENV)/.installed

$(DOCS_STAMP): tools/docs/requirements.txt
	python3 -m venv $(DOCS_VENV)
	$(DOCS_VENV)/bin/pip install -q -r tools/docs/requirements.txt
	@touch $@

docs: $(DOCS_STAMP)
	$(DOCS_PY) tools/docs/gen_reference.py

docs-check: $(DOCS_STAMP)
	$(DOCS_PY) tools/docs/gen_reference.py --check
