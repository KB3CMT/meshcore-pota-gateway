#pragma once

#include <stdint.h>

namespace mesh {

// Consensus math from mikecarper/MeshCore (b80971b85 ClockSyncUtils.h).
// Used here only as a fallback when GPS, NTP, manual time, and a hardware
// RTC have not already set the clock.

static const uint32_t CLOCK_SYNC_MIN_EPOCH = 1704067200UL;  // 2024-01-01 UTC
static const uint32_t CLOCK_SYNC_MAX_EPOCH = 4102444799UL;  // 2099-12-31 UTC
static const uint32_t CLOCK_SYNC_NRF52_SEED = 1715770351UL; // VolatileRTCClock / clkreboot
static const uint32_t CLOCK_SYNC_ESP32_SEED = 1772323200UL; // ESP32RTCClock RTC_TIME_MIN
static const uint8_t CLOCK_SYNC_REQUIRED_SAMPLES = 3;
static const uint32_t CLOCK_SYNC_WINDOW_SECONDS = 120;

inline bool clockSyncEstimateInBounds(uint32_t epoch) {
  return epoch >= CLOCK_SYNC_MIN_EPOCH && epoch <= CLOCK_SYNC_MAX_EPOCH;
}

inline bool clockLooksUnset(uint32_t epoch) {
  if (!clockSyncEstimateInBounds(epoch)) return true;
  if (epoch == CLOCK_SYNC_NRF52_SEED) return true;
  if (epoch == CLOCK_SYNC_ESP32_SEED) return true;
  return false;
}

inline bool shouldApplyMeshClockFallback(bool authoritative, bool has_valid_time,
                                         uint32_t current, uint32_t estimate) {
  if (authoritative || has_valid_time) return false;
  if (!clockSyncEstimateInBounds(estimate)) return false;
  // Never pull a clock that already looks valid backward (POTA spots).
  if (!clockLooksUnset(current) && estimate < current) return false;
  return true;
}

struct ClockSyncConsensusResult {
  bool consensus;
  uint8_t fresh_count;
  uint8_t agreeing_count;
  uint8_t required_count;
  uint32_t estimate;
};

inline ClockSyncConsensusResult evaluateClockSyncConsensus(uint32_t* values,
                                                           uint8_t count,
                                                           uint8_t configured_required,
                                                           uint32_t window_seconds) {
  ClockSyncConsensusResult result = {false, count, 0, configured_required, 0};
  if (values == nullptr || count < configured_required) return result;

  uint8_t strict_majority = (uint8_t)(count / 2U + 1U);
  if (strict_majority > result.required_count) result.required_count = strict_majority;

  for (uint8_t i = 1; i < count; i++) {
    uint32_t value = values[i];
    uint8_t j = i;
    while (j > 0 && values[j - 1] > value) {
      values[j] = values[j - 1];
      j--;
    }
    values[j] = value;
  }

  result.estimate = values[count / 2U];
  for (uint8_t i = 0; i < count; i++) {
    uint32_t difference = values[i] > result.estimate
        ? values[i] - result.estimate : result.estimate - values[i];
    if (difference <= window_seconds) result.agreeing_count++;
  }
  result.consensus = result.agreeing_count >= result.required_count;
  return result;
}

}  // namespace mesh
