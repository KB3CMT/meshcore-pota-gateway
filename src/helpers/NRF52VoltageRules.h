#pragma once

#include <stdint.h>

namespace mesh {
namespace power {

// LiFePO4 pack limits used by the opt-in nRF52 cutoff
// (mikecarper/MeshCore 11d64b3db).
static const uint16_t LIFEPO4_BOOTLOCK_MV_DEFAULT = 2900;
static const uint16_t LIFEPO4_CUTOFF_MV_DEFAULT = 2700;
static const uint16_t LIFEPO4_EMPTY_MV = 2700;
static const uint16_t LIFEPO4_FULL_MV = 3550;

static const uint8_t LOW_VOLTAGE_CONFIRM_READINGS = 3;

inline uint16_t medianVoltage(uint16_t a, uint16_t b, uint16_t c) {
  if (a > b) { uint16_t t = a; a = b; b = t; }
  if (b > c) { uint16_t t = b; b = c; c = t; }
  if (a > b) { uint16_t t = a; a = b; b = t; }
  return b;
}

// Valid ADC readings sit above 1000 mV. Anything at or below that is treated
// as a glitch rather than a discharged cell.
inline bool shouldBootLock(uint16_t voltage_mv, uint16_t threshold_mv,
                           bool externally_powered) {
  return !externally_powered && threshold_mv != 0
      && voltage_mv > 1000 && voltage_mv < threshold_mv;
}

// Count consecutive in-range readings below the running cutoff. External
// power, a disabled cutoff, an invalid ADC reading, or a recovery above the
// threshold all reset the counter. Matches companion #3462 (3-reading confirm).
inline uint8_t nextLowVoltageCount(uint8_t previous, uint16_t mv,
                                   uint16_t cutoff, bool external_power) {
  if (cutoff == 0 || external_power || mv <= 1000 || mv >= cutoff) return 0;
  return previous < LOW_VOLTAGE_CONFIRM_READINGS
      ? (uint8_t)(previous + 1) : LOW_VOLTAGE_CONFIRM_READINGS;
}

} // namespace power
} // namespace mesh
