#include <gtest/gtest.h>

#include <helpers/ClockSyncUtils.h>
#include <helpers/MeshClockFallback.h>

using namespace mesh;

TEST(ClockSyncUtils, RequiresQuorumAndMajority) {
  uint32_t values[] = {1800000000UL, 1800000010UL, 1800000020UL};
  auto r = evaluateClockSyncConsensus(values, 3, 3, 120);
  EXPECT_TRUE(r.consensus);
  EXPECT_EQ(r.agreeing_count, 3);
  EXPECT_EQ(r.estimate, 1800000010UL);
}

TEST(ClockSyncUtils, SplitDoesNotReachConsensus) {
  uint32_t values[] = {1800000000UL, 1800000000UL, 1900000000UL, 1900000000UL};
  auto r = evaluateClockSyncConsensus(values, 4, 3, 120);
  EXPECT_FALSE(r.consensus);
  EXPECT_EQ(r.required_count, 3);  // strict majority of 4 is 3; two clusters of 2
}

TEST(ClockSyncUtils, RejectsTooFewSamples) {
  uint32_t values[] = {1800000000UL, 1800000010UL};
  auto r = evaluateClockSyncConsensus(values, 2, 3, 120);
  EXPECT_FALSE(r.consensus);
}

TEST(ClockSyncUtils, BoundsAndSentinels) {
  EXPECT_TRUE(clockSyncEstimateInBounds(1800000000UL));
  EXPECT_FALSE(clockSyncEstimateInBounds(1000UL));
  EXPECT_TRUE(clockLooksUnset(CLOCK_SYNC_NRF52_SEED));
  EXPECT_TRUE(clockLooksUnset(CLOCK_SYNC_ESP32_SEED));
  EXPECT_TRUE(clockLooksUnset(0));
  EXPECT_FALSE(clockLooksUnset(1800000000UL));
}

TEST(ClockSyncUtils, FallbackNeverMovesAuthoritativeOrValidClock) {
  EXPECT_FALSE(shouldApplyMeshClockFallback(true, true, 1800000000UL, 1800001000UL));
  EXPECT_FALSE(shouldApplyMeshClockFallback(false, true, 1800000000UL, 1800001000UL));
  EXPECT_TRUE(shouldApplyMeshClockFallback(false, false, CLOCK_SYNC_NRF52_SEED, 1800000000UL));
}

TEST(ClockSyncUtils, FallbackNeverPullsValidTimeBackward) {
  EXPECT_FALSE(shouldApplyMeshClockFallback(false, false, 1800001000UL, 1800000000UL));
  EXPECT_TRUE(shouldApplyMeshClockFallback(false, false, 1800000000UL, 1800001000UL));
}

class FakeClock : public mesh::RTCClock {
  uint32_t t;
public:
  explicit FakeClock(uint32_t now) : t(now) {}
  uint32_t getCurrentTime() override { return t; }
  void setCurrentTime(uint32_t time) override { t = time; }
};

TEST(MeshClockFallback, SetsUnsetClockFromThreeAgreeingAdverts) {
  FakeClock clock(CLOCK_SYNC_NRF52_SEED);
  MeshClockFallback fallback;
  fallback.begin(&clock);
  uint8_t a[32] = {1}, b[32] = {2}, c[32] = {3};
  fallback.observeAdvert(a, 1800000000UL);
  fallback.observeAdvert(b, 1800000010UL);
  EXPECT_EQ(clock.getCurrentTime(), CLOCK_SYNC_NRF52_SEED);
  fallback.observeAdvert(c, 1800000020UL);
  EXPECT_EQ(clock.getCurrentTime(), 1800000010UL);
  EXPECT_TRUE(clock.hasValidTime());
  EXPECT_FALSE(clock.isAuthoritative());
}

TEST(MeshClockFallback, AuthoritativeClockIsNeverMoved) {
  FakeClock clock(CLOCK_SYNC_NRF52_SEED);
  clock.markAuthoritative();
  MeshClockFallback fallback;
  fallback.begin(&clock);
  uint8_t a[32] = {1}, b[32] = {2}, c[32] = {3};
  fallback.observeAdvert(a, 1800000000UL);
  fallback.observeAdvert(b, 1800000010UL);
  fallback.observeAdvert(c, 1800000020UL);
  EXPECT_EQ(clock.getCurrentTime(), CLOCK_SYNC_NRF52_SEED);
}

TEST(MeshClockFallback, DoesNotPullValidTimeBackward) {
  FakeClock clock(1800001000UL);
  MeshClockFallback fallback;
  fallback.begin(&clock);
  uint8_t a[32] = {1}, b[32] = {2}, c[32] = {3};
  fallback.observeAdvert(a, 1800000000UL);
  fallback.observeAdvert(b, 1800000010UL);
  fallback.observeAdvert(c, 1800000020UL);
  EXPECT_EQ(clock.getCurrentTime(), 1800001000UL);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
