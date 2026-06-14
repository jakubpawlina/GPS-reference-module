/**
 * @file status_presentation.cpp
 * @brief Presentation-layer transforms for OLED display rows and LED patterns.
 *
 * Converts raw GpsData and GpsValiditySnapshot into formatted strings
 * (DisplayModel) and boolean LED states (LedPattern).  All formatting
 * decisions live here so the hardware modules stay policy-free.
 */
#include "status_presentation.h"

#include <cstdio>

namespace StatusPresentation {

LedPattern buildLedPattern(const GpsProcessing::GpsValiditySnapshot &snapshot) {
  LedPattern pattern;
  pattern.data = snapshot.freshNmea;
  pattern.error = snapshot.diagnosticState == GpsProcessing::DiagnosticState::NoData ||
                  snapshot.diagnosticState == GpsProcessing::DiagnosticState::NoFix;
  pattern.warning = snapshot.diagnosticState == GpsProcessing::DiagnosticState::Degraded2D ||
                    snapshot.diagnosticState == GpsProcessing::DiagnosticState::DegradedLowSat;
  pattern.ok = snapshot.diagnosticState == GpsProcessing::DiagnosticState::Ok;
  return pattern;
}

/** @brief Format the NMEA data age as a human-readable string (e.g. "<1s", "STALE"). */
static void formatDisplayAge(const GpsProcessing::GpsData &gps,
                             const GpsProcessing::GpsValiditySnapshot &snapshot, char *buffer,
                             size_t bufferSize) {
  if (!buffer || bufferSize == 0) {
    return;
  }

  if (!gps.nmeaValid) {
    snprintf(buffer, bufferSize, "---");
    return;
  }

  if (!snapshot.freshNmea) {
    snprintf(buffer, bufferSize, "STALE");
    return;
  }

  if (snapshot.nmeaAgeMs < 1000) {
    snprintf(buffer, bufferSize, "<1s");
    return;
  }

  const uint32_t ageSeconds = snapshot.nmeaAgeMs / 1000;
  snprintf(buffer, bufferSize, "%lus", static_cast<unsigned long>(ageSeconds));
}

void buildDisplayModel(const GpsProcessing::GpsData &gps,
                       const GpsProcessing::GpsValiditySnapshot &snapshot, DisplayModel &model) {
  snprintf(model.state, sizeof(model.state), "%s",
           GpsProcessing::diagnosticStateToDisplay(snapshot.diagnosticState));
  snprintf(model.fix, sizeof(model.fix), "%s", GpsProcessing::fixTypeToDisplay(gps, snapshot));

  if (snapshot.freshGga) {
    snprintf(model.sats, sizeof(model.sats), "%02u", static_cast<unsigned int>(gps.satellitesUsed));
  } else {
    snprintf(model.sats, sizeof(model.sats), "---");
  }

  if (snapshot.usablePosition) {
    snprintf(model.latitude, sizeof(model.latitude), "%.6f", gps.latitude);
    snprintf(model.longitude, sizeof(model.longitude), "%.6f", gps.longitude);
  } else {
    snprintf(model.latitude, sizeof(model.latitude), "---");
    snprintf(model.longitude, sizeof(model.longitude), "---");
  }

  if (snapshot.usableAltitude) {
    snprintf(model.altitude, sizeof(model.altitude), "%.1fm", gps.altitudeM);
  } else {
    snprintf(model.altitude, sizeof(model.altitude), "---");
  }

  if (snapshot.usableHdop) {
    snprintf(model.hdop, sizeof(model.hdop), "%.1f", gps.hdop);
  } else {
    snprintf(model.hdop, sizeof(model.hdop), "---");
  }

  formatDisplayAge(gps, snapshot, model.age, sizeof(model.age));
}

} // namespace StatusPresentation
