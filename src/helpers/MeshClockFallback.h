#pragma once

#include <MeshCore.h>
#include <helpers/ClockSyncUtils.h>
#include <string.h>

namespace mesh {

// Fallback-only mesh clock: collect signed-advert timestamps from distinct
// sources and set the RTC once if GPS/NTP/manual/hardware have not.
class MeshClockFallback {
  static constexpr uint8_t SAMPLE_SLOTS = 8;

  struct Sample {
    uint32_t epoch;
    uint8_t source_id[4];
    bool active;
  };

  Sample _samples[SAMPLE_SLOTS];
  RTCClock* _rtc;

  void tryApply() {
    if (!_rtc) return;
    uint32_t values[SAMPLE_SLOTS];
    uint8_t count = 0;
    for (uint8_t i = 0; i < SAMPLE_SLOTS; i++) {
      if (_samples[i].active) values[count++] = _samples[i].epoch;
    }
    ClockSyncConsensusResult result = evaluateClockSyncConsensus(
        values, count, CLOCK_SYNC_REQUIRED_SAMPLES, CLOCK_SYNC_WINDOW_SECONDS);
    if (!result.consensus) return;
    if (!shouldApplyMeshClockFallback(_rtc->isAuthoritative(),
                                      _rtc->hasValidTime(),
                                      _rtc->getCurrentTime(),
                                      result.estimate)) {
      return;
    }
    _rtc->setCurrentTime(result.estimate);
    _rtc->markValid();  // not authoritative: GPS/NTP/manual can still win
  }

public:
  MeshClockFallback() : _rtc(nullptr) {
    memset(_samples, 0, sizeof(_samples));
  }

  void begin(RTCClock* rtc) { _rtc = rtc; }

  void observeAdvert(const uint8_t* pub_key, uint32_t timestamp) {
    if (!_rtc || _rtc->isAuthoritative() || _rtc->hasValidTime()) return;
    if (pub_key == nullptr || !clockSyncEstimateInBounds(timestamp)) return;

    int slot = -1;
    for (uint8_t i = 0; i < SAMPLE_SLOTS; i++) {
      if (_samples[i].active && memcmp(_samples[i].source_id, pub_key, 4) == 0) {
        slot = i;
        break;
      }
      if (slot < 0 && !_samples[i].active) slot = i;
    }
    if (slot < 0) slot = 0;  // overwrite oldest-index 0 if full
    memcpy(_samples[slot].source_id, pub_key, 4);
    _samples[slot].epoch = timestamp;
    _samples[slot].active = true;
    tryApply();
  }
};

}  // namespace mesh
