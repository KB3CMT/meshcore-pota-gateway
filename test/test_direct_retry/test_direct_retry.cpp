#include <gtest/gtest.h>

#include <helpers/DirectRetryUtils.h>

using namespace mesh;

TEST(DirectRetryUtils, HashedPathEchoIsShorterRemainingPath) {
  EXPECT_TRUE(isDirectRetryEcho(false, 3, 0, 2));
  EXPECT_FALSE(isDirectRetryEcho(false, 3, 0, 3));
  EXPECT_FALSE(isDirectRetryEcho(false, 3, 0, 4));
}

TEST(DirectRetryUtils, TraceEchoIsPathGrowth) {
  EXPECT_TRUE(isDirectRetryEcho(true, 2, 3, 0));
  EXPECT_FALSE(isDirectRetryEcho(true, 2, 2, 0));
  EXPECT_FALSE(isDirectRetryEcho(true, 2, 1, 0));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
