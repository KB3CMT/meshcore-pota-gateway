#include <gtest/gtest.h>

#include <helpers/NRF52VoltageRules.h>

TEST(NRF52VoltageRules, LiFePO4Thresholds) {
  EXPECT_EQ(mesh::power::LIFEPO4_BOOTLOCK_MV_DEFAULT, 2900);
  EXPECT_EQ(mesh::power::LIFEPO4_CUTOFF_MV_DEFAULT, 2700);
  EXPECT_EQ(mesh::power::LIFEPO4_EMPTY_MV, 2700);
  EXPECT_EQ(mesh::power::LIFEPO4_FULL_MV, 3550);
}

TEST(NRF52VoltageRules, MedianOfThree) {
  using mesh::power::medianVoltage;
  EXPECT_EQ(medianVoltage(2800, 2900, 3000), 2900);
  EXPECT_EQ(medianVoltage(3000, 2800, 2900), 2900);
  EXPECT_EQ(medianVoltage(100, 100, 5000), 100);
}

TEST(NRF52VoltageRules, BootLockIgnoresGlitchesAndExternalPower) {
  using mesh::power::shouldBootLock;
  EXPECT_TRUE(shouldBootLock(2800, 2900, false));
  EXPECT_FALSE(shouldBootLock(2900, 2900, false));
  EXPECT_FALSE(shouldBootLock(3000, 2900, false));
  EXPECT_FALSE(shouldBootLock(2800, 2900, true));
  EXPECT_FALSE(shouldBootLock(2800, 0, false));
  EXPECT_FALSE(shouldBootLock(999, 2900, false));
}

TEST(NRF52VoltageRules, RequiresThreeConsecutiveValidLowReadings) {
  using mesh::power::nextLowVoltageCount;
  EXPECT_EQ(nextLowVoltageCount(0, 2690, 2700, false), 1);
  EXPECT_EQ(nextLowVoltageCount(1, 2650, 2700, false), 2);
  EXPECT_EQ(nextLowVoltageCount(2, 2600, 2700, false), 3);
  EXPECT_EQ(nextLowVoltageCount(3, 2600, 2700, false), 3);
  EXPECT_EQ(nextLowVoltageCount(2, 2700, 2700, false), 0);
  EXPECT_EQ(nextLowVoltageCount(2, 999, 2700, false), 0);
  EXPECT_EQ(nextLowVoltageCount(2, 2600, 2700, true), 0);
  EXPECT_EQ(nextLowVoltageCount(2, 2600, 0, false), 0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
