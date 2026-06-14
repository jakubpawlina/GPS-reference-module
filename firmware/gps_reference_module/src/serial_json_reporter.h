#pragma once

#include <stdint.h>

#include "gps_processing.h"

/**
 * @brief JSON Lines reporter for USB serial output.
 *
 * The service process consumes this output directly, so field names and
 * semantics are treated as a public interface.
 */
namespace SerialJsonReporter {

/** @brief Emit the one-time startup event. */
void reportStartupJson(bool displayReady, uint8_t oledAddress);
/** @brief Emit a raw-NMEA observation event. */
void reportRawNmeaJson(const char *sentence, bool checksumOk);
/** @brief Emit the current parsed-state event. */
void reportParsedStateJson(const GpsProcessing::GpsData &gps,
                           const GpsProcessing::GpsValiditySnapshot &snapshot, bool displayReady);

} // namespace SerialJsonReporter
