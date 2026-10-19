// The whole googletest framework as one object, from libDaisy's vendored copy
// (the same arrangement as rodeo-sdk/common/tests).
#include "googletest/src/gtest-all.cc"

#include "gtest/gtest.h"

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
