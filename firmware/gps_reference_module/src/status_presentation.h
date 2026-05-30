#pragma once

#include "gps_processing.h"

/**
 * @brief Presentation-layer transforms for LEDs and OLED rows.
 *
 * The firmware uses this module to keep UI policy deterministic and testable
 * without involving hardware drivers.
 */
namespace StatusPresentation {

/** @brief Logical LED outputs for the four status indicators. */
struct LedPattern {
  bool error = false;
  bool data = false;
  bool warning = false;
  bool ok = false;
};

/** @brief Fully formatted OLED content for one refresh cycle. */
struct DisplayModel {
  char state[24] = "";
  char fix[8] = "";
  char sats[8] = "";
  char latitude[24] = "";
  char longitude[24] = "";
  char altitude[24] = "";
  char hdop[16] = "";
  char age[16] = "";
};

/** @brief Derive the current LED pattern from a validity snapshot. */
LedPattern buildLedPattern(const GpsProcessing::GpsValiditySnapshot &snapshot);
/** @brief Derive all formatted OLED rows from raw GPS state. */
void buildDisplayModel(
  const GpsProcessing::GpsData &gps,
  const GpsProcessing::GpsValiditySnapshot &snapshot,
  DisplayModel &model
);

}  // namespace StatusPresentation
