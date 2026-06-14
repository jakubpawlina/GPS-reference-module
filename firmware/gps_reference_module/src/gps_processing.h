#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Pure firmware logic for NMEA parsing and diagnostic-state evaluation.
 *
 * This module is intentionally free of Arduino runtime dependencies so it can
 * be compiled and tested on the host with a normal C++ compiler.
 */
namespace GpsProcessing {

/** @brief High-level diagnostic state exposed by the firmware. */
enum class DiagnosticState : uint8_t { NoData, NoFix, Degraded2D, DegradedLowSat, Ok };

/** @brief Supported NMEA sentence types handled by the parser. */
enum class NmeaSentenceType : uint8_t { Unknown, Gga, Gsa, Rmc };

/** @brief Accumulated GPS state built from recently parsed NMEA sentences. */
struct GpsData {
  bool nmeaValid = false;

  bool hasFix = false;
  bool locationValid = false;
  bool altitudeValid = false;
  bool geoidValid = false;

  bool fixTypeValid = false;
  bool hdopValid = false;
  bool pdopValid = false;
  bool vdopValid = false;

  bool speedValid = false;
  bool courseValid = false;
  bool timeValid = false;
  bool dateValid = false;

  uint8_t fixType = 0;
  uint8_t fixQuality = 0;
  uint8_t satellitesUsed = 0;

  double latitude = 0.0;
  double longitude = 0.0;
  double altitudeM = 0.0;
  double geoidSeparationM = 0.0;

  double hdop = 0.0;
  double pdop = 0.0;
  double vdop = 0.0;

  double speedKnots = 0.0;
  double speedKmh = 0.0;
  double courseDeg = 0.0;

  char utcTime[16] = "";
  char utcDate[8] = "";

  uint32_t lastNmeaMs = 0;
  uint32_t lastGgaMs = 0;
  uint32_t lastGsaMs = 0;
  uint32_t lastRmcMs = 0;

  uint32_t acceptedSentenceCount = 0;
  uint32_t rawSentenceCount = 0;
  uint32_t checksumErrorCount = 0;
  uint32_t bufferOverflowCount = 0;
};

/** @brief Time-relative interpretation of GpsData for a single loop tick. */
struct GpsValiditySnapshot {
  uint32_t nowMs = 0;

  bool freshNmea = false;
  bool freshGga = false;
  bool freshGsa = false;
  bool freshRmc = false;

  bool freshPositionSource = false;
  bool freshFixSource = false;
  bool currentFix = false;
  bool usablePosition = false;
  bool usableAltitude = false;
  bool usableHdop = false;

  uint32_t nmeaAgeMs = 0;
  uint32_t ggaAgeMs = 0;
  uint32_t gsaAgeMs = 0;
  uint32_t rmcAgeMs = 0;

  DiagnosticState diagnosticState = DiagnosticState::NoData;
};

/** @brief Validate a NMEA checksum, optionally accepting checksum-less input. */
bool verifyNmeaChecksum(const char *sentence, bool requireChecksum);
/** @brief Detect the handled NMEA sentence type directly from raw text. */
NmeaSentenceType detectSentenceTypeFromRaw(const char *sentence);
/** @brief Convert a sentence type enum to protocol text. */
const char *sentenceTypeToText(NmeaSentenceType type);
/** @brief Parse one accepted sentence and merge its contents into gps.
 *
 * When checksumAlreadyVerified is true the caller has already validated the
 * checksum — the function skips re-verification and treats the sentence as
 * accepted.  Pass false to perform the checksum check internally.
 */
void processNmeaSentence(GpsData &gps, const char *sentence, uint32_t nowMs, bool requireChecksum,
                         bool checksumAlreadyVerified = false);
/** @brief Build a time-relative validity snapshot used by output and LEDs. */
GpsValiditySnapshot buildGpsSnapshot(const GpsData &gps, uint32_t nowMs, uint32_t gpsDataTimeoutMs,
                                     uint8_t minOkSatellites);
/** @brief Convert a diagnostic state to the JSON API representation. */
const char *diagnosticStateToJson(DiagnosticState state);
/** @brief Convert a diagnostic state to the compact OLED label. */
const char *diagnosticStateToDisplay(DiagnosticState state);
/** @brief Convert fix information to the JSON fix type text. */
const char *fixTypeToText(const GpsData &gps, const GpsValiditySnapshot &snapshot);
/** @brief Convert fix information to the compact OLED label. */
const char *fixTypeToDisplay(const GpsData &gps, const GpsValiditySnapshot &snapshot);

} // namespace GpsProcessing
