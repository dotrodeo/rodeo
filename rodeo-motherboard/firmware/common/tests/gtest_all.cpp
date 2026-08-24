// Compiles the whole googletest framework into one object, from libDaisy's
// vendored copy. The `-I $(GTEST_DIR)/` flag in the Makefile makes this path
// resolve to ../../libDaisy/tests/googletest/googletest/src/gtest-all.cc.
// (Named .cpp so the Makefile's `find -name '*.cpp'` picks it up; the included
// .cc is not matched, so it is not double-compiled.)
#include "googletest/src/gtest-all.cc"
